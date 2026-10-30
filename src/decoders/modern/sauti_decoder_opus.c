/* Opusfile adapter: platform-neutral in-memory decoder.
 * libopusfile/libopus/libogg may be bundled for AyoOS/Windows later.
 * Without SAUTI_HAVE_OPUSFILE, this decoder is intentionally unavailable.
 */
#include "sauti_decoder_opus.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef SAUTI_HAVE_OPUSFILE
#include <opus/opusfile.h>
#endif

static int opus_probe(const uint8_t *p,size_t n){
    /* Ogg packet streams begin with "OggS"; OpusHead is normally in page 0.
     * Do not claim generic Vorbis Ogg content.
     */
    if(!p || n<36 || memcmp(p,"OggS",4)!=0)return 0;
    size_t limit=n<4096?n:4096;
    for(size_t i=0;i+8<=limit;++i)
        if(memcmp(p+i,"OpusHead",8)==0)return 1;
    return 0;
}
#ifdef SAUTI_HAVE_OPUSFILE
typedef struct {OggOpusFile *file;} OpusState;
static void *opus_open(const uint8_t *p,size_t n,SautiPcmInfo *info){
    if(!opus_probe(p,n) || n>INT32_MAX || !info)return NULL;
    int error=0;
    OggOpusFile *file=op_open_memory(p,n,&error);
    if(!file)return NULL;
    opus_int64 count=op_pcm_total(file,-1);
    if(count<=0){op_free(file);return NULL;}
    OpusState *s=malloc(sizeof(*s));
    if(!s){op_free(file);return NULL;}
    s->file=file;
    info->sample_rate=48000;
    info->channels=2; /* opusfile stereo downmix */
    info->total_frames=(uint64_t)count;
    return s;
}
static size_t opus_read(void *ctx,float *out,size_t frames){
    OpusState *s=ctx;
    size_t total=0;
    while(total<frames){
        size_t part=frames-total;
        if(part>5760)part=5760;
        int got=op_read_float_stereo(s->file,out+2*total,(int)(part*2));
        if(got<=0)break;
        total+=(size_t)got;
    }
    return total;
}
static int opus_seek(void *ctx,uint64_t frame){
    OpusState *s=ctx;
    if(frame>INT64_MAX)return 0;
    return op_pcm_seek(s->file,(opus_int64)frame)==0;
}
static void opus_close(void *ctx){
    OpusState *s=ctx;
    op_free(s->file);
    free(s);
}
#else
static void *opus_open(const uint8_t *p,size_t n,SautiPcmInfo *info){
    (void)p;(void)n;(void)info;return NULL;
}
static size_t opus_read(void *ctx,float *out,size_t frames){
    (void)ctx;(void)out;(void)frames;return 0;
}
static int opus_seek(void *ctx,uint64_t frame){
    (void)ctx;(void)frame;return 0;
}
static void opus_close(void *ctx){(void)ctx;}
#endif
const SautiDecoderOps sauti_decoder_opus_ops={
    "opus-opusfile",opus_probe,opus_open,opus_read,opus_seek,opus_close
};
