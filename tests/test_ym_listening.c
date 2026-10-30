/* Atari ST YM playback integration regression for Classic/Reference/Pre-tick.
 * Checks one uninterrupted render against mixed-size reads, forward/backward
 * seeking, YM3b loop metadata and replay from the loop target.
 */
#include "sauti_decoder.h"
#include "sauti_decoder_ym.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAMES 150u
#define RATE 44100u
#define TOTAL (FRAMES*RATE/50u)
static void be32(unsigned char *p,uint32_t v){
 p[0]=(unsigned char)(v>>24);p[1]=(unsigned char)(v>>16);
 p[2]=(unsigned char)(v>>8);p[3]=(unsigned char)v;
}
int main(void){
 const size_t size=4+14*FRAMES+4;
 uint8_t *song=calloc(size,1);
 float *baseline=malloc(TOTAL*sizeof(float));
 float *replayed=malloc(TOTAL*sizeof(float));
 assert(song&&baseline&&replayed);
 memcpy(song,"YM3b",4);
 for(unsigned f=0;f<FRAMES;f++){
  song[4+0*FRAMES+f]=(uint8_t)(150+f%90);
  song[4+1*FRAMES+f]=0;
  song[4+6*FRAMES+f]=3;
  song[4+7*FRAMES+f]=0x3e;
  song[4+8*FRAMES+f]=0x10;
  song[4+11*FRAMES+f]=50;
  song[4+12*FRAMES+f]=0;
  song[4+13*FRAMES+f]=(f==0||f==75)?0x0c:0xff;
 }
 be32(song+size-4,50);
 const SautiDecoderOps *registry[]={&sauti_decoder_ym_ops};
 SautiDecoder d={0};
 assert(sauti_decoder_open(&d,registry,1,song,size));
 assert(d.info.total_frames==TOTAL);
 uint64_t loop=0;
 assert(sauti_decoder_ym_loop_start(&d,&loop)&&loop==50u*RATE/50u);
 assert(sauti_decoder_read(&d,baseline,TOTAL)==TOTAL);
 assert(sauti_decoder_read(&d,replayed,1)==0);
 /* Explicitly replay past multiple VBL boundaries with varied request sizes. */
 assert(sauti_decoder_seek(&d,0));
 size_t pos=0;
 while(pos<TOTAL){
  size_t want=1+((pos*37u+17u)%4096u);
  if(want>TOTAL-pos)want=TOTAL-pos;
  assert(sauti_decoder_read(&d,replayed+pos,want)==want);
  pos+=want;
 }
 assert(memcmp(replayed,baseline,TOTAL*sizeof(float))==0);
 const unsigned seeks[]={1,881,882,44099,44100,66149,66150,66151,
                         88200,100,132299,22050,0,44101,132300};
 for(size_t k=0;k<sizeof(seeks)/sizeof(seeks[0]);k++){
  unsigned at=seeks[k];
  assert(sauti_decoder_seek(&d,at));
  float window[96]={0};
  size_t count=TOTAL-at<96?TOTAL-at:96;
  assert(sauti_decoder_read(&d,window,96)==count);
  assert(memcmp(window,baseline+at,count*sizeof(float))==0);
 }
 assert(sauti_decoder_seek(&d,TOTAL));
 assert(sauti_decoder_read(&d,replayed,1)==0);
 assert(sauti_decoder_seek(&d,loop));
 assert(sauti_decoder_read(&d,replayed,4096)==4096);
 assert(memcmp(replayed,baseline+loop,4096*sizeof(float))==0);
 assert(!sauti_decoder_seek(&d,TOTAL+1u));
 sauti_decoder_close(&d);
 /* YM5/YM6 header loop points, including default zero, must be honored.
  * The SDL player uses this metadata to repeat instead of stopping. */
 for(unsigned version=5;version<=6;version++){
  uint8_t ext[34+3+16*3+4]={0};
  memcpy(ext,version==5?"YM5!LeOnArD!":"YM6!LeOnArD!",12);
  be32(ext+12,3);be32(ext+16,0);be32(ext+22,2000000);
  ext[27]=50;
  for(unsigned f=0;f<3;f++){
   ext[37+f*16+0]=(uint8_t)(100+f*10);
   ext[37+f*16+7]=0x3e;
   ext[37+f*16+8]=15;
   ext[37+f*16+13]=0xff;
  }
  memcpy(ext+37+48,"End!",4);
  for(unsigned loop_index=0;loop_index<3;loop_index+=2){
   be32(ext+28,loop_index);
   assert(sauti_decoder_open(&d,registry,1,ext,sizeof(ext)));
   assert(sauti_decoder_ym_loop_start(&d,&loop));
   assert(loop==(uint64_t)loop_index*RATE/50u);
   float full[3*RATE/50u],resume[3*RATE/50u];
   assert(sauti_decoder_read(&d,full,3*RATE/50u)==3*RATE/50u);
   assert(sauti_decoder_seek(&d,loop));
   size_t remaining=3*RATE/50u-(size_t)loop;
   assert(sauti_decoder_read(&d,resume,remaining)==remaining);
   assert(memcmp(resume,full+loop,remaining*sizeof(float))==0);
   sauti_decoder_close(&d);
  }
  /* Nonzero R14/R15 values without an effect are inert and legal.
   * High bits in R1/R3 alone must not activate an invalid timer. */
  be32(ext+28,0);
  ext[37+14]=0x7f;ext[37+15]=0x91;
  ext[37+1]=0xc0;ext[37+3]=0x80;
  assert(sauti_decoder_open(&d,registry,1,ext,sizeof(ext)));
  assert(sauti_decoder_read(&d,replayed,64)==64);
  sauti_decoder_close(&d);
  be32(ext+28,3);
  assert(!sauti_decoder_open(&d,registry,1,ext,sizeof(ext)));
 }
 free(replayed);free(baseline);free(song);
 puts("PASS: YM3b/YM5/YM6 loops, VBL restart, chunk-independent playback, rewind/seek");
 return 0;
}
