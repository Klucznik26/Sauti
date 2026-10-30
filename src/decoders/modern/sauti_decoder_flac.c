/* Portable, memory-backed FLAC -> interleaved float PCM.
 * dr_flac is independently portable and needs no OS codec libraries.
 */
#include "sauti_decoder_flac.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#include "../../../third_party/dr_libs/dr_flac.h"

static int flac_probe(const uint8_t *p,size_t n){
    return p && n>=4 && memcmp(p,"fLaC",4)==0;
}
static void *flac_open(const uint8_t *p,size_t n,SautiPcmInfo *info){
    if(!flac_probe(p,n) || !info)return NULL;
    drflac *f=drflac_open_memory(p,n,NULL);
    if(!f)return NULL;
    if(f->sampleRate==0 || f->channels<1 || f->channels>2 ||
       f->totalPCMFrameCount==0){
        drflac_close(f);
        return NULL;
    }
    info->sample_rate=f->sampleRate;
    info->channels=f->channels;
    info->total_frames=f->totalPCMFrameCount;
    return f;
}
static size_t flac_read(void *ctx,float *samples,size_t frames){
    return (size_t)drflac_read_pcm_frames_f32((drflac*)ctx,(drflac_uint64)frames,samples);
}
static int flac_seek(void *ctx,uint64_t frame){
    return drflac_seek_to_pcm_frame((drflac*)ctx,(drflac_uint64)frame)!=DRFLAC_FALSE;
}
static void flac_close(void *ctx){drflac_close((drflac*)ctx);}
const SautiDecoderOps sauti_decoder_flac_ops={
    "flac-drflac",flac_probe,flac_open,flac_read,flac_seek,flac_close
};
