/* Atari Falcon Digital Tracker modules through libxmp.
 * Optional dependency; unsupported builds reject DTM rather than misdecode.
 * The decoder streams 16-bit stereo PCM from libxmp's replay engine.
 */
#include "sauti_decoder_dtm.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef SAUTI_HAVE_LIBXMP
#include <xmp.h>
#endif

static int probe(const uint8_t *p,size_t n){
 /* D.T. (DTM 2.x) and SONG (DTM 1.x) are Digital Tracker headers.
  * Detection is deliberately narrow: do not claim other .dtm formats. */
 return p&&n>=4&&(!memcmp(p,"D.T.",4)||!memcmp(p,"SONG",4));
}
#ifdef SAUTI_HAVE_LIBXMP
#define DTM_RATE 44100u
#define DTM_BUF_FRAMES 4096u
typedef struct {
 xmp_context context;
 int16_t buffer[DTM_BUF_FRAMES*2];
 size_t buffered,offset;
 uint64_t position,total;
 int ended;
} Dtm;
static int pump(Dtm *s){
 if(s->ended)return 0;
 int rc=xmp_play_buffer(s->context,s->buffer,(int)sizeof(s->buffer),1);
 if(rc!=0){s->ended=1;return 0;}
 s->buffered=DTM_BUF_FRAMES;s->offset=0;
 return 1;
}
static void *open_dtm(const uint8_t *data,size_t n,SautiPcmInfo *info){
 if(!info||!probe(data,n)||n>INT32_MAX)return NULL;
 Dtm *s=calloc(1,sizeof(*s));
 if(!s)return NULL;
 s->context=xmp_create_context();
 if(!s->context){free(s);return NULL;}
 if(xmp_load_module_from_memory(s->context,(void *)data,(long)n)!=0)goto fail;
 struct xmp_module_info mi;
 xmp_get_module_info(s->context,&mi);
 if(!mi.mod||!mi.mod->type[0]||!strstr(mi.mod->type,"Digital Tracker"))goto unload;
 if(xmp_start_player(s->context,DTM_RATE,0)!=0)goto unload;
 struct xmp_frame_info fi;
 xmp_get_frame_info(s->context,&fi);
 /* libxmp's duration is in milliseconds and may vary with loop policy. */
 if(fi.total_time<=0||fi.total_time>30*60*1000)goto end_player;
 s->total=((uint64_t)(unsigned)fi.total_time*DTM_RATE)/1000u;
 if(!s->total)goto end_player;
 info->channels=2;info->sample_rate=DTM_RATE;info->total_frames=s->total;
 return s;
end_player:
 xmp_end_player(s->context);
unload:
 xmp_release_module(s->context);
fail:
 xmp_free_context(s->context);free(s);return NULL;
}
static size_t read_dtm(void *ctx,float *out,size_t frames){
 Dtm *s=ctx;
 if(!s||!out)return 0;
 if((uint64_t)frames>s->total-s->position)
  frames=(size_t)(s->total-s->position);
 size_t done=0;
 while(done<frames){
  if(s->offset==s->buffered&&!pump(s))break;
  size_t take=s->buffered-s->offset;
  if(take>frames-done)take=frames-done;
  for(size_t j=0;j<take*2;j++)
   out[done*2+j]=(float)s->buffer[s->offset*2+j]/32768.0f;
  s->offset+=take;done+=take;s->position+=take;
 }
 return done;
}
static int seek_dtm(void *ctx,uint64_t frame){
 Dtm *s=ctx;
 if(!s||frame>s->total)return 0;
 if(frame==s->position)return 1;
 /* Exact seek: replay from start, discarding frames, not a millisecond
  * approximated seek that would desynchronise the user's timeline. */
 xmp_restart_module(s->context);
 xmp_play_buffer(s->context,NULL,0,1);
 s->buffered=s->offset=0;s->ended=0;s->position=0;
 while(s->position<frame){
  if(s->offset==s->buffered&&!pump(s))return 0;
  size_t skip=s->buffered-s->offset;
  if((uint64_t)skip>frame-s->position)skip=(size_t)(frame-s->position);
  s->offset+=skip;s->position+=skip;
 }
 return 1;
}
static void close_dtm(void *ctx){
 Dtm *s=ctx;if(!s)return;
 xmp_end_player(s->context);xmp_release_module(s->context);
 xmp_free_context(s->context);free(s);
}
#else
static void *open_dtm(const uint8_t *data,size_t n,SautiPcmInfo *info){
 (void)data;(void)n;(void)info;return NULL;
}
static size_t read_dtm(void *s,float *p,size_t n){
 (void)s;(void)p;(void)n;return 0;
}
static int seek_dtm(void *s,uint64_t n){(void)s;(void)n;return 0;}
static void close_dtm(void *s){(void)s;}
#endif
const SautiDecoderOps sauti_decoder_dtm_ops={
 "atari-falcon-digital-tracker",probe,open_dtm,read_dtm,seek_dtm,close_dtm
};
