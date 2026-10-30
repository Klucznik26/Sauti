/* Memory-backed WavPack lossless PCM decoder via optional libwavpack.
 * The first stage supports 1-2 PCM channels including IEEE float.
 * DSD and hybrid/lossy streams are intentionally rejected. */
#include "sauti_decoder_wavpack.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#ifdef SAUTI_HAVE_WAVPACK
#include <wavpack/wavpack.h>
typedef struct {const uint8_t *data;size_t length,position;} WvMemory;
typedef struct {WvMemory stream;WavpackContext *context;int channels,bits,floating;int32_t *tmp;} WvState;
static int32_t wv_mem_read(void *id,void *dest,int32_t amount){
 WvMemory *m=id;if(amount<=0)return 0;
 size_t n=(size_t)amount;if(n>m->length-m->position)n=m->length-m->position;
 if(n)memcpy(dest,m->data+m->position,n);
 m->position+=n;return (int32_t)n;
}
static int32_t wv_mem_write(void *id,void *data,int32_t n){(void)id;(void)data;(void)n;return 0;}
static int64_t wv_mem_pos(void *id){return (int64_t)((WvMemory*)id)->position;}
static int wv_mem_abs(void *id,int64_t p){
 WvMemory *m=id;if(p<0||(uint64_t)p>m->length)return -1;
 m->position=(size_t)p;return 0;
}
static int wv_mem_rel(void *id,int64_t d,int mode){
 WvMemory *m=id;
 int64_t base=mode==SEEK_END?(int64_t)m->length:mode==SEEK_CUR?(int64_t)m->position:0;
 if(mode!=SEEK_SET&&mode!=SEEK_CUR&&mode!=SEEK_END)return -1;
 if((d>0&&base>INT64_MAX-d)||(d<0&&base<INT64_MIN-d))return -1;
 return wv_mem_abs(id,base+d);
}
static int wv_mem_push(void *id,int ch){WvMemory *m=id;if(!m->position)return EOF;--m->position;return ch;}
static int64_t wv_mem_len(void *id){return (int64_t)((WvMemory*)id)->length;}
static int wv_mem_seekable(void *id){(void)id;return 1;}
static int wv_mem_truncate(void *id){(void)id;return -1;}
static int wv_mem_close(void *id){(void)id;return 0;}
static WavpackStreamReader64 wv_reader={
 wv_mem_read,wv_mem_write,wv_mem_pos,wv_mem_abs,wv_mem_rel,wv_mem_push,
 wv_mem_len,wv_mem_seekable,wv_mem_truncate,wv_mem_close
};
static void wv_close(void *ptr){
 WvState *s=ptr;if(!s)return;
 if(s->context)WavpackCloseFile(s->context);
 free(s->tmp);free(s);
}
static void *wv_open(const uint8_t *bytes,size_t size,SautiPcmInfo *info){
 if(!bytes||size<32||size>INT64_MAX||!info)return NULL;
 WvState *s=calloc(1,sizeof(*s));if(!s)return NULL;
 s->stream.data=bytes;s->stream.length=size;
 char err[128]={0};
 s->context=WavpackOpenFileInputEx64(&wv_reader,&s->stream,NULL,err,OPEN_NORMALIZE,0);
 if(!s->context){wv_close(s);return NULL;}
 s->channels=WavpackGetNumChannels(s->context);
 s->bits=WavpackGetBitsPerSample(s->context);
 s->floating=(WavpackGetMode(s->context)&MODE_FLOAT)!=0;
 int64_t count=WavpackGetNumSamples64(s->context);
 uint32_t rate=WavpackGetSampleRate(s->context);
 if(s->channels<1||s->channels>2||s->bits<1||s->bits>32||
    count<=0||!rate||
    !(WavpackGetMode(s->context)&MODE_LOSSLESS)||
    (WavpackGetQualifyMode(s->context)&QMODE_DSD_AUDIO)){
    wv_close(s);return NULL;
 }
 s->tmp=malloc(4096u*(size_t)s->channels*sizeof(int32_t));
 if(!s->tmp){wv_close(s);return NULL;}
 info->sample_rate=rate;info->channels=(uint32_t)s->channels;info->total_frames=(uint64_t)count;
 return s;
}
static size_t wv_read(void *ctx,float *out,size_t frames){
 WvState *s=ctx;if(!s||!out)return 0;
 size_t done=0;
 while(done<frames){
  uint32_t want=(uint32_t)((frames-done)>4096?4096:(frames-done));
  uint32_t got=WavpackUnpackSamples(s->context,s->tmp,want);
  if(!got)break;
  float scale=s->bits==32?2147483648.0f:(float)(1u<<(s->bits-1));
  size_t samples=(size_t)got*(size_t)s->channels;
  for(size_t i=0;i<samples;++i){
   if(s->floating){
    float value;uint32_t pattern=(uint32_t)s->tmp[i];
    memcpy(&value,&pattern,sizeof(value));out[done*s->channels+i]=value;
   }else out[done*s->channels+i]=(float)s->tmp[i]/scale;
  }
  done+=got;
 }
 return done;
}
static int wv_seek(void *ptr,uint64_t frame){
 WvState *s=ptr;return s&&frame<=INT64_MAX&&WavpackSeekSample64(s->context,(int64_t)frame);
}
#else
static void *wv_open(const uint8_t *b,size_t n,SautiPcmInfo *i){(void)b;(void)n;(void)i;return NULL;}
static size_t wv_read(void *s,float *o,size_t n){(void)s;(void)o;(void)n;return 0;}
static int wv_seek(void *s,uint64_t p){(void)s;(void)p;return 0;}
static void wv_close(void *s){(void)s;}
#endif
static int wv_probe(const uint8_t *b,size_t n){return b&&n>=4&&memcmp(b,"wvpk",4)==0;}
const SautiDecoderOps sauti_decoder_wavpack_ops={
 "wavpack-lossless",wv_probe,wv_open,wv_read,wv_seek,wv_close
};
