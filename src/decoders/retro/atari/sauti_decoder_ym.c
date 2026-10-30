/* Experimental YM2/YM3/YM3b/YM5/YM6 Atari PSG register-dump decoder.
 * NASM generates tone, noise, envelope and nonlinear volume.
 * Not hardware-cycle-accurate; no YM2 Mad Max external digi-drums.
 */
#include "sauti_decoder_ym.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stddef.h>
#define YM_RATE 44100u
#ifdef SAUTI_YM_LISTEN_REFERENCE
/* Experimental ST-Sound-inspired playback path, isolated to a second binary.
 * Identical processing order to tools/compare_ym_pcm.py stsound_filter()
 * followed by offline candidate gain 0.5. Not an exact hardware model. */
static int32_t ym_floor_div_pow2(int32_t value,unsigned bits){
 int32_t divisor=(int32_t)(1u<<bits);
 return value>=0?value/divisor:-(((-((int64_t)value))+divisor-1)/divisor);
}
#endif

#define YM_CHECKPOINT_INTERVAL (YM_RATE*5u)
typedef struct {uint32_t phase,index,attack,alternate,hold,holding;} YmEnvelope;
typedef struct Ym {
 const uint8_t *data;
 uint32_t frames, fps, clock, phase[3],step,noise_phase,noise_rng,noise_step;
 uint64_t pos,total,frame_samples;
 YmEnvelope env;
#ifdef SAUTI_YM_LISTEN_REFERENCE
 int32_t dc_history[512],dc_total,lp1,lp2;
 uint32_t dc_pos;
#endif
 uint32_t env_shape;
 uint32_t registers,last_frame;
 uint64_t loop_frame;
 const uint8_t *drums[32];
 uint32_t drum_sizes[32],drum_count,drum_attr;
 struct {uint64_t phase,step;uint32_t voice,volume,wave;int active,on;} sid[2];
 struct {uint64_t phase,step;uint32_t shape;int active;} buzzer[2];
 struct { const uint8_t *active; uint64_t phase,step; uint32_t pos,size,voice; } drum[2];
 const SautiYm2Sample *ym2_bank;
 size_t ym2_bank_count;
 const uint8_t *ym2_active;
 uint32_t ym2_size,ym2_index,ym2_timer;
 uint64_t ym2_phase;
 int is_ym2;
 int interleaved,has_loop;
 struct Ym *checkpoints[8];
 unsigned next_checkpoint;
} Ym;
/* Snapshots copy only decoder state, not ownership of the snapshot cache. */
#define YM_STATE_BYTES offsetof(Ym,checkpoints)
static void checkpoint_save(Ym *s){
 /* Avoid duplicates when seeking to an already cached position. */
 for(unsigned i=0;i<8;i++)
  if(s->checkpoints[i] && s->checkpoints[i]->pos==s->pos)return;
 unsigned index=s->next_checkpoint++%8u;
 if(!s->checkpoints[index])s->checkpoints[index]=malloc(sizeof(Ym));
 if(s->checkpoints[index])memcpy(s->checkpoints[index],s,YM_STATE_BYTES);
}

extern void sauti_ym_tone_step_asm(const uint8_t *,uint32_t *,uint32_t,float *,uint32_t,uint32_t);
extern uint32_t sauti_ym_noise_step_asm(uint32_t *,uint32_t *,uint32_t,uint32_t);
extern void sauti_ym_env_init_asm(YmEnvelope *,uint32_t);
extern uint32_t sauti_ym_env_step_asm(YmEnvelope *,uint32_t,uint32_t);
extern float sauti_ym_drum_sample_asm(const uint8_t *,uint32_t);
static uint16_t b16(const uint8_t *p){return (uint16_t)((p[0]<<8)|p[1]);}
/* Accumulate exact MFP clock ticks with a rational remainder.
 * phase is in timer-clock units; step is ticks-per-event denominator.
 * The quotient counts every event, including multiple per PCM sample.
 */
static uint32_t mfp_advance(uint64_t *phase,uint64_t denominator){
 *phase+=2457600u;
 uint32_t ticks=(uint32_t)(*phase/denominator);
 *phase%=denominator;
 return ticks;
}
static uint32_t b32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static int probe(const uint8_t *p,size_t n){
 return p && ((n>=4 && (!memcmp(p,"YM2!",4)||!memcmp(p,"YM3!",4)||!memcmp(p,"YM3b",4))) ||
               (n>=12 && (!memcmp(p,"YM5!LeOnArD!",12)||!memcmp(p,"YM6!LeOnArD!",12))));
}
static int skip_string(const uint8_t *p,size_t n,size_t *off){
 if(*off>=n)return 0;
 const uint8_t *e=memchr(p+*off,0,n-*off);
 if(!e)return 0;
 *off=(size_t)(e-p)+1;
 return 1;
}
static void *open_ym(const uint8_t *p,size_t n,SautiPcmInfo *info){
 if(!probe(p,n)||!info)return NULL;
 uint32_t frames=0,clock=2000000,fps=50,registers=14,drum_count=0,attr=0,loop_index=0;
 const uint8_t *drums[32]={0};
 uint32_t drum_sizes[32]={0};
 size_t off=4;
 int il=1;
 if(n>=4&&(!memcmp(p,"YM2!",4)||!memcmp(p,"YM3!",4)||!memcmp(p,"YM3b",4))){
  /* YM3b adds a big-endian loop-frame index in the final four bytes. */
  int has_loop=!memcmp(p,"YM3b",4);
  /* Check length BEFORE subtraction: short YM3b headers underflow size_t. */
  if(n<4u+(has_loop?4u:0u))return NULL;
  size_t payload=n-4u-(has_loop?4u:0u);
  if((has_loop&&n<22u)||!payload||payload%14u)return NULL;
  if(payload/14u>1000000u)return NULL;
  frames=(uint32_t)(payload/14u);
  if(has_loop&&b32(p+n-4u)>=frames)return NULL;
 }else{
  if(n<38)return NULL;
  frames=b32(p+12);
  attr=b32(p+16);
  clock=b32(p+22);fps=b16(p+26);loop_index=b32(p+28);
  registers=16;
  off=34;
  if(!frames||frames>1000000||!fps||fps>1000||
     clock<100000||clock>16000000||loop_index>=frames)return NULL;
  /* Digi-drum payloads precede the title/author/comment strings.
   * Parse and bounds-check them before scheduling the supported drum effects.
   * Bits 1/2 describe signed and ST 4-bit sample encoding. */
  drum_count=b16(p+20);
  if(drum_count>32u||b16(p+32)!=0||(attr&~7u))return NULL;
  for(uint32_t i=0;i<drum_count;i++){
   if(off>n||n-off<4u)return NULL;
   uint32_t bytes=b32(p+off);
   off+=4u;
   if(bytes>n-off)return NULL;
   drums[i]=p+off;drum_sizes[i]=bytes;
   off+=(size_t)bytes;
  }
  for(int i=0;i<3;i++)if(!skip_string(p,n,&off))return NULL;
  /* Validate the available tail before computing frame-plane offsets. */
  if(off>n || n-off<4u)return NULL;
  if((size_t)frames>(n-off-4u)/16u)return NULL;
  size_t span=(size_t)frames*16u;
  if(memcmp(p+off+span,"End!",4))return NULL;
  il=(attr&1u)!=0;
  const uint8_t *data=p+off;
  for(uint32_t f=0;f<frames;f++){
   static const uint32_t divs[8]={0,4,10,16,50,64,100,200};
   for(unsigned slot=0;slot<2;slot++){
    unsigned er=slot?3u:1u,pr=slot?8u:6u,tr=slot?15u:14u;
    uint8_t code=data[il?(size_t)er*frames+f:(size_t)f*16u+er]>>4;
    unsigned voice=code&3u,kind=code>>2;
    if(!voice){
     /* R14/R15 may carry ordinary port/register data. Only an enabled
      * effect voice gives these bytes timer semantics. A disabled effect
      * must not make an otherwise valid YM5/YM6 dump unreadable. */
     continue;
    }
    if(kind>3u)return NULL; /* Four YM6 timer effect types. */
    uint8_t pre=data[il?(size_t)pr*frames+f:(size_t)f*16u+pr]>>5;
    uint8_t tc=data[il?(size_t)tr*frames+f:(size_t)f*16u+tr];
    uint8_t volume=data[il?(size_t)(7u+voice)*frames+f:(size_t)f*16u+7u+voice];
    if(!divs[pre]||!tc)return NULL;
    if(kind==1u && ((volume&31u)>=drum_count||!drum_sizes[volume&31u]))return NULL;
   }
  }
 }
 Ym *s=calloc(1,sizeof(*s));if(!s)return NULL;
 if(n>=4&&!memcmp(p,"YM3b",4)){
  s->has_loop=1;
  s->loop_frame=(uint64_t)b32(p+n-4u)*YM_RATE/fps;
 }else if(registers==16u){
  /* YM5/YM6 store the repeat VBL at header offset 28 (zero = start).
   * Convert to the same PCM-frame timeline used for YM3b loops. */
  s->has_loop=1;
  s->loop_frame=(uint64_t)loop_index*YM_RATE/fps;
 }
 s->is_ym2=(n>=4 && !memcmp(p,"YM2!",4));
 s->drum_count=drum_count;s->drum_attr=attr;
 memcpy(s->drums,drums,sizeof(drums));
 memcpy(s->drum_sizes,drum_sizes,sizeof(drum_sizes));
 s->data=p+off;s->frames=frames;s->fps=fps;s->clock=clock;
 s->registers=registers;s->interleaved=il;
 s->total=(uint64_t)frames*YM_RATE/fps;
 /* Tone half-wave and 32-step envelope use clock/8; noise uses clock/16. */
 s->step=(uint32_t)(((uint64_t)clock*65536u)/(8u*YM_RATE));
 s->noise_step=(uint32_t)(((uint64_t)clock*65536u)/(16u*YM_RATE));s->noise_rng=1u;
 s->env_shape=UINT32_MAX;s->last_frame=UINT32_MAX;
 if(!s->step||!s->noise_step||!s->total){free(s);return NULL;}
 info->channels=1;info->sample_rate=YM_RATE;info->total_frames=s->total;
 return s;
}
#ifdef SAUTI_YM_LISTEN_REFERENCE
static float ym_reference_listen_filter(Ym *s,float input){
 /* Clamp before conversion, matching the diagnostic PCM16 input domain. */
 double scaled=(double)input*32768.0;
 if(scaled>32767.0)scaled=32767.0;
 if(scaled< -32768.0)scaled=-32768.0;
 int32_t current=(int32_t)(scaled>=0?scaled+0.5:scaled-0.5);
 s->dc_total+=current-s->dc_history[s->dc_pos];
 s->dc_history[s->dc_pos]=current;
 s->dc_pos=(s->dc_pos+1u)&511u;
 /* The reference diagnostic truncates the DC average toward zero. */
 int32_t highpass=current-s->dc_total/512;
 int32_t lowpass=ym_floor_div_pow2(s->lp2,2)+
                 ym_floor_div_pow2(s->lp1,1)+
                 ym_floor_div_pow2(highpass,2);
 s->lp2=s->lp1;
 s->lp1=highpass;
 if(lowpass>32767)lowpass=32767;
 if(lowpass< -32768)lowpass=-32768;
 return (float)lowpass*(0.5f/32768.0f);
}
#endif
static size_t read_ym(void *ctx,float *samples,size_t count){
 Ym *s=ctx;
 if(!s||!samples)return 0;
 if((uint64_t)count>s->total-s->pos)count=(size_t)(s->total-s->pos);
 for(size_t i=0;i<count;i++){
  /* Cache the pre-sample state at exact time intervals. Using pos+i
   * instead of read() call boundaries keeps results independent of
   * audio backend buffer sizes. */
  if((s->pos+i) && (s->pos+i)%YM_CHECKPOINT_INTERVAL==0){
   uint64_t original_pos=s->pos;
   s->pos+=i;
   checkpoint_save(s);
   s->pos=original_pos;
  }
  uint32_t frame=(uint32_t)(((s->pos+i)*(uint64_t)s->fps)/YM_RATE);
  if(frame>=s->frames)frame=s->frames-1;
  uint8_t regs[16]={0};
  for(unsigned r=0;r<s->registers;r++)
   regs[r]=s->data[s->interleaved?(size_t)r*s->frames+frame:
                    (size_t)frame*s->registers+r];
  if(frame!=s->last_frame && s->registers==16u){
   static const uint32_t divs[8]={0,4,10,16,50,64,100,200};
   for(unsigned slot=0;slot<2;slot++){
    unsigned er=slot?3u:1u,pr=slot?8u:6u,tr=slot?15u:14u;
    unsigned code=regs[er]>>4,voice=code&3u,kind=code>>2;
    if(voice && kind==3u){
     /* Sync-buzzer replaces any previous effect owned by this slot. */
     s->sid[slot].active=0;
     s->drum[slot].active=NULL;
     unsigned pre=regs[pr]>>5,tc=regs[tr];
     s->buzzer[slot].active=1;
     s->buzzer[slot].shape=regs[7u+voice]&15u;
     s->buzzer[slot].phase=0;
     s->buzzer[slot].step=((uint64_t)divs[pre]*tc*YM_RATE);
     /* One shared hardware envelope: later sync-buzzer slot wins. */
     s->buzzer[slot^1u].active=0;
    }
    if(voice && (kind==0u || kind==2u)){
     /* The slot has switched away from envelope-retrigger mode. */
     s->buzzer[slot].active=0;
     uint32_t pre=regs[pr]>>5,tc=regs[tr];
     s->sid[slot].active=1;
     s->sid[slot].voice=voice-1u;
     s->sid[slot].volume=regs[7u+voice]&15u;
     s->sid[slot].wave=kind==2u;
     s->sid[slot].step=((uint64_t)divs[pre]*tc*YM_RATE);
     s->sid[slot].phase=0;
     s->sid[slot].on=kind==2u?0:1;
     /* New effect owns its voice, regardless of the old effect type. */
     for(unsigned old=0;old<2;old++){
      if(s->drum[old].active && s->drum[old].voice==voice-1u)
       s->drum[old].active=NULL;
      if(old!=slot && s->sid[old].active && s->sid[old].voice==voice-1u)
       s->sid[old].active=0;
     }
     s->drum[slot].active=NULL;
    }
    if(voice && kind==1u){
     s->buzzer[slot].active=0;
     s->sid[slot].active=0;
     unsigned id=regs[7u+voice]&31u;
     unsigned pre=regs[pr]>>5,tc=regs[tr];
     s->drum[slot].active=s->drums[id];
     s->drum[slot].size=s->drum_sizes[id];
     s->drum[slot].voice=voice-1u;
     s->drum[slot].pos=0;s->drum[slot].phase=0;
     s->drum[slot].step=((uint64_t)divs[pre]*tc*YM_RATE);
     /* Preempt old effects on the same voice in either slot. */
     for(unsigned old=0;old<2;old++){
      if(old!=slot && s->drum[old].active && s->drum[old].voice==voice-1u)
       s->drum[old].active=NULL;
      if(s->sid[old].active && s->sid[old].voice==voice-1u)
       s->sid[old].active=0;
     }
    }
   }
  }
  /* YM2 Mad Max external digi-drum trigger: optional per-decoder bank.
   * R10 bit 7 identifies a trigger, low 7 bits the sample number.
   * R12 is the MFP divisor. Only update at the start of a YM frame. */
  if(s->is_ym2 && s->ym2_bank && frame!=s->last_frame &&
     (regs[10]&0x80u)){
   unsigned id=regs[10]&0x7fu;
   s->ym2_active=NULL;
   s->ym2_index=0;s->ym2_phase=0;
   if(id<s->ym2_bank_count && regs[12] &&
      s->ym2_bank[id].data && s->ym2_bank[id].size){
    s->ym2_active=s->ym2_bank[id].data;
    s->ym2_size=s->ym2_bank[id].size;
    s->ym2_timer=regs[12];
   }
  }
  if(s->is_ym2 && s->ym2_bank && (regs[10]&0x80u))
   regs[10]=0; /* Trigger marker is not a PSG volume. */
  /* ST-Sound YmMusic.cpp, player(), YM_V2: a non-FF R13 marker
   * requests the Mad Max envelope shape 10 with R12 forced to zero.
   * It is not a generic YM3-style envelope-shape write. */
  if(s->is_ym2 && regs[13]!=0xffu){
   regs[12]=0;
   regs[13]=10;
  }
  uint32_t noise_period=regs[6]&31u;
  if(!noise_period)noise_period=1;
  uint32_t noise_bit=sauti_ym_noise_step_asm(&s->noise_phase,&s->noise_rng,s->noise_step,noise_period);
  /* YM dumps provide register snapshots, not precise CPU write events. */
  /* R13 = 0xFF means NO REGISTER WRITE for this VBL.
   * Every explicit 0..15 write retriggers the envelope, even when
   * the shape is unchanged. Only apply writes at the VBL boundary.
   */
  if(frame!=s->last_frame){
   s->last_frame=frame;
   if(regs[13]!=0xffu){
    s->env_shape=regs[13]&15u;
    sauti_ym_env_init_asm(&s->env,s->env_shape);
   }
  }
  uint32_t period=(uint32_t)regs[11]|((uint32_t)regs[12]<<8);
  /* Timer-driven envelope restarts occur at sample boundaries, before
   * advancing the PSG envelope for this output sample. */
  for(unsigned slot=0;slot<2;slot++){
   if(s->buzzer[slot].active){
    if(mfp_advance(&s->buzzer[slot].phase,s->buzzer[slot].step)){
     sauti_ym_env_init_asm(&s->env,s->buzzer[slot].shape);
    }
   }
  }
  uint32_t level=sauti_ym_env_step_asm(&s->env,s->step,period);
  for(unsigned slot=0;slot<2;slot++){
   if(s->drum[slot].active && s->drum[slot].pos<s->drum[slot].size)
    regs[8u+s->drum[slot].voice]=0;
  }
  for(unsigned slot=0;slot<2;slot++){
   if(s->sid[slot].active){
    uint32_t v=s->sid[slot].voice;
    if(s->sid[slot].wave){
     /* Experimental 8-step rectified sine, one step per MFP tick. */
     static const uint8_t shape[8]={0,5,11,15,15,11,5,0};
     regs[8u+v]=(uint8_t)((s->sid[slot].volume*shape[s->sid[slot].on&7u]+7u)/15u);
    }else regs[8u+v]=s->sid[slot].on?(uint8_t)s->sid[slot].volume:0;
   }
  }
  /* Mad Max PCM replaces voice C while its sample is active; without
   * muting the PSG volume, the same voice is heard twice. */
  if(s->is_ym2 && s->ym2_active)regs[10]=0;
  sauti_ym_tone_step_asm(regs,s->phase,s->step,&samples[i],noise_bit,level);
  for(unsigned slot=0;slot<2;slot++){
   if(s->sid[slot].active){
    uint32_t ticks=mfp_advance(&s->sid[slot].phase,s->sid[slot].step);
    if(s->sid[slot].wave)s->sid[slot].on=(s->sid[slot].on+ticks)&7u;
    else s->sid[slot].on^=(int)(ticks&1u);
   }
  }
  for(unsigned slot=0;slot<2;slot++){
   if(s->drum[slot].active && s->drum[slot].pos<s->drum[slot].size){
    samples[i]+=sauti_ym_drum_sample_asm(s->drum[slot].active+s->drum[slot].pos,s->drum_attr);
    s->drum[slot].pos+=mfp_advance(&s->drum[slot].phase,s->drum[slot].step);
    if(s->drum[slot].pos>=s->drum[slot].size)s->drum[slot].active=NULL;
   }
  }
  if(s->ym2_active){
   /* The Mad Max sampler occupies channel C. Synthetic bank bytes are
    * unsigned 8-bit PCM. MFP_CLOCK/timer determines the sample rate. */
   samples[i]+=((float)s->ym2_active[s->ym2_index]-128.0f)/(128.0f*3.0f);
   s->ym2_phase+=2457600u;
   uint64_t divisor=(uint64_t)s->ym2_timer*YM_RATE;
   uint64_t ticks=s->ym2_phase/divisor;
   s->ym2_phase%=divisor;
   if(ticks >= s->ym2_size-s->ym2_index){
    s->ym2_active=NULL;
   }else s->ym2_index+=(uint32_t)ticks;
  }
#ifdef SAUTI_YM_LISTEN_REFERENCE
  samples[i]=ym_reference_listen_filter(s,samples[i]);
#endif
 }
 s->pos+=count;
 return count;
}
static int seek_ym(void *ctx,uint64_t frame){
 Ym *s=ctx;
 if(!s||frame>s->total)return 0;
 /* Restore the closest previously visited PCM state at or before the
  * target, including the current playback position. This preserves
  * sample-exact results and avoids repeatedly decoding from zero. */
 const Ym *best=NULL;
 uint64_t best_pos=0;
 if(s->pos<=frame){best=s;best_pos=s->pos;}
 for(unsigned k=0;k<8;k++){
  const Ym *candidate=s->checkpoints[k];
  if(candidate && candidate->pos<=frame && (!best || candidate->pos>best_pos)){
   best=candidate;best_pos=candidate->pos;
  }
 }
 if(best && best!=s)memcpy(s,best,YM_STATE_BYTES);
 if(!best){
 memset(s->phase,0,sizeof(s->phase));
 s->noise_phase=0;s->noise_rng=1;
 s->env_shape=UINT32_MAX;s->last_frame=UINT32_MAX;
 memset(&s->env,0,sizeof(s->env));
#ifdef SAUTI_YM_LISTEN_REFERENCE
 memset(s->dc_history,0,sizeof(s->dc_history));
 s->dc_total=s->lp1=s->lp2=0;s->dc_pos=0;
#endif
 s->ym2_active=NULL;s->ym2_index=0;s->ym2_phase=0;
 memset(s->drum,0,sizeof(s->drum));
 memset(s->sid,0,sizeof(s->sid));
 memset(s->buzzer,0,sizeof(s->buzzer));
 s->pos=0;
 }
 float discarded[2048];
 while(s->pos<frame){
  uint64_t remain=frame-s->pos;
  size_t want=remain<2048u?(size_t)remain:2048u;
  if(read_ym(s,discarded,want)!=want)return 0;
 }
 checkpoint_save(s);
 return 1;
}
static void close_ym(void *p){
 Ym *s=p;
 if(!s)return;
 for(unsigned i=0;i<8;i++)free(s->checkpoints[i]);
 free(s);
}
const SautiDecoderOps sauti_decoder_ym_ops={"atari-ym2149-experimental",probe,open_ym,read_ym,seek_ym,close_ym};
 
int sauti_decoder_ym_loop_start(const SautiDecoder *decoder,uint64_t *frame){
 if(!decoder||!frame||decoder->ops!=&sauti_decoder_ym_ops||!decoder->state)return 0;
 const Ym *s=decoder->state;
 if(!s->has_loop||s->loop_frame>=s->total)return 0;
 *frame=s->loop_frame;
 return 1;
}

int sauti_decoder_ym2_set_bank(SautiDecoder *decoder,
                              const SautiYm2Sample *bank,size_t count){
 if(!decoder||decoder->ops!=&sauti_decoder_ym_ops||!decoder->state ||
    (count && !bank)||count>128u)return 0;
 Ym *s=decoder->state;
 if(!s->is_ym2 || s->pos!=0)return 0;
 for(size_t i=0;i<count;i++)
  if((bank[i].size && !bank[i].data))return 0;
 s->ym2_bank=bank;s->ym2_bank_count=count;
 s->ym2_active=NULL;s->ym2_index=0;s->ym2_phase=0;
 for(unsigned i=0;i<8;i++){free(s->checkpoints[i]);s->checkpoints[i]=NULL;}
 s->next_checkpoint=0;
 return 1;
}
