#include "sauti_decoder_mp3.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MINIMP3_NO_STDIO
#define MINIMP3_NO_SIMD
#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "../../../third_party/minimp3/minimp3_ex.h"

typedef struct {
    mp3dec_ex_t decoder;
    unsigned channels;
} Mp3State;

static int mp3_probe(const uint8_t *data,size_t length){
    if(!data || length<=12)return 0;
    /* Do not allow permissive MP3 detection to claim RIFF/WAVE or Ogg. */
    if((memcmp(data,"RIFF",4)==0 && memcmp(data+8,"WAVE",4)==0) ||
       memcmp(data,"OggS",4)==0)return 0;
    return mp3dec_detect_buf(data,length)==0;
}
static void *mp3_open(const uint8_t *data,size_t length,SautiPcmInfo *info){
    if(!data || !info || !mp3_probe(data,length))return NULL;
    Mp3State *s=calloc(1,sizeof(*s));
    if(!s)return NULL;
    if(mp3dec_ex_open_buf(&s->decoder,data,length,MP3D_SEEK_TO_SAMPLE)!=0 ||
       s->decoder.info.channels<1 || s->decoder.info.channels>2 ||
       s->decoder.info.hz<=0 || s->decoder.samples==0){
        mp3dec_ex_close(&s->decoder);
        free(s);
        return NULL;
    }
    s->channels=(unsigned)s->decoder.info.channels;
    info->channels=s->channels;
    info->sample_rate=(uint32_t)s->decoder.info.hz;
    info->total_frames=s->decoder.samples/s->channels;
    return s;
}
static size_t mp3_read(void *ctx,float *out,size_t frames){
    Mp3State *s=ctx;
    /* Bound scratch allocation, independent of the caller's read size. */
    mp3d_sample_t scratch[MINIMP3_MAX_SAMPLES_PER_FRAME];
    size_t completed=0;
    const size_t batch=MINIMP3_MAX_SAMPLES_PER_FRAME/s->channels;
    while(completed<frames){
        size_t want=frames-completed;
        if(want>batch)want=batch;
        size_t got=mp3dec_ex_read(&s->decoder,scratch,want*s->channels);
        size_t decoded=got/s->channels;
        for(size_t i=0;i<decoded*s->channels;++i)
            out[completed*s->channels+i]=(float)scratch[i]/32768.0f;
        completed+=decoded;
        if(decoded<want)break;
    }
    return completed;
}
static int mp3_seek(void *ctx,uint64_t frame){
    Mp3State *s=ctx;
    if(frame>UINT64_MAX/s->channels)return 0;
    return mp3dec_ex_seek(&s->decoder,frame*s->channels)==0;
}
static void mp3_close(void *ctx){
    Mp3State *s=ctx;
    mp3dec_ex_close(&s->decoder);
    free(s);
}
const SautiDecoderOps sauti_decoder_mp3_ops={
    "mp3-minimp3",mp3_probe,mp3_open,mp3_read,mp3_seek,mp3_close
};
