/* Optional YM2 Mad Max PCM playback and deterministic seeking. */
#include "sauti_decoder.h"
#include "sauti_decoder_ym.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FRAMES=50, TOTAL=44100 };
static void fixture(uint8_t *p,int triggers){
 memset(p,0,4+14*FRAMES);
 memcpy(p,"YM2!",4);
 for(unsigned f=0;f<FRAMES;f++){
  p[4+7*FRAMES+f]=0x3f;
  p[4+13*FRAMES+f]=0xff;
 }
 if(triggers){
  p[4+10*FRAMES+5]=0x81;
  p[4+12*FRAMES+5]=20;
 }
}
static void init(SautiDecoder *d,const uint8_t *ym){
 const SautiDecoderOps *ops[]={&sauti_decoder_ym_ops};
 assert(sauti_decoder_open(d,ops,1,ym,4+14*FRAMES));
}
int main(void){
 uint8_t ym[4+14*FRAMES],none[sizeof(ym)],pcm[4096];
 float *a=calloc(TOTAL,sizeof(float)),*b=calloc(TOTAL,sizeof(float));
 assert(a&&b);
 for(unsigned i=0;i<sizeof(pcm);i++)pcm[i]=(i&1)?255:0;
 const SautiYm2Sample bank[2]={{NULL,0},{pcm,sizeof(pcm)}};
 SautiDecoder d={0},ctrl={0};
 fixture(ym,1);fixture(none,0);
 init(&d,ym);init(&ctrl,none);
 assert(sauti_decoder_ym2_set_bank(&d,bank,2));
 assert(sauti_decoder_read(&d,a,TOTAL)==TOTAL);
 assert(sauti_decoder_read(&ctrl,b,TOTAL)==TOTAL);
 assert(memcmp(a,b,5*882*sizeof(float))==0);
 int different=0;
 for(unsigned i=5*882;i<6*882;i++)if(a[i]!=b[i])different=1;
 assert(different);
 assert(sauti_decoder_seek(&d,5*882+123));
 float replay[1024];
 assert(sauti_decoder_read(&d,replay,1024)==1024);
 assert(memcmp(replay,a+5*882+123,sizeof(replay))==0);
 assert(sauti_decoder_seek(&d,0));
 assert(sauti_decoder_read(&d,replay,1024)==1024);
 assert(memcmp(replay,a,sizeof(replay))==0);
 /* Edge cases: absent sample ID, zero timer, short sample exhaustion,
  * re-trigger, and sample-accurate rewind after both events. */
 sauti_decoder_close(&d);sauti_decoder_close(&ctrl);
 {
  uint8_t fx[sizeof(ym)],short_pcm[3]={255,0,255};
  const SautiYm2Sample tiny_bank[2]={{NULL,0},{short_pcm,3}};
  fixture(fx,0);
  fx[4+10*FRAMES+5]=0x81;fx[4+12*FRAMES+5]=20;
  fx[4+10*FRAMES+6]=0x81;fx[4+12*FRAMES+6]=20;
  fx[4+10*FRAMES+7]=0x85;fx[4+12*FRAMES+7]=20;
  fx[4+10*FRAMES+8]=0x81;fx[4+12*FRAMES+8]=0;
  init(&d,fx);init(&ctrl,none);
  assert(sauti_decoder_ym2_set_bank(&d,tiny_bank,2));
  assert(!sauti_decoder_ym2_set_bank(&d,tiny_bank,129));
  assert(sauti_decoder_read(&d,a,TOTAL)==TOTAL);
  assert(sauti_decoder_read(&ctrl,b,TOTAL)==TOTAL);
  assert(memcmp(a,b,5*882*sizeof(float))==0);
  /* At the trigger sample a short PCM8 bank contributes exactly 127/384.
   * The next output consumes its final byte before exhaustion. */
  const float contribution=(255.0f-128.0f)/(128.0f*3.0f);
  assert(a[5*882]-b[5*882]==contribution);
  assert(a[5*882+1]-b[5*882+1]==contribution);
  assert(a[5*882+2]==b[5*882+2]);
  assert(a[6*882]-b[6*882]==contribution);
  assert(a[6*882+1]-b[6*882+1]==contribution);
  assert(a[6*882+2]==b[6*882+2]);
  /* Unknown index and zero divisor must never start a sample. */
  assert(memcmp(a+7*882,b+7*882,2*882*sizeof(float))==0);
  assert(sauti_decoder_seek(&d,6*882));
  assert(sauti_decoder_read(&d,replay,1024)==1024);
  assert(memcmp(replay,a+6*882,sizeof(replay))==0);
  assert(sauti_decoder_seek(&d,0));
  assert(sauti_decoder_read(&d,replay,1024)==1024);
  assert(memcmp(replay,a,sizeof(replay))==0);
  sauti_decoder_close(&d);sauti_decoder_close(&ctrl);
 }

 /* While a Mad Max sample plays on C, PSG voice C must not leak.
  * After the sample ends, the normal PSG volume must return. */
 {
  uint8_t c_loud[sizeof(ym)],c_quiet[sizeof(ym)];
  fixture(c_loud,1);fixture(c_quiet,1);
  c_loud[4+10*FRAMES+6]=15; /* Volume C during PCM. */
  c_loud[4+10*FRAMES+7]=15; /* Volume C after PCM expires. */
  init(&d,c_loud);init(&ctrl,c_quiet);
  assert(sauti_decoder_ym2_set_bank(&d,bank,2));
  assert(sauti_decoder_ym2_set_bank(&ctrl,bank,2));
  assert(sauti_decoder_read(&d,a,TOTAL)==TOTAL);
  assert(sauti_decoder_read(&ctrl,b,TOTAL)==TOTAL);
  assert(memcmp(a+6*882,b+6*882,300*sizeof(float))==0);
  int returns=0;
  for(unsigned i=7*882;i<8*882;i++)
   if(a[i]!=b[i]){returns=1;break;}
  assert(returns);
  sauti_decoder_close(&d);sauti_decoder_close(&ctrl);
 }
 free(a);free(b);
 puts("PASS: YM2 Mad Max PCM trigger, exact sample, exhaustion, retrigger, invalid ID/timer, voice C override, seek");
 return 0;
}
