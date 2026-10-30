/* YM parser negative and mutation regression. Safe inputs only, no external corpus. */
#include "sauti_decoder.h"
#include "sauti_decoder_ym.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void be32(uint8_t *p,uint32_t n){p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
static int open_ym(const uint8_t *p,size_t n){
 SautiDecoder d={0};
 const SautiDecoderOps *ops[]={&sauti_decoder_ym_ops};
 int ok=sauti_decoder_open(&d,ops,1,p,n);
 if(ok){
  float pcm[128];
  assert(d.info.channels==1 && d.info.sample_rate==44100);
  assert(sauti_decoder_read(&d,pcm,128)<=128);
  assert(sauti_decoder_seek(&d,0));
  sauti_decoder_close(&d);
 }
 return ok;
}
int main(void){
 enum { N=34+3+16*2+4 };
 uint8_t valid[N]={0},bad[N];
 memcpy(valid,"YM6!LeOnArD!",12);
 be32(valid+12,2);be32(valid+16,1);be32(valid+22,2000000);
 valid[27]=50;
 for(unsigned r=0;r<2;r++){
  valid[37+7*2+r]=0x3e;
  valid[37+8*2+r]=15;
  valid[37+13*2+r]=0xff;
 }
 memcpy(valid+N-4,"End!",4);
 assert(open_ym(valid,N));
 /* Legacy YM header boundary checks, including YM3b's trailing loop. */
 {
  uint8_t legacy[4+14*2+4]={0};
  memcpy(legacy,"YM3b",4);
  legacy[4+7*2]=0x3e;legacy[4+7*2+1]=0x3e;
  legacy[4+8*2]=15;legacy[4+8*2+1]=15;
  legacy[4+13*2]=0xff;legacy[4+13*2+1]=0xff;
  be32(legacy+sizeof(legacy)-4,1);
  assert(open_ym(legacy,sizeof(legacy)));
  for(size_t n=0;n<sizeof(legacy);n++)
   assert(!open_ym(legacy,n));
  be32(legacy+sizeof(legacy)-4,2);
  assert(!open_ym(legacy,sizeof(legacy)));
  memcpy(legacy,"YM3!",4);
  assert(open_ym(legacy,sizeof(legacy)-4));
  assert(!open_ym(legacy,4));
  memcpy(legacy,"YM2!",4);
  assert(open_ym(legacy,sizeof(legacy)-4));
 }
 unsigned rejections=0,accepted=0;
 for(size_t length=0;length<N;length++){
  if(!open_ym(valid,length))rejections++;
 }
 for(size_t offset=0;offset<N;offset++){
  for(unsigned value=0;value<256;value++){
   memcpy(bad,valid,N);
   bad[offset]=(uint8_t)value;
   if(open_ym(bad,N))accepted++;
   else rejections++;
  }
 }
 /* Explicit structural rejection cases: oversized frame count, metadata
  * termination, invalid attributes, and unsupported MFP prescaler. */
 memcpy(bad,valid,N);be32(bad+12,1000001);assert(!open_ym(bad,N));
 memcpy(bad,valid,N);be32(bad+16,0x80000000u);assert(!open_ym(bad,N));
 memcpy(bad,valid,N);bad[34]=bad[35]=bad[36]='X';assert(!open_ym(bad,N));
 memcpy(bad,valid,N);bad[37+1*2]=0x10;bad[37+14*2]=30;
 assert(!open_ym(bad,N)); /* prescaler 0 */
 /* Deterministic pseudorandom malformed payloads of varied length. */
 uint32_t state=0xA70A2149u;
 for(unsigned k=0;k<2000;k++){
  size_t length=(size_t)(k%N);
  for(size_t i=0;i<length;i++){
   state^=state<<13;state^=state>>17;state^=state<<5;
   bad[i]=(uint8_t)state;
  }
  (void)open_ym(bad,length);
 }
 printf("PASS: YM truncation, %u accepted / %u rejected mutations; deterministic malformed corpus\n",
        accepted,rejections);
 return 0;
}
