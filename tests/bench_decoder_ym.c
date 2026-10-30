/* Reproducible YM decoder seek benchmark. No external music files needed.
 * Measures CPU time, not wall time; performance differs across machines.
 * A PCM checksum guards against compiler removing the decode path.
 */
#include "sauti_decoder.h"
#include "sauti_decoder_ym.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { FPS=50, SONG_SECONDS=120, FRAMES=FPS*SONG_SECONDS,
       PCM_RATE=44100, CHUNK=1024 };
static uint8_t song[4+14*FRAMES];
static volatile double sink;
static double elapsed(clock_t start){return (double)(clock()-start)/CLOCKS_PER_SEC;}
static int use_seek(SautiDecoder *d,uint64_t offset,double *seconds){
 float buffer[CHUNK];
 clock_t start=clock();
 if(!sauti_decoder_seek(d,offset))return 0;
 if(sauti_decoder_read(d,buffer,CHUNK)!=CHUNK)return 0;
 *seconds=elapsed(start);
 for(size_t j=0;j<CHUNK;j++)sink+=(double)buffer[j];
 return 1;
}
int main(void){
 memcpy(song,"YM3!",4);
 for(unsigned i=0;i<FRAMES;i++){
  song[4+0*FRAMES+i]=(uint8_t)(120+i%101);
  song[4+2*FRAMES+i]=(uint8_t)(37+i%91);
  song[4+4*FRAMES+i]=(uint8_t)(71+i%81);
  song[4+7*FRAMES+i]=0x38;
  song[4+8*FRAMES+i]=15;
  song[4+9*FRAMES+i]=12;
  song[4+10*FRAMES+i]=11;
  song[4+13*FRAMES+i]=0xff;
 }
 const SautiDecoderOps *registry[]={&sauti_decoder_ym_ops};
 SautiDecoder decoder={0};
 if(!sauti_decoder_open(&decoder,registry,1,song,sizeof(song))){
  fputs("Cannot open synthetic YM3 fixture\n",stderr);return 1;
 }
 float buffer[CHUNK];
 /* A cold seek must reconstruct state; a warm seek uses checkpoints. */
 double cold=0,warm=0,backward=0;
 const uint64_t target=110u*PCM_RATE;
 if(!use_seek(&decoder,target,&cold)){fputs("cold seek failed\n",stderr);return 1;}
 if(!use_seek(&decoder,target,&warm)){fputs("warm seek failed\n",stderr);return 1;}
 if(!use_seek(&decoder,105u*PCM_RATE,&backward)){fputs("backward seek failed\n",stderr);return 1;}
 /* Exercise normal playback and automatic checkpoints separately. */
 if(!sauti_decoder_seek(&decoder,0)){fputs("rewind failed\n",stderr);return 1;}
 clock_t start=clock();
 uint64_t remaining=30u*PCM_RATE;
 while(remaining){
  size_t n=remaining<CHUNK?(size_t)remaining:CHUNK;
  if(sauti_decoder_read(&decoder,buffer,n)!=n){fputs("stream failed\n",stderr);return 1;}
  sink+=(double)buffer[0];
  remaining-=n;
 }
 double streaming=elapsed(start);
 double replay=0;
 if(!use_seek(&decoder,20u*PCM_RATE,&replay)){fputs("cached rewind failed\n",stderr);return 1;}
 printf("YM3 synthetic 120s / 44.1kHz; CPU seconds (lower is better)\n");
 printf("Cold seek to 110s:  %.6f\n",cold);
 printf("Warm seek to 110s:  %.6f\n",warm);
 printf("Backward seek 105s: %.6f\n",backward);
 printf("Stream 30s:         %.6f\n",streaming);
 printf("Seek into 20s:      %.6f\n",replay);
 printf("PCM checksum: %.8f\n",(double)sink);
 sauti_decoder_close(&decoder);
 return 0;
}
