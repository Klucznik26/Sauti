#include "sauti_mp3_sdl_player.h"
#include "sauti_decoder.h"
#include "sauti_decoder_mp3.h"
#include "sauti_decoder_vorbis.h"
#include "sauti_decoder_wav.h"
#include "sauti_decoder_voc.h"
#include "sauti_decoder_aiff.h"
#include "sauti_decoder_ffmpeg.h"
#include "sauti_decoder_aac_native.h"
#include "sauti_decoder_midi.h"
#include "sauti_decoder_opus.h"
#include "sauti_decoder_flac.h"
#include "sauti_decoder_wavpack.h"
#include "sauti_decoder_avr.h"
#include "sauti_decoder_dvsm.h"
#include "sauti_decoder_dtm.h"
#ifdef SAUTI_HAVE_ATARIAUDIO
#include "sauti_decoder_sndh.h"
#endif
#include "sauti_decoder_ym.h"
#include "sauti_ym_archive.h"
#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

struct SautiMp3SdlPlayer {
    uint8_t *bytes;
    SautiDecoder decoder;
    SautiYm2Sample ym2_bank[128];
    uint8_t *ym2_owned[128];
    SDL_AudioDeviceID device;
    uint64_t submitted,loop_start;
    int has_loop;
    int active,paused,ended,muted,diagnosed;
    SDL_mutex *seek_mutex;
    SDL_Thread *seek_thread;
    SDL_atomic_t seek_busy;
    uint64_t seek_request;
    int seek_pending;
};
/* AAC-LC seek must not run thousands of IMDCT frames on the SDL/UI
 * event thread. The worker owns decoder state while seek_busy is set;
 * pump() never reads it concurrently. New slider positions coalesce
 * to the newest requested frame, without launching parallel decoders. */
static int aac_seek_worker(void *userdata){
    SautiMp3SdlPlayer *p=userdata;
    for(;;){
        SDL_LockMutex(p->seek_mutex);
        if(!p->seek_pending){
            SDL_AtomicSet(&p->seek_busy,0);
            SDL_UnlockMutex(p->seek_mutex);
            return 0;
        }
        const uint64_t frame=p->seek_request;
        p->seek_pending=0;
        SDL_UnlockMutex(p->seek_mutex);
        const int ok=sauti_decoder_seek(&p->decoder,frame);
        SDL_LockMutex(p->seek_mutex);
        if(p->seek_pending){
            /* Slider was moved again while this seek was running.
             * Keep the old audio queued until the final target is ready. */
            SDL_UnlockMutex(p->seek_mutex);
            continue;
        }
        if(ok){
            /* Atomically switch from old queued PCM to the prepared target.
             * Clearing at request time caused 0.5-1 s of silence. */
            SDL_ClearQueuedAudio(p->device);
            p->submitted=frame;
            p->ended=0;
            p->diagnosed=0;
        }else{
            p->ended=1;
            fprintf(stderr,"Sauti AAC: failed to seek to frame %llu\\n",
                    (unsigned long long)frame);
        }
        SDL_AtomicSet(&p->seek_busy,0);
        SDL_UnlockMutex(p->seek_mutex);
        return 0;
    }
}
static void aac_seek_join(SautiMp3SdlPlayer *p){
    if(!p->seek_mutex)return;
    /* Stop only after any active worker has finished using decoder
     * and the incoming compressed-data buffer. */
    SDL_LockMutex(p->seek_mutex);
    p->seek_pending=0;
    SDL_UnlockMutex(p->seek_mutex);
    if(p->seek_thread){
        SDL_WaitThread(p->seek_thread,NULL);
        p->seek_thread=NULL;
    }
}
/* Optional unsigned PCM8 bank, supplied by the user, not bundled.
 * Filename scheme: SAUTI_YM2_BANK_DIR/<index>.raw (0..127).
 * Limit every sample to 1 MiB. The SDL player owns every allocation. */
static int load_ym2_bank(SautiMp3SdlPlayer *p,const char *directory){
    if(!directory || !*directory)return 0;
    size_t loaded=0;
    for(unsigned id=0;id<128;id++){
        char filename[4096];
        int n=snprintf(filename,sizeof(filename),"%s/%u.raw",directory,id);
        if(n<0 || (size_t)n>=sizeof(filename))return -1;
        FILE *f=fopen(filename,"rb");
        if(!f)continue;
        if(fseek(f,0,SEEK_END)!=0){fclose(f);return -1;}
        long size=ftell(f);
        if(size<=0 || size>1048576L || fseek(f,0,SEEK_SET)!=0){
            fclose(f);return -1;
        }
        uint8_t *data=malloc((size_t)size);
        if(!data){fclose(f);return -1;}
        size_t got=fread(data,1,(size_t)size,f);
        fclose(f);
        if(got!=(size_t)size){free(data);return -1;}
        p->ym2_owned[id]=data;
        p->ym2_bank[id].data=data;
        p->ym2_bank[id].size=(uint32_t)size;
        loaded++;
    }
    if(!loaded)return 0;
    return sauti_decoder_ym2_set_bank(&p->decoder,p->ym2_bank,128)
        ? (int)loaded : -1;
}
static uint64_t queued_frames(const SautiMp3SdlPlayer *p){
    return SDL_GetQueuedAudioSize(p->device)/(sizeof(float)*p->decoder.info.channels);
}
SautiMp3SdlPlayer *sauti_mp3_sdl_open(const char *path){
    FILE *f=fopen(path,"rb");
    if(!f)return NULL;
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return NULL;}
    long size=ftell(f);
    if(size<=0 || fseek(f,0,SEEK_SET)!=0){fclose(f);return NULL;}
    SautiMp3SdlPlayer *p=calloc(1,sizeof(*p));
    if(!p){fclose(f);return NULL;}
    p->bytes=malloc((size_t)size);
    if(!p->bytes){fclose(f);free(p);return NULL;}
    size_t len=fread(p->bytes,1,(size_t)size,f);
    fclose(f);
    /* Validate the on-disk read before replacing the compressed buffer.
     * Once LHA is unpacked, len is intentionally different from size. */
    if(len!=(size_t)size){
        fprintf(stderr,"Sauti: incomplete file read: %s\n",path);
        sauti_mp3_sdl_destroy(p);return NULL;
    }
    {
        uint8_t *raw=NULL;
        size_t raw_size=0;
        int unpack=sauti_ym_unpack_lha(p->bytes,len,&raw,&raw_size);
        if(unpack<0){
            fprintf(stderr,"Sauti: LHA YM archive unsupported or invalid: %s\n",path);
            sauti_mp3_sdl_destroy(p);return NULL;
        }
        if(unpack==1){
            free(p->bytes);p->bytes=raw;len=raw_size;
            fprintf(stderr,"Sauti: unpacked LHA YM in memory: %zu bytes\n",len);
        }
    }
    const SautiDecoderOps *decoders[]={
#ifdef SAUTI_HAVE_ATARIAUDIO
        &sauti_decoder_sndh_ops,
#endif
        &sauti_decoder_wav_ops,&sauti_decoder_voc_ops,&sauti_decoder_aiff_ops,&sauti_decoder_aac_native_ops,&sauti_decoder_aac_ops,&sauti_decoder_alac_ops,&sauti_decoder_webm_ops,&sauti_decoder_opus_ops,&sauti_decoder_vorbis_ops,&sauti_decoder_midi_ops,&sauti_decoder_flac_ops,&sauti_decoder_wavpack_ops,&sauti_decoder_avr_ops,&sauti_decoder_dvsm_ops,&sauti_decoder_dtm_ops,&sauti_decoder_ym_ops,&sauti_decoder_mp3_ops};
    if(!sauti_decoder_open(&p->decoder,decoders,sizeof(decoders)/sizeof(decoders[0]),p->bytes,len)){
        sauti_mp3_sdl_destroy(p);return NULL;
    }
    /* Only YM2 with actual Mad Max triggers needs an external PCM8 bank. */
    if(p->decoder.ops==&sauti_decoder_ym_ops && len>=18u &&
       memcmp(p->bytes,"YM2!",4)==0 && (len-4u)%14u==0u){
        size_t frames=(len-4u)/14u, triggers=0, missing=0;
        const uint8_t *r10=p->bytes+4u+10u*frames;
        const uint8_t *r12=p->bytes+4u+12u*frames;
        unsigned char required[128]={0};
        for(size_t i=0;i<frames;i++)
            if((r10[i]&0x80u) && r12[i]){
                required[r10[i]&0x7fu]=1;
                triggers++;
            }
        if(triggers){
            int loaded=load_ym2_bank(p,getenv("SAUTI_YM2_BANK_DIR"));
            if(loaded<0){
                fprintf(stderr,"Sauti YM2: failed to load external PCM8 bank; "
                               "check SAUTI_YM2_BANK_DIR and sample sizes.\n");
                sauti_mp3_sdl_destroy(p);return NULL;
            }
            for(unsigned id=0;id<128;id++)
                if(required[id] && !p->ym2_bank[id].data)missing++;
            if(missing)
                fprintf(stderr,"Sauti YM2: %zu Mad Max event(s), "
                               "%zu missing sample ID(s); percussion incomplete.\n",
                        triggers,missing);
            else
                fprintf(stderr,"Sauti YM2: external sample bank ready "
                               "(%d loaded, %zu trigger(s)).\n",loaded,triggers);
        }
    }
    p->has_loop=sauti_decoder_ym_loop_start(&p->decoder,&p->loop_start);
    /* This SDL adapter currently queues mono/stereo interleaved PCM only. */
    if(p->decoder.info.channels<1 || p->decoder.info.channels>2){
        sauti_mp3_sdl_destroy(p);return NULL;
    }
    SDL_AudioSpec want={0},obtained={0};
    want.freq=(int)p->decoder.info.sample_rate;
    want.channels=(Uint8)p->decoder.info.channels;
    want.samples=2048;
    want.format=AUDIO_F32SYS;
    p->device=SDL_OpenAudioDevice(NULL,0,&want,&obtained,0);
    if(!p->device){sauti_mp3_sdl_destroy(p);return NULL;}
    if(obtained.format!=AUDIO_F32SYS || obtained.freq!=want.freq || obtained.channels!=want.channels){
        fprintf(stderr,"Sauti PCM: unexpected SDL device format; rejecting mismatched audio output\n");
        sauti_mp3_sdl_destroy(p);return NULL;
    }
    fprintf(stderr,"Sauti PCM output: SDL AUDIO_F32SYS, %d Hz, %u ch\n",obtained.freq,(unsigned)obtained.channels);
    fprintf(stderr,"Sauti PCM: codec=%s, %u Hz, %u ch, %.2f s, device=%u, muted=%d\n",
            p->decoder.ops->name,p->decoder.info.sample_rate,p->decoder.info.channels,
            sauti_mp3_sdl_duration(p),(unsigned)p->device,p->muted);
    if(p->decoder.ops==&sauti_decoder_aac_native_ops){
        p->seek_mutex=SDL_CreateMutex();
        if(!p->seek_mutex){
            fprintf(stderr,"Sauti AAC: failed to create seek mutex\\n");
            sauti_mp3_sdl_destroy(p);return NULL;
        }
        SDL_AtomicSet(&p->seek_busy,0);
    }
    SDL_PauseAudioDevice(p->device,1);
    return p;
}
void sauti_mp3_sdl_destroy(SautiMp3SdlPlayer *p){
    if(!p)return;
    aac_seek_join(p);
    if(p->seek_mutex)SDL_DestroyMutex(p->seek_mutex);
    if(p->device)SDL_CloseAudioDevice(p->device);
    sauti_decoder_close(&p->decoder);
    for(unsigned id=0;id<128;id++)free(p->ym2_owned[id]);
    free(p->bytes);
    free(p);
}
void sauti_mp3_sdl_pump(SautiMp3SdlPlayer *p){
    if(!p || !p->active || p->paused)return;
    if(p->seek_mutex && SDL_AtomicGet(&p->seek_busy))return;
    if(p->ended){
        if(queued_frames(p)==0){
            p->active=0;
            SDL_PauseAudioDevice(p->device,1);
        }
        return;
    }
    /* A two-second safety buffer keeps the current song audible while
     * an AAC seek worker reconstructs history for a distant target. */
    const uint64_t limit=(uint64_t)p->decoder.info.sample_rate*
                         (p->seek_mutex?2u:1u)/ (p->seek_mutex?1u:2u);
    float pcm[4096];
    while(queued_frames(p)<limit){
        size_t frames=sauti_decoder_read(&p->decoder,pcm,
                                         4096/p->decoder.info.channels);
        if(!frames){
            if(p->has_loop && sauti_decoder_seek(&p->decoder,p->loop_start)){
                /* The first traversal plays the intro, later ones loop. */
                frames=sauti_decoder_read(&p->decoder,pcm,
                                          4096/p->decoder.info.channels);
            }
            if(!frames){p->ended=1;break;}
        }
        size_t count=frames*p->decoder.info.channels;
        if(!p->diagnosed){
            float peak=0.0f;
            for(size_t i=0;i<count;++i){
                float a=pcm[i]<0.0f?-pcm[i]:pcm[i];
                if(a>peak)peak=a;
            }
            fprintf(stderr,"Sauti PCM first block: frames=%zu, peak=%.6f, muted=%d\n",frames,(double)peak,p->muted);
            p->diagnosed=1;
        }
        if(p->muted)memset(pcm,0,count*sizeof(float));
        if(SDL_QueueAudio(p->device,pcm,(Uint32)(count*sizeof(float)))!=0){
            p->ended=1;
            fprintf(stderr,"Sauti MP3 PCM queue: %s\n",SDL_GetError());
            break;
        }
        p->submitted+=frames;
    }
    if(p->ended && queued_frames(p)==0){
        p->active=0;
        SDL_PauseAudioDevice(p->device,1);
    }
}
void sauti_mp3_sdl_play(SautiMp3SdlPlayer *p){
    if(!p)return;
    if(p->ended && !p->active){
        sauti_decoder_seek(&p->decoder,0);
        p->submitted=0;p->ended=0;
    }
    fprintf(stderr,"Sauti PCM: PLAY requested, codec=%s, device=%u, queued=%u bytes\n",
            p->decoder.ops->name,(unsigned)p->device,
            (unsigned)SDL_GetQueuedAudioSize(p->device));
    p->active=1;p->paused=0;
    sauti_mp3_sdl_pump(p);
    SDL_PauseAudioDevice(p->device,0);
    fprintf(stderr,"Sauti PCM: PLAY running, queued=%u bytes, status=%d\n",
            (unsigned)SDL_GetQueuedAudioSize(p->device),
            (int)SDL_GetAudioDeviceStatus(p->device));
}
void sauti_mp3_sdl_pause(SautiMp3SdlPlayer *p){
    if(!p || !p->active)return;
    p->paused=1;
    SDL_PauseAudioDevice(p->device,1);
}
void sauti_mp3_sdl_stop(SautiMp3SdlPlayer *p){
    if(!p)return;
    aac_seek_join(p);
    SDL_PauseAudioDevice(p->device,1);
    SDL_ClearQueuedAudio(p->device);
    sauti_decoder_seek(&p->decoder,0);
    p->submitted=0;p->active=0;p->paused=0;p->ended=0;p->diagnosed=0;
}
void sauti_mp3_sdl_seek(SautiMp3SdlPlayer *p,double sec){
    if(!p || sec<0.0)return;
    double total=sauti_mp3_sdl_duration(p);
    if(sec>total)sec=total;
    uint64_t frame=(uint64_t)(sec*p->decoder.info.sample_rate);
    if(p->seek_mutex){
        SDL_LockMutex(p->seek_mutex);
        p->seek_request=frame;
        p->seek_pending=1;
        if(!SDL_AtomicGet(&p->seek_busy)){
            /* Previous thread has already exited: join without blocking
             * on a live seek, then create the next worker. */
            if(p->seek_thread){
                SDL_Thread *previous=p->seek_thread;
                p->seek_thread=NULL;
                SDL_UnlockMutex(p->seek_mutex);
                SDL_WaitThread(previous,NULL);
                SDL_LockMutex(p->seek_mutex);
            }
            SDL_AtomicSet(&p->seek_busy,1);
            p->seek_thread=SDL_CreateThread(aac_seek_worker,
                                              "sauti-aac-seek",p);
            if(!p->seek_thread){
                p->seek_pending=0;
                SDL_AtomicSet(&p->seek_busy,0);
                fprintf(stderr,"Sauti AAC: failed to create seek thread\\n");
            }
        }
        SDL_UnlockMutex(p->seek_mutex);
        return;
    }
    SDL_ClearQueuedAudio(p->device);
    if(sauti_decoder_seek(&p->decoder,frame)){
        p->submitted=frame;p->ended=0;
        if(p->active && !p->paused)sauti_mp3_sdl_pump(p);
    }
}
void sauti_mp3_sdl_mute(SautiMp3SdlPlayer *p,int muted){
    if(p)p->muted=muted!=0;
}
int sauti_mp3_sdl_playing(const SautiMp3SdlPlayer *p){
    return p && p->active;
}
int sauti_mp3_sdl_paused(const SautiMp3SdlPlayer *p){
    return p && p->active && p->paused;
}
double sauti_mp3_sdl_position(const SautiMp3SdlPlayer *p){
    if(!p)return 0.0;
    uint64_t queued=queued_frames(p);
    uint64_t consumed=p->submitted>queued?p->submitted-queued:0;
    if(p->has_loop && consumed>=p->decoder.info.total_frames){
        uint64_t span=p->decoder.info.total_frames-p->loop_start;
        consumed=p->loop_start+(consumed-p->decoder.info.total_frames)%span;
    }
    return (double)consumed/p->decoder.info.sample_rate;
}
double sauti_mp3_sdl_duration(const SautiMp3SdlPlayer *p){
    if(!p)return 0.0;
    return (double)p->decoder.info.total_frames/p->decoder.info.sample_rate;
}
