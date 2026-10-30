/* Optional SNDH replay adapter for AtariAudio 1.26 (MIT).
 * Builds only when the caller explicitly enables SAUTI_HAVE_ATARIAUDIO.
 * AtariAudio supplies the 68000 and Atari sound hardware emulation.
 * No AtariAudio code is copied into Sauti's source tree.
 */
#include "sauti_decoder_sndh.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#ifdef SAUTI_HAVE_ATARIAUDIO
#include "AtariAudioRenderer.h"
struct Sndh {
    AtariAudioRenderer *renderer;
    uint64_t position, total;
    int subsong;
};
static void *open_sndh(const uint8_t *bytes,size_t size,SautiPcmInfo *info){
    if(!info||!bytes||size<16||size>UINT32_MAX)return nullptr;
    AtariAudioRenderer *renderer=AtariAudioRenderer::Create(bytes,(uint32_t)size,44100);
    if(!renderer)return nullptr;
    const auto &song=renderer->GetSongInfo();
    if(song.fileType!=AtariAudioRenderer::eFileType::eSndh ||
       song.subsongCount<1 || song.subsongCount>128){
        AtariAudioRenderer::Destroy(renderer);return nullptr;
    }
    int subsong=song.defaultSubsong;
    if(subsong<1||subsong>song.subsongCount)subsong=1;
    if(!renderer->InitSubSong(subsong)){
        AtariAudioRenderer::Destroy(renderer);return nullptr;
    }
    Sndh *state=(Sndh*)calloc(1,sizeof(Sndh));
    if(!state){AtariAudioRenderer::Destroy(renderer);return nullptr;}
    state->renderer=renderer;state->subsong=subsong;
    uint32_t duration=renderer->GetSubsongDurationSample(subsong);
    /* No duration metadata: use an explicit 3-minute playback horizon,
     * not an unbounded "infinite" stream. */
    state->total=duration?duration:180u*44100u;
    info->sample_rate=44100;info->channels=1;info->total_frames=state->total;
    return state;
}
static size_t read_sndh(void *ctx,float *out,size_t frames){
    Sndh *s=(Sndh*)ctx;
    if(!s||!out)return 0;
    uint64_t remain=s->total-s->position;
    if(frames>remain)frames=(size_t)remain;
    size_t done=0;
    int16_t scratch[2048];
    while(done<frames){
        uint32_t take=(uint32_t)((frames-done)>2048?2048:(frames-done));
        s->renderer->AudioRender(scratch,take);
        for(uint32_t i=0;i<take;i++)out[done+i]=(float)scratch[i]/32768.0f;
        done+=take;
    }
    s->position+=done;return done;
}
static int seek_sndh(void *ctx,uint64_t pos){
    Sndh *s=(Sndh*)ctx;
    if(!s||pos>s->total)return 0;
    if(pos<s->position){
        if(!s->renderer->InitSubSong(s->subsong))return 0;
        s->position=0;
    }
    while(s->position<pos){
        uint32_t step=(uint32_t)((pos-s->position)>65536?65536:(pos-s->position));
        s->renderer->FastForward(step);
        s->position+=step;
    }
    return 1;
}
static void close_sndh(void *ctx){
    Sndh *s=(Sndh*)ctx;
    if(!s)return;
    AtariAudioRenderer::Destroy(s->renderer);free(s);
}
#else
static void *open_sndh(const uint8_t*,size_t,SautiPcmInfo*){return nullptr;}
static size_t read_sndh(void*,float*,size_t){return 0;}
static int seek_sndh(void*,uint64_t){return 0;}
static void close_sndh(void*){}
#endif
static int probe_sndh(const uint8_t *p,size_t n){
    if(!p||n<16)return 0;
    /* Most SNDH files begin with three 68k JMP/BRA entry stubs;
     * defer full validation to AtariAudio. */
    const size_t max=n<512?n:512;
    for(size_t i=6;i+4<=max;i+=2)
        if(memcmp(p+i,"SNDH",4)==0)return 1;
    return 0;
}
extern "C" const SautiDecoderOps sauti_decoder_sndh_ops={
    "atari-sndh-atariaudio",probe_sndh,open_sndh,read_sndh,seek_sndh,close_sndh
};
