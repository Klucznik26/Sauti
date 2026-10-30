#include "sauti_decoder.h"
#include "sauti_decoder_ym.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
extern float sauti_ym_drum_sample_asm(const uint8_t *,uint32_t);
extern uint32_t sauti_ym_noise_step_asm(uint32_t *,uint32_t *,uint32_t,uint32_t);
typedef struct {uint32_t phase,index,attack,alternate,hold,holding;} YmEnvelope;
extern void sauti_ym_env_init_asm(YmEnvelope *,uint32_t);
extern uint32_t sauti_ym_env_step_asm(YmEnvelope *,uint32_t,uint32_t);
extern void sauti_ym_tone_step_asm(const uint8_t *,uint32_t *,uint32_t,float *,uint32_t,uint32_t);
static void w32(unsigned char *p,unsigned v){p[0]=(unsigned char)(v>>24);p[1]=(unsigned char)(v>>16);p[2]=(unsigned char)(v>>8);p[3]=(unsigned char)v;}
int main(void){
 unsigned char b[34+3+16*2+4]={0};
 memcpy(b,"YM6!LeOnArD!",12);w32(b+12,2);w32(b+16,0);
 w32(b+22,2000000);b[27]=50;
 /* 3 empty metadata strings; frame 0/1 */
 b[34+3+0]=0x80;b[34+3+1]=0x01;
 b[34+3+7]=0x3e; /* disable B,C and all noise */
 b[34+3+8]=15;
 b[34+3+16+0]=0x80;b[34+3+16+1]=0x01;
 b[34+3+16+7]=0x3e;b[34+3+16+8]=15;
 memcpy(b+34+3+32,"End!",4);
 const SautiDecoderOps *registry[]={&sauti_decoder_ym_ops};
 SautiDecoder d={0};float out[882]={0};
 assert(sauti_decoder_open(&d,registry,1,b,sizeof(b)));
 assert(d.info.sample_rate==44100&&d.info.channels==1&&d.info.total_frames==1764);
 assert(sauti_decoder_read(&d,out,882)==882);
 float lo=1.0f,hi=-1.0f;
 for(size_t i=0;i<882;i++){if(out[i]<lo)lo=out[i];if(out[i]>hi)hi=out[i];}
 assert(lo<0.0f&&hi>0.0f);
 assert(sauti_decoder_seek(&d,100));
 float at100[64]={0};
 assert(sauti_decoder_read(&d,at100,64)==64);
 assert(memcmp(at100,out+100,sizeof(at100))==0);
 assert(!sauti_decoder_seek(&d,1765));
 assert(sauti_decoder_seek(&d,0));
 sauti_decoder_close(&d);
 /* Free-running 32-bit tone phase survives period changes. The
  * reference increment is (master_clock_div8_q16 << 15)/period. */
 {
  uint8_t regs[16]={0};
  uint32_t phases[3]={0};
  uint32_t reference_phase=0;
  const uint32_t master_step=(uint32_t)(((uint64_t)2000000*65536)/(8*44100));
  regs[7]=0x3e;regs[8]=15;
  for(unsigned i=0;i<4096;i++){
   uint32_t period=(i<1024)?250u:(i<3072?375u:250u);
   regs[0]=(uint8_t)period;regs[1]=(uint8_t)(period>>8);
   float sample=0.0f;
   sauti_ym_tone_step_asm(regs,phases,master_step,&sample,1,0);
   reference_phase+=(uint32_t)(((uint64_t)master_step<<15)/period);
   assert(phases[0]==reference_phase);
  }
 }
 /* ST-Sound reference: feedback bit0 ^ bit2, separate noise output
  * toggles only on zero feedback. Verify 65536 ticks, including LFSR wrap. */
 uint32_t phase=0,rng=1,rack=1,output=1;
 for(unsigned i=0;i<65536;i++){
  unsigned feedback=(rack^(rack>>2))&1u;
  rack=(rack>>1)|(feedback<<16);
  if(!feedback)output^=1u;
  unsigned bit=sauti_ym_noise_step_asm(&phase,&rng,65536,1);
  assert(bit==output);
  assert((rng&0x1ffffu)==rack);
  assert(((rng>>17)&1u)==(output^1u));
  assert(phase==0);
 }
 /* Noise-only channel A: tone disabled and noise enabled, B/C muted. */
 b[34+3+7]=0x37;b[34+3+6]=1;
 b[34+3+16+7]=0x37;b[34+3+16+6]=1;
 assert(sauti_decoder_open(&d,registry,1,b,sizeof(b)));
 assert(sauti_decoder_read(&d,out,882)==882);
 lo=1.0f;hi=-1.0f;
 for(size_t i=0;i<882;i++){if(out[i]<lo)lo=out[i];if(out[i]>hi)hi=out[i];}
 assert(lo<0.0f&&hi>0.0f);
 assert(sauti_decoder_seek(&d,0));
 float first[64]={0},again[64]={0};
 assert(sauti_decoder_read(&d,first,64)==64);
 assert(sauti_decoder_seek(&d,0));
 assert(sauti_decoder_read(&d,again,64)==64);
 assert(memcmp(first,again,sizeof(first))==0);
 sauti_decoder_close(&d);
 b[34+3+7]=0x3e;b[34+3+16+7]=0x3e;

 b[34+3+7]=0x3e;b[34+3+8]=0x1f;
 assert(sauti_decoder_open(&d,registry,1,b,sizeof(b))); /* envelope now supported */
 assert(sauti_decoder_read(&d,out,882)==882);
 sauti_decoder_close(&d);
 /* Inert R15 data is legal when no timer effect is enabled. */
 b[34+3+8]=15;b[34+3+15]=1;
 assert(sauti_decoder_open(&d,registry,1,b,sizeof(b)));
 sauti_decoder_close(&d);
 /* With a timer effect enabled, an invalid prescaler must be rejected. */
 b[34+3+1]=0x10;
 assert(!sauti_decoder_open(&d,registry,1,b,sizeof(b)));
 b[34+3+1]=0;
 b[34+3+15]=0;b[34+3+32]='X';assert(!sauti_decoder_open(&d,registry,1,b,sizeof(b)));
 /* YM6 with one embedded digi-drum sample but no active effect commands.
  * Verify the sample block is skipped before metadata and register planes. */
 {
  uint8_t song[34+4+3+3+16*2+4]={0};
  memcpy(song,"YM6!LeOnArD!",12);
  w32(song+12,2);w32(song+16,7);song[21]=1;
  w32(song+22,2000000);song[27]=50;
  w32(song+34,3);song[38]=0x00;song[39]=0x7f;song[40]=0xff;
  const size_t base=44;
  for(unsigned f=0;f<2;f++){
   song[base+7*2+f]=0x3e;
   song[base+8*2+f]=15;
   song[base+13*2+f]=0xff;
  }
  memcpy(song+base+32,"End!",4);
  assert(sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  assert(d.info.total_frames==1764);
  assert(sauti_decoder_read(&d,out,882)==882);
  sauti_decoder_close(&d);
  w32(song+34,1000); /* truncated sample data must be rejected */
  assert(!sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  w32(song+34,3);
  song[base+14*2]=1; /* inert port byte is valid without an effect */
  assert(sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  sauti_decoder_close(&d);
  song[base+1*2]=0x50; /* activate a drum: invalid prescaler/timer */
  assert(!sauti_decoder_open(&d,registry,1,song,sizeof(song)));
 }
 /* Full YM6 digidrum event through decoded register frames. */
 {
  uint8_t song[34+4+4+3+16*2+4]={0};
  memcpy(song,"YM6!LeOnArD!",12);
  w32(song+12,2);w32(song+16,1);song[21]=1;
  w32(song+22,2000000);song[27]=50;
  w32(song+34,4);song[38]=255;song[39]=0;song[40]=255;song[41]=0;
  const size_t base=45;
  song[base+1*2]=0x50; /* digidrum on channel A */
  song[base+6*2]=0x20; /* prescaler /4 */
  song[base+7*2]=0x3f;
  song[base+14*2]=28; /* ~21943 sample ticks/sec */
  song[base+1*2+1]=0;
  song[base+7*2+1]=0x3f;
  memcpy(song+base+32,"End!",4);
  assert(sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  float pcm[1764]={0};
  assert(sauti_decoder_read(&d,pcm,1764)==1764);
  assert(pcm[0]>0.3f);
  assert(pcm[4]<-0.3f); /* advancing through alternating drum bytes */
  assert(sauti_decoder_seek(&d,0));
  float rewind[32];
  assert(sauti_decoder_read(&d,rewind,32)==32);
  assert(memcmp(pcm,rewind,sizeof(rewind))==0);
  sauti_decoder_close(&d);
  song[base+1*2]=0xd0; /* sync-buzzer is now supported */
  assert(sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  sauti_decoder_close(&d);
  song[base+14*2]=0; /* active timer effect without timer count */
  assert(!sauti_decoder_open(&d,registry,1,song,sizeof(song)));
 }
 /* Two YM6 effect slots: concurrent drums on channels A and B. */
 {
  uint8_t song[34+2*(4+8)+3+16*2+4]={0};
  memcpy(song,"YM6!LeOnArD!",12);
  w32(song+12,2);w32(song+16,1);song[21]=2;
  w32(song+22,2000000);song[27]=50;
  w32(song+34,8);w32(song+46,8);
  for(unsigned i=0;i<8;i++){song[38+i]=255;song[50+i]=0;}
  const size_t base=61;
  song[base+1*2]=0x50; /* slot 0, drum 0, channel A */
  song[base+3*2]=0x60; /* slot 1, drum 1, channel B */
  song[base+6*2]=0x20; /* /4 */
  song[base+8*2]=0x20|0; /* /4 and drum 0 index 0, volume A */
  song[base+9*2]=1; /* drum 1 index */
  song[base+14*2]=28;song[base+15*2]=28;
  song[base+7*2]=0x3f;
  song[base+7*2+1]=0x3f;
  memcpy(song+base+32,"End!",4);
  assert(sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  float pcm[1764]={0},replay[128];
  assert(sauti_decoder_read(&d,pcm,1764)==1764);
  assert(pcm[0]<0.01f && pcm[0]>-0.01f); /* opposite drums cancel */
  assert(sauti_decoder_seek(&d,0));
  assert(sauti_decoder_read(&d,replay,128)==128);
  assert(memcmp(pcm,replay,sizeof(replay))==0);
  sauti_decoder_close(&d);
  /* Both slots on A: one physical voice, not two summed drums.
   * Slot 1 retriggers after slot 0 and wins the collision. */
  song[base+3*2]=0x50;
  assert(sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  assert(sauti_decoder_read(&d,pcm,1764)==1764);
  assert(pcm[0]>0.30f && pcm[0]<0.35f);
  assert(sauti_decoder_seek(&d,0));
  assert(sauti_decoder_read(&d,replay,128)==128);
  assert(memcmp(pcm,replay,sizeof(replay))==0);
  sauti_decoder_close(&d);
  song[base+15*2]=0;
  assert(!sauti_decoder_open(&d,registry,1,song,sizeof(song)));
 }
 /* YM6 retriggers across VBLs: slot 0 changes voice while playing,
  * then slot 1 interrupts it on the new voice. Seek must reconstruct both. */
 {
  enum { FRAMES=3, DRUM_BYTES=1200, BASE=34+2*(4+DRUM_BYTES)+3 };
  static uint8_t song[BASE+16*FRAMES+4];
  memset(song,0,sizeof(song));
  memcpy(song,"YM6!LeOnArD!",12);
  w32(song+12,FRAMES);w32(song+16,1);song[21]=2;
  w32(song+22,2000000);song[27]=50;
  w32(song+34,DRUM_BYTES);
  memset(song+38,255,DRUM_BYTES);
  w32(song+38+DRUM_BYTES,DRUM_BYTES);
  memset(song+42+DRUM_BYTES,0,DRUM_BYTES);
  for(unsigned f=0;f<FRAMES;f++){
   song[BASE+7*FRAMES+f]=0x3f;
   song[BASE+6*FRAMES+f]=0x20;
   song[BASE+8*FRAMES+f]=0x20;
   song[BASE+9*FRAMES+f]=1;
   song[BASE+13*FRAMES+f]=0xff;
  }
  song[BASE+1*FRAMES+0]=0x50; /* slot 0, voice A, drum 0 */
  song[BASE+1*FRAMES+1]=0x60; /* same slot, voice B, drum 1 */
  song[BASE+3*FRAMES+2]=0x60; /* slot 1 wins on B, drum 1 */
  song[BASE+14*FRAMES+0]=28;
  song[BASE+14*FRAMES+1]=28;
  song[BASE+15*FRAMES+2]=28;
  memcpy(song+BASE+16*FRAMES,"End!",4);
  assert(sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  float full[2646]={0},replay[96]={0};
  assert(sauti_decoder_read(&d,full,2646)==2646);
  assert(full[0]>0.30f); /* first sample, drum 0 */
  assert(full[882]<-0.30f); /* slot 0 retriggers drum 1 */
  assert(full[1764]<-0.30f); /* slot 1 takes B from slot 0 */
  static const unsigned offsets[]={0,881,882,900,1763,1764,1790,2600};
  for(unsigned k=0;k<sizeof(offsets)/sizeof(offsets[0]);k++){
   unsigned at=offsets[k],count=2646-at;
   if(count>96)count=96;
   assert(sauti_decoder_seek(&d,at));
   assert(sauti_decoder_read(&d,replay,count)==count);
   assert(memcmp(full+at,replay,count*sizeof(float))==0);
  }
  sauti_decoder_close(&d);
 }
 /* Basic SID: volume changes at MFP timer rate; seeking is deterministic. */
 {
  uint8_t song[34+3+16*2+4]={0};
  memcpy(song,"YM6!LeOnArD!",12);
  w32(song+12,2);w32(song+16,1);
  w32(song+22,2000000);song[27]=50;
  const size_t base=37;
  song[base+0*2]=250; song[base+0*2+1]=250;
  song[base+1*2]=0x10; /* slot 0, SID, channel A */
  song[base+6*2]=0x80; /* MFP /50 */
  song[base+7*2]=0x3e;song[base+7*2+1]=0x3e;
  song[base+8*2]=15;song[base+8*2+1]=15;
  song[base+14*2]=50; /* around 983 Hz */
  song[base+13*2]=0xff;song[base+13*2+1]=0xff;
  memcpy(song+base+32,"End!",4);
  assert(sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  float pcm[1764]={0},replay[96]={0};
  assert(sauti_decoder_read(&d,pcm,1764)==1764);
  int saw_zero=0,saw_signal=0;
  for(unsigned i=0;i<882;i++){
   if(pcm[i]==0.0f)saw_zero=1;
   if(pcm[i]>0.01f||pcm[i]<-0.01f)saw_signal=1;
  }
  assert(saw_zero&&saw_signal);
  assert(sauti_decoder_seek(&d,200));
  assert(sauti_decoder_read(&d,replay,96)==96);
  assert(memcmp(replay,pcm+200,sizeof(replay))==0);
  sauti_decoder_close(&d);
 }
 /* Cross-type conflict: slot 1 digi-drum preempts slot 0 SID on A.
  * The mixed output must equal a drum-only reference. */
 {
  enum {BASE=34+2*(4+8)+3};
  uint8_t song[BASE+16*2+4]={0};
  memcpy(song,"YM6!LeOnArD!",12);
  w32(song+12,2);w32(song+16,1);song[21]=2;
  w32(song+22,2000000);song[27]=50;
  w32(song+34,8);w32(song+46,8);
  for(unsigned i=0;i<8;i++){song[38+i]=128;song[50+i]=(i&1u)?0:255;}
  song[BASE+1*2]=0x10; /* slot 0 SID on A */
  song[BASE+3*2]=0x50; /* slot 1 drum #1 on A: must win */
  song[BASE+6*2]=0x20;song[BASE+8*2]=0x21;
  song[BASE+7*2]=0x3f;song[BASE+7*2+1]=0x3f;
  song[BASE+14*2]=28;song[BASE+15*2]=28;
  song[BASE+13*2]=0xff;song[BASE+13*2+1]=0xff;
  memcpy(song+BASE+32,"End!",4);
  float both[1764],reference[1764],replay[96];
  assert(sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  assert(sauti_decoder_read(&d,both,1764)==1764);
  assert(sauti_decoder_seek(&d,881));
  assert(sauti_decoder_read(&d,replay,96)==96);
  assert(memcmp(replay,both+881,sizeof(replay))==0);
  sauti_decoder_close(&d);
  song[BASE+1*2]=0; /* no SID, keep drum */
  song[BASE+14*2]=0;
  assert(sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  assert(sauti_decoder_read(&d,reference,1764)==1764);
  assert(memcmp(both,reference,sizeof(reference))==0);
  sauti_decoder_close(&d);
 }
 /* Sinus-SID (type 2) controls the YM voice with an 8-step shape.
  * It must vary smoothly through intermediate amplitudes and replay exactly
  * after seek across the VBL boundary. */
 {
  uint8_t song[34+3+16*2+4]={0};
  memcpy(song,"YM6!LeOnArD!",12);w32(song+12,2);w32(song+16,1);
  w32(song+22,2000000);song[27]=50;
  const size_t base=37;
  song[base+0*2]=250;song[base+0*2+1]=250;
  song[base+1*2]=0x90; /* slot 0 sinus SID, channel A */
  song[base+6*2]=0x80; /* /50 */
  song[base+7*2]=0x3e;song[base+7*2+1]=0x3e;
  song[base+8*2]=15;song[base+8*2+1]=15;
  song[base+14*2]=50;
  song[base+13*2]=0xff;song[base+13*2+1]=0xff;
  memcpy(song+base+32,"End!",4);
  float pcm[1764]={0},replay[96]={0};
  assert(sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  assert(sauti_decoder_read(&d,pcm,1764)==1764);
  int zero=0,low=0,high=0;
  for(unsigned i=0;i<882;i++){
   float v=pcm[i]<0?-pcm[i]:pcm[i];
   if(v==0)zero=1;
   if(v>0.04f&&v<0.25f)low=1;
   if(v>0.30f)high=1;
  }
  assert(zero&&low&&high);
  assert(sauti_decoder_seek(&d,881));
  assert(sauti_decoder_read(&d,replay,96)==96);
  assert(memcmp(replay,pcm+881,sizeof(replay))==0);
  sauti_decoder_close(&d);
 }
 /* YM6 sync-buzzer (type 3): repeated envelope restart via MFP.
  * Compare against non-buzzer envelope and verify seek replay. */
 {
  uint8_t song[34+3+16*2+4]={0};
  memcpy(song,"YM6!LeOnArD!",12);
  w32(song+12,2);w32(song+16,1);
  w32(song+22,2000000);song[27]=50;
  const size_t base=37;
  song[base+7*2]=0x3f;song[base+7*2+1]=0x3f;
  song[base+8*2]=0x10;song[base+8*2+1]=0x10;
  song[base+11*2]=1;song[base+11*2+1]=1;
  song[base+13*2]=0x0c;song[base+13*2+1]=0xff;
  song[base+1*2]=0xd0; /* slot 0, sync-buzzer, channel A */
  song[base+6*2]=0x80; /* MFP /50 */
  song[base+14*2]=50; /* about 983 Hz envelope reset */
  memcpy(song+base+32,"End!",4);
  float buzzer[1764]={0},plain[1764]={0},replay[96]={0};
  assert(sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  assert(sauti_decoder_read(&d,buzzer,1764)==1764);
  assert(sauti_decoder_seek(&d,881));
  assert(sauti_decoder_read(&d,replay,96)==96);
  assert(memcmp(buzzer+881,replay,sizeof(replay))==0);
  sauti_decoder_close(&d);
  song[base+1*2]=0; song[base+14*2]=0; /* no timer effect */
  assert(sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  assert(sauti_decoder_read(&d,plain,1764)==1764);
  sauti_decoder_close(&d);
  int different=0;
  for(unsigned i=0;i<882;i++)if(buzzer[i]!=plain[i]){different=1;break;}
  assert(different);
 }
 /* A slot switching from sync-buzzer to SID must stop restarting
  * the shared envelope. Frame 1 explicitly resets R13, so its complete
  * PCM must match an otherwise identical song without the old buzzer. */
 {
  uint8_t song[34+3+16*2+4]={0};
  memcpy(song,"YM6!LeOnArD!",12);
  w32(song+12,2);w32(song+16,1);
  w32(song+22,2000000);song[27]=50;
  const size_t base=37;
  for(unsigned f=0;f<2;f++){
   song[base+7*2+f]=0x3f;
   song[base+8*2+f]=15; /* SID volume on A */
   song[base+9*2+f]=0x10; /* envelope on B */
   song[base+11*2+f]=1;
   song[base+13*2+f]=0x0c; /* explicit envelope restart */
   song[base+6*2+f]=0x80; /* MFP /50 */
   song[base+14*2+f]=50;
  }
  song[base+1*2]=0xd0; /* frame 0: sync-buzzer */
  song[base+1*2+1]=0x10; /* frame 1: SID on same slot */
  memcpy(song+base+32,"End!",4);
  float switched[1764],reference[1764];
  assert(sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  assert(sauti_decoder_read(&d,switched,1764)==1764);
  sauti_decoder_close(&d);
  song[base+1*2]=0; song[base+14*2]=0; /* no buzzer in frame 0 */
  assert(sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  assert(sauti_decoder_read(&d,reference,1764)==1764);
  sauti_decoder_close(&d);
  assert(memcmp(switched+882,reference+882,882*sizeof(float))==0);
 }
 /* Replacing SID with sync-buzzer in the SAME slot must stop SID.
  * The second VBL must match a song that never launched SID. */
 {
  uint8_t song[34+3+16*2+4]={0};
  memcpy(song,"YM6!LeOnArD!",12);
  w32(song+12,2);w32(song+16,1);
  w32(song+22,2000000);song[27]=50;
  const size_t base=37;
  for(unsigned f=0;f<2;f++){
   song[base+0*2+f]=250;
   song[base+7*2+f]=0x3e;
   song[base+8*2+f]=15;
   song[base+13*2+f]=0x0c;
   song[base+6*2+f]=0x80;
   song[base+14*2+f]=50;
  }
  song[base+1*2]=0x10; /* SID on A in VBL 0 */
  song[base+1*2+1]=0xd0; /* sync-buzzer on A in VBL 1 */
  memcpy(song+base+32,"End!",4);
  float transitioned[1764],reference[1764];
  assert(sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  assert(sauti_decoder_read(&d,transitioned,1764)==1764);
  sauti_decoder_close(&d);
  song[base+1*2]=0;song[base+14*2]=0; /* no SID in VBL 0 */
  assert(sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  assert(sauti_decoder_read(&d,reference,1764)==1764);
  sauti_decoder_close(&d);
  assert(memcmp(transitioned+882,reference+882,882*sizeof(float))==0);
 }
 /* MFP /4, count=1: ~13.93 timer ticks per PCM sample. SID must
  * use the parity of ALL elapsed timer ticks, not toggle only once. */
 {
  uint8_t song[34+3+16*2+4]={0};
  memcpy(song,"YM6!LeOnArD!",12);
  w32(song+12,2);w32(song+16,1);
  w32(song+22,2000000);song[27]=50;
  const size_t base=37;
  for(unsigned f=0;f<2;f++){
   song[base+7*2+f]=0x3f; /* tone/noise disabled: fixed positive channel A */
   song[base+8*2+f]=15;
   song[base+13*2+f]=0xff;
  }
  song[base+1*2]=0x10; /* SID, channel A, slot 0 */
  song[base+6*2]=0x20; /* MFP prescaler /4 */
  song[base+14*2]=1; /* 614400 ticks/s, above PCM rate */
  memcpy(song+base+32,"End!",4);
  float pcm[512]={0},replay[80]={0};
  assert(sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  assert(sauti_decoder_read(&d,pcm,512)==512);
  for(unsigned i=0;i<512;i++){
   /* State at sample i is determined by ticks in the first i samples. */
   uint64_t numerator=(uint64_t)i*614400u;
   /* Exact integer MFP accumulation also covers PCM boundaries. */
   uint64_t ticks=numerator/44100u;
   if(ticks&1u)assert(pcm[i]==0.0f);
   else assert(pcm[i]>0.01f);
  }
  assert(sauti_decoder_seek(&d,217));
  assert(sauti_decoder_read(&d,replay,80)==80);
  assert(memcmp(replay,pcm+217,sizeof(replay))==0);
  sauti_decoder_close(&d);
 }
 /* NASM digi-drum PCM conversion: unsigned/signed 8-bit and ST nibble. */
 {
  uint8_t u=255,z=128,lo=0,hi=15,neg=0x80;
  assert(sauti_ym_drum_sample_asm(&u,0)>0.30f);
  assert(sauti_ym_drum_sample_asm(&z,0)==0.0f);
  assert(sauti_ym_drum_sample_asm(&neg,2)< -0.30f);
  assert(sauti_ym_drum_sample_asm(&lo,4)<0.0f);
  assert(sauti_ym_drum_sample_asm(&hi,4)>0.0f);
 }
 /* Direct mixer regression: envelope-selected nonzero level MUST make sound.
  * Previous NASM code jumped on stale flags after MOV and silently muted it. */
 {
  uint8_t regs[16]={0};
  uint32_t ph[3]={0,0,0};
  float sample=0;
  regs[0]=0x40; regs[1]=0; regs[7]=0x3e;
  regs[8]=0x10; /* envelope for channel A */
  sauti_ym_tone_step_asm(regs,ph,65536,&sample,1,15);
  assert(sample>0.0f);
  regs[8]=0;ph[0]=0;sample=1.0f;
  sauti_ym_tone_step_asm(regs,ph,65536,&sample,1,15);
  assert(sample==0.0f);
  regs[8]=0x10;ph[0]=0;sample=1.0f;
  sauti_ym_tone_step_asm(regs,ph,65536,&sample,1,0);
  assert(sample==0.0f);
  regs[8]=0x0f;ph[0]=0;sample=0.0f;
  sauti_ym_tone_step_asm(regs,ph,65536,&sample,1,0);
  assert(sample>0.0f); /* fixed volume unaffected by envelope=0 */
 }
 /* Frequency regression: 2 MHz YM clock / (16 * period 250) = 500 Hz.
  * Count square-wave crossings across exactly one second at 44.1 kHz. */
 {
  uint8_t regs[16]={0};
  uint32_t phases[3]={0};
  uint32_t step=(uint32_t)(((uint64_t)2000000*65536)/(8*44100));
  unsigned crossings=0;
  float previous=0.0f;
  regs[0]=250;regs[7]=0x3e;regs[8]=15;
  for(unsigned i=0;i<44100;i++){
   float v=0.0f;
   sauti_ym_tone_step_asm(regs,phases,step,&v,1,0);
   if(i && (v>0.0f)!=(previous>0.0f))crossings++;
   previous=v;
  }
  /* 500 full periods -> about 1000 half-wave crossings. */
  assert(crossings>=997 && crossings<=1002);
 }
 /* YM DAC regression: fixed 16 levels map to odd entries of the
  * independent emu2149 32-level table; envelope uses all 32 entries. */
 {
  static const unsigned dac[32]={
   0,1,1,2,2,3,3,4,5,6,7,9,11,13,15,18,
   22,26,31,37,45,53,63,76,90,106,127,151,180,214,255,255
  };
  uint8_t regs[16]={0};
  uint32_t ph[3]={0};
  regs[0]=0x80;regs[7]=0x3e;
  for(unsigned level=0;level<16;level++){
   float sample=0.0f;
   ph[0]=0;regs[8]=(uint8_t)level;
   sauti_ym_tone_step_asm(regs,ph,65536,&sample,1,0);
   float expected=level?(float)dac[2*level+1]/765.0f:0.0f;
   float error=sample-expected;
   if(error<0.0f)error=-error;
   assert(error<0.000001f);
  }
  for(unsigned level=0;level<32;level++){
   float sample=0.0f;
   ph[0]=0;regs[8]=0x10;
   sauti_ym_tone_step_asm(regs,ph,65536,&sample,1,level);
   float expected=(float)dac[level]/765.0f;
   float error=sample-expected;
   if(error<0.0f)error=-error;
   assert(error<0.000001f);
  }
  regs[8]=0x0f;regs[9]=0x0f;regs[10]=0x0f;
  regs[7]=0x3f; /* all tone/noise gates held open */
  float mixed=0.0f;ph[0]=ph[1]=ph[2]=0;
  sauti_ym_tone_step_asm(regs,ph,65536,&mixed,1,0);
  assert(mixed>0.999f && mixed<1.001f);
 }
 /* YM6 R13 bus-write semantics:
  * FF = no write; explicit same value in next frame must retrigger. */
 {
  unsigned char song[sizeof(b)];
  memcpy(song,b,sizeof(song));
  /* Restore valid metadata, two identical frames, envelope-only A. */
  song[34+3+32]='E';
  song[34+3+7]=0x3f; song[34+3+8]=0x10;
  song[34+3+16+7]=0x3f; song[34+3+16+8]=0x10;
  song[34+3+11]=200; song[34+3+12]=0; song[34+3+13]=0x0a;
  song[34+3+16+11]=200; song[34+3+16+12]=0; song[34+3+16+13]=0xff;
  memcpy(song+34+3+32,"End!",4);
  float no_write[1764]={0}, retrigger[1764]={0};
  assert(sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  assert(sauti_decoder_read(&d,no_write,1764)==1764);
  sauti_decoder_close(&d);
  song[34+3+16+13]=0x0a;
  assert(sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  assert(sauti_decoder_read(&d,retrigger,1764)==1764);
  sauti_decoder_close(&d);
  /* First 882 samples are unchanged; subsequent envelope differs. */
  assert(memcmp(no_write,retrigger,882*sizeof(float))==0);
  int different=0;
  for(unsigned i=882;i<1764;i++)
   if(no_write[i]!=retrigger[i]){different=1;break;}
  assert(different);
 }
 /* Independent discrete envelope shape model: verify all 16 shapes. */
 for(unsigned shape=0;shape<16;shape++){
  YmEnvelope env={0};
  sauti_ym_env_init_asm(&env,shape);
  unsigned attack=(shape&4)?31u:0u;
  unsigned alt=(shape&2)!=0u;
  unsigned hold=(shape&1)!=0u;
  if(!(shape&8)){hold=1;alt=(shape&4)!=0u;}
  unsigned step=31,holding=0;
  for(unsigned tick=0;tick<100;tick++){
   unsigned want=step^attack;
   unsigned got=sauti_ym_env_step_asm(&env,0,1);
   assert(got==want);
   if(!holding){
    if(step){step--;}
    else if(hold){
     holding=1;
     if(alt)attack^=31u;
    }else{
     step=31;
     if(alt)attack^=31u;
    }
   }
   (void)sauti_ym_env_step_asm(&env,65536,1);
  }
 }
 /* Real YM2 and YM3 header layout: 14 register planes, 50 Hz.
  * Build two frames independently of the YM5/YM6 fixture above. */
 for(unsigned version=2;version<=3;version++){
  uint8_t legacy[4+14*2]={0};
  memcpy(legacy,version==2?"YM2!":"YM3!",4);
  legacy[4+0*2+0]=250; legacy[4+0*2+1]=125;
  legacy[4+7*2+0]=0x3e;legacy[4+7*2+1]=0x3e;
  legacy[4+8*2+0]=15; legacy[4+8*2+1]=15;
  legacy[4+13*2+0]=0xff;legacy[4+13*2+1]=0xff;
  assert(sauti_decoder_open(&d,registry,1,legacy,sizeof(legacy)));
  assert(d.info.sample_rate==44100&&d.info.channels==1&&
         d.info.total_frames==1764);
  float old[1764]={0};
  assert(sauti_decoder_read(&d,old,1764)==1764);
  assert(sauti_decoder_read(&d,old,1)==0);
  float a=old[0],min=1.0f,max=-1.0f;
  for(size_t i=0;i<1764;i++){
   if(old[i]<min)min=old[i];
   if(old[i]>max)max=old[i];
  }
  (void)a;
  assert(min<0.0f&&max>0.0f);
  assert(sauti_decoder_seek(&d,0));
  float replay[64]={0};
  assert(sauti_decoder_read(&d,replay,64)==64);
  assert(memcmp(replay,old,sizeof(replay))==0);
  sauti_decoder_close(&d);
  assert(!sauti_decoder_open(&d,registry,1,legacy,sizeof(legacy)-1));
  legacy[0]='X';
  assert(!sauti_decoder_open(&d,registry,1,legacy,sizeof(legacy)));
 }
 /* YM2/YM3 nonzero seek: state-replay PCM must match uninterrupted
  * rendering, including crossing the 50 Hz VBL transition and EOF. */
 for(unsigned version=2;version<=3;version++){
  uint8_t song[4+14*4]={0};
  memcpy(song,version==2?"YM2!":"YM3!",4);
  for(unsigned f=0;f<4;f++){
   song[4+0*4+f]=(uint8_t)(120+f*14);
   song[4+6*4+f]=(uint8_t)(f+1);
   song[4+7*4+f]=0x3e;
   song[4+8*4+f]=0x10;
   song[4+11*4+f]=5;
   song[4+13*4+f]=(f==1)?0xff:0x0a;
  }
  float continuous[3528],from_seek[96];
  assert(sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  assert(sauti_decoder_read(&d,continuous,3528)==3528);
  static const unsigned offsets[]={1,100,881,882,883,1759,1764,2599,3432};
  for(size_t k=0;k<sizeof(offsets)/sizeof(offsets[0]);k++){
   unsigned at=offsets[k];
   assert(sauti_decoder_seek(&d,at));
   assert(sauti_decoder_read(&d,from_seek,96)==
          (at+96<=3528?96:3528-at));
   size_t cmp=(at+96<=3528?96:3528-at);
   assert(memcmp(from_seek,continuous+at,cmp*sizeof(float))==0);
  }
  /* Backward and forward seeks reuse stored decoder states. */
  static const unsigned jumps[]={2600,100,3432,882,1759,2599,1,1764};
  for(size_t j=0;j<sizeof(jumps)/sizeof(jumps[0]);j++){
   unsigned at=jumps[j];
   assert(sauti_decoder_seek(&d,at));
   assert(sauti_decoder_read(&d,from_seek,96)==96);
   assert(memcmp(from_seek,continuous+at,sizeof(from_seek))==0);
  }
  assert(sauti_decoder_seek(&d,3528));
  assert(sauti_decoder_read(&d,from_seek,1)==0);
  assert(!sauti_decoder_seek(&d,3529));
  sauti_decoder_close(&d);
 }
 /* YM3b: identical 14 register planes, plus big-endian trailing loop frame.
  * Decode stops at EOF because the PCM API has no loop metadata yet. */
 {
  uint8_t song[4+14*3+4]={0};
  memcpy(song,"YM3b",4);
  for(unsigned f=0;f<3;f++){
   song[4+0*3+f]=(uint8_t)(100+f*25);
   song[4+7*3+f]=0x3e;
   song[4+8*3+f]=15;
   song[4+13*3+f]=0xff;
  }
  w32(song+sizeof(song)-4,1);
  assert(sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  assert(d.info.total_frames==2646);
  uint64_t loop=UINT64_MAX;
  assert(sauti_decoder_ym_loop_start(&d,&loop)&&loop==882);
  float whole[2646],section[96];
  assert(sauti_decoder_read(&d,whole,2646)==2646);
  assert(sauti_decoder_read(&d,section,1)==0);
  assert(sauti_decoder_seek(&d,882));
  assert(sauti_decoder_read(&d,section,96)==96);
  assert(memcmp(section,whole+882,sizeof(section))==0);
  sauti_decoder_close(&d);
  assert(!sauti_decoder_ym_loop_start(&d,&loop));
  w32(song+sizeof(song)-4,3); /* loop target past the last valid frame */
  assert(!sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  w32(song+sizeof(song)-4,1);
  assert(!sauti_decoder_open(&d,registry,1,song,sizeof(song)-1));
  song[0]='X';
  assert(!sauti_decoder_open(&d,registry,1,song,sizeof(song)));
 }
 /* Playback checkpoint boundaries must be independent of read chunk size.
  * Rewind across 5s checkpoints and compare with uninterrupted decoding. */
 {
  enum { FRAMES=600, TOTAL=FRAMES*882 };
  static uint8_t song[4+14*FRAMES];
  static float expected[TOTAL];
  uint8_t *planes=song+4;
  memcpy(song,"YM3!",4);
  for(unsigned f=0;f<FRAMES;f++){
   planes[0*FRAMES+f]=(uint8_t)(80+(f%90));
   planes[7*FRAMES+f]=0x3e;
   planes[8*FRAMES+f]=15;
   planes[13*FRAMES+f]=0xff;
  }
  assert(sauti_decoder_open(&d,registry,1,song,sizeof(song)));
  unsigned offset=0;
  while(offset<TOTAL){
   unsigned chunk=(offset%3u==0)?377u:2048u;
   if(chunk>TOTAL-offset)chunk=TOTAL-offset;
   assert(sauti_decoder_read(&d,expected+offset,chunk)==chunk);
   offset+=chunk;
  }
  static const unsigned seeks[]={220500,441000,132300,440999,220499,352800,0,528900};
  float samples[117];
  for(unsigned j=0;j<sizeof(seeks)/sizeof(seeks[0]);j++){
   unsigned at=seeks[j];
   assert(sauti_decoder_seek(&d,at));
   assert(sauti_decoder_read(&d,samples,117)==117);
   assert(memcmp(samples,expected+at,sizeof(samples))==0);
  }
  sauti_decoder_close(&d);
 }
 /* Ten-second YM6 checkpoint stress: both SID and sync-buzzer share
  * normal playback, variable buffer sizes and zig-zag seeks. Compare
  * bit-identical PCM with an independent uninterrupted decoder. */
 {
  enum {FRAMES=500, TOTAL=FRAMES*882, BASE=34+3};
  static uint8_t song[BASE+16*FRAMES+4];
  static float continuous[TOTAL];
  memset(song,0,sizeof(song));
  memcpy(song,"YM6!LeOnArD!",12);
  w32(song+12,FRAMES);w32(song+16,1);
  w32(song+22,2000000);song[27]=50;
  for(unsigned f=0;f<FRAMES;f++){
   song[BASE+0*FRAMES+f]=(uint8_t)(95+f%65);
   song[BASE+7*FRAMES+f]=0x3e;
   song[BASE+8*FRAMES+f]=15;
   song[BASE+11*FRAMES+f]=2;
   song[BASE+13*FRAMES+f]=f%97==0?0x0c:0xff;
  }
  for(unsigned f=0;f<FRAMES;f+=50){
   song[BASE+1*FRAMES+f]=(f/50)%2==0?0x10:0xd0;
   song[BASE+6*FRAMES+f]=0x80;
   song[BASE+14*FRAMES+f]=50;
  }
  memcpy(song+BASE+16*FRAMES,"End!",4);
  const SautiDecoderOps *ym_ops[]={&sauti_decoder_ym_ops};
  SautiDecoder a={0},b={0};
  assert(sauti_decoder_open(&a,ym_ops,1,song,sizeof(song)));
  assert(sauti_decoder_open(&b,ym_ops,1,song,sizeof(song)));
  assert(sauti_decoder_read(&a,continuous,TOTAL)==TOTAL);
  float piece[301];
  unsigned at=0;
  while(at<TOTAL){
   unsigned n=at%2?301:127;
   if(n>TOTAL-at)n=TOTAL-at;
   assert(sauti_decoder_read(&b,piece,n)==n);
   assert(memcmp(piece,continuous+at,n*sizeof(float))==0);
   at+=n;
  }
  static const unsigned offsets[]={352800,88200,220500,0,440883,132300,4410,264600,352799};
  for(unsigned j=0;j<sizeof(offsets)/sizeof(offsets[0]);j++){
   at=offsets[j];
   assert(sauti_decoder_seek(&b,at));
   assert(sauti_decoder_read(&b,piece,117)==117);
   assert(memcmp(piece,continuous+at,117*sizeof(float))==0);
  }
  sauti_decoder_close(&a);
  sauti_decoder_close(&b);
 }
 /* YM2 ANCOOL1 layout: 64,404 raw bytes -> exactly 4600 VBL frames. */
 {
  static uint8_t ancool_shape[64404];
  memcpy(ancool_shape,"YM2!",4);
  const unsigned frames=4600;
  memset(ancool_shape+4+7*frames,0x3e,frames);
  memset(ancool_shape+4+8*frames,15,frames);
  memset(ancool_shape+4+13*frames,0xff,frames);
  assert(sauti_decoder_open(&d,registry,1,ancool_shape,sizeof(ancool_shape)));
  assert(d.info.total_frames==4600u*882u);
  sauti_decoder_close(&d);
 }
 puts("PASS: YM2/YM3/YM3b/YM6 tone/noise/envelope NASM; 16 shapes, nonlinear DAC, R13, seek and 500Hz regressions");
 return 0;
}
