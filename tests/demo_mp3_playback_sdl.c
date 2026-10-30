/* Independent smoke test: portable MP3 decoder -> PCM output -> SDL audio.
 * This exercises the future player pipeline without changing the live UI.
 */
#include "sauti_decoder.h"
#include "sauti_decoder_mp3.h"
#include "sauti_pcm_output.h"
#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    SDL_AudioDeviceID device;
    uint32_t channels;
} SdlSink;

static int sink_open(void *ctx,SautiPcmFormat format){
    SdlSink *s=ctx;
    SDL_AudioSpec want={0},got={0};
    want.freq=(int)format.sample_rate;
    want.format=AUDIO_S16SYS;
    want.channels=(Uint8)format.channels;
    want.samples=2048;
    s->device=SDL_OpenAudioDevice(NULL,0,&want,&got,0);
    if(!s->device)return 0;
    s->channels=format.channels;
    SDL_PauseAudioDevice(s->device,0);
    return 1;
}
static size_t sink_write(void *ctx,const float *pcm,size_t frames){
    SdlSink *s=ctx;
    int16_t out[4096];
    if(frames>4096/s->channels)return 0;
    for(size_t i=0;i<frames*s->channels;++i){
        float v=pcm[i];
        if(v>1.0f)v=1.0f;
        if(v< -1.0f)v= -1.0f;
        out[i]=(int16_t)(v*32767.0f);
    }
    if(SDL_QueueAudio(s->device,out,(Uint32)(frames*s->channels*2))!=0)return 0;
    return frames;
}
static void sink_pause(void *ctx,int p){
    SdlSink *s=ctx;
    SDL_PauseAudioDevice(s->device,p);
}
static void sink_flush(void *ctx){
    SdlSink *s=ctx;
    SDL_ClearQueuedAudio(s->device);
}
static size_t sink_queued(const void *ctx){
    const SdlSink *s=ctx;
    return SDL_GetQueuedAudioSize(s->device)/(s->channels*2);
}
static void sink_close(void *ctx){
    SdlSink *s=ctx;
    SDL_CloseAudioDevice(s->device);
    s->device=0;
}
static const SautiPcmOutputOps sink_ops={
    sink_open,sink_write,sink_pause,sink_flush,sink_queued,sink_close
};

int main(int argc,char **argv){
    if(argc!=2){fprintf(stderr,"Uzycie: %s plik.mp3\n",argv[0]);return 2;}
    FILE *f=fopen(argv[1],"rb");
    if(!f){perror("MP3 fopen");return 1;}
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return 1;}
    long len=ftell(f);
    if(len<=0 || fseek(f,0,SEEK_SET)!=0){fclose(f);return 1;}
    uint8_t *bytes=malloc((size_t)len);
    if(!bytes){fclose(f);return 1;}
    size_t read_bytes=fread(bytes,1,(size_t)len,f);
    fclose(f);
    if(read_bytes!=(size_t)len){free(bytes);return 1;}
    if(SDL_Init(SDL_INIT_AUDIO)!=0){
        fprintf(stderr,"SDL audio: %s\n",SDL_GetError());
        free(bytes);return 1;
    }
    const SautiDecoderOps *decoders[]={&sauti_decoder_mp3_ops};
    SautiDecoder decoder={0};
    SautiPcmOutput output={0};
    SdlSink sink={0};
    int ok=sauti_decoder_open(&decoder,decoders,1,bytes,(size_t)len);
    if(ok){
        SautiPcmFormat format={decoder.info.sample_rate,decoder.info.channels};
        ok=sauti_pcm_output_open(&output,&sink,&sink_ops,format);
    }
    if(!ok){
        fprintf(stderr,"Nie mozna otworzyc MP3 lub urzadzenia PCM: %s\n",SDL_GetError());
    }else{
        fprintf(stderr,"Sauti portable MP3: %u Hz, %u channels (SDL PCM output)\n",
                decoder.info.sample_rate,decoder.info.channels);
        /* Smoke test only: exit automatically after 15 seconds. */
        const Uint32 deadline=SDL_GetTicks()+15000u;
        fprintf(stderr,"Sauti: test trwa maksymalnie 15 sekund.\n");
        float pcm[4096];
        for(;;){
            if((Sint32)(SDL_GetTicks()-deadline)>=0)break;
            /* Keep less than 2 s queued: avoid unbounded memory growth. */
            if(sauti_pcm_output_queued(&output)>decoder.info.sample_rate*2u){
                SDL_Delay(10);
                continue;
            }
            size_t frames=sauti_decoder_read(&decoder,pcm,4096/decoder.info.channels);
            if(!frames)break;
            if(sauti_pcm_output_write(&output,pcm,frames)!=frames){
                fprintf(stderr,"PCM queue failed: %s\n",SDL_GetError());
                ok=0;
                break;
            }
        }
        while(ok && sauti_pcm_output_queued(&output)>0 &&
              (Sint32)(SDL_GetTicks()-deadline)<0)SDL_Delay(20);
    }
    sauti_pcm_output_close(&output);
    sauti_decoder_close(&decoder);
    SDL_Quit();
    free(bytes);
    return ok?0:1;
}
