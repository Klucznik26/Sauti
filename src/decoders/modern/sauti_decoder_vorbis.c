#include "sauti_decoder_vorbis.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#define STB_VORBIS_NO_STDIO
#include "../../../third_party/stb/stb_vorbis.c"

typedef struct {
    stb_vorbis *stream;
    unsigned channels;
} VorbisState;
static int vorbis_probe(const uint8_t *p,size_t n){
    return p && n>=4 && memcmp(p,"OggS",4)==0;
}
static void *vorbis_open(const uint8_t *p,size_t n,SautiPcmInfo *info){
    if(!vorbis_probe(p,n) || n>(size_t)INT_MAX || !info)return NULL;
    int error=0;
    stb_vorbis *stream=stb_vorbis_open_memory(p,(int)n,&error,NULL);
    if(!stream)return NULL;
    stb_vorbis_info fmt=stb_vorbis_get_info(stream);
    if(fmt.channels<1 || fmt.channels>2 || fmt.sample_rate==0){
        stb_vorbis_close(stream);
        return NULL;
    }
    VorbisState *state=malloc(sizeof(*state));
    if(!state){stb_vorbis_close(stream);return NULL;}
    state->stream=stream;state->channels=(unsigned)fmt.channels;
    info->channels=state->channels;
    info->sample_rate=fmt.sample_rate;
    info->total_frames=stb_vorbis_stream_length_in_samples(stream);
    return state;
}
static size_t vorbis_read(void *ctx,float *output,size_t frames){
    VorbisState *state=ctx;
    size_t completed=0;
    while(completed<frames){
        size_t todo=frames-completed;
        if(todo>(size_t)INT_MAX/state->channels)
            todo=(size_t)INT_MAX/state->channels;
        int read=stb_vorbis_get_samples_float_interleaved(
            state->stream,(int)state->channels,
            output+completed*state->channels,(int)(todo*state->channels));
        if(read<=0)break;
        completed+=(size_t)read;
        if((size_t)read<todo)break;
    }
    return completed;
}
static int vorbis_seek(void *ctx,uint64_t frame){
    VorbisState *state=ctx;
    if(frame>UINT_MAX)return 0;
    return stb_vorbis_seek(state->stream,(unsigned int)frame);
}
static void vorbis_close(void *ctx){
    VorbisState *state=ctx;
    stb_vorbis_close(state->stream);
    free(state);
}
const SautiDecoderOps sauti_decoder_vorbis_ops={
    "ogg-vorbis-stb",vorbis_probe,vorbis_open,vorbis_read,
    vorbis_seek,vorbis_close
};
