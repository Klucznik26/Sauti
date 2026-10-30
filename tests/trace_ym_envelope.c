/* Isolated YM2149 envelope state trace (no DAC, filtering or player).
 * Record returned level and internal phase around the repeating cycle.
 * Use --shape N to inspect any of the 16 shapes.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {uint32_t phase,index,attack,alternate,hold,holding;} YmEnvelope;
extern void sauti_ym_env_init_asm(YmEnvelope *,uint32_t);
extern uint32_t sauti_ym_env_step_asm(YmEnvelope *,uint32_t,uint32_t);

int main(int argc,char **argv){
 unsigned shape=12,period=1,ticks=100;
 if(argc>1){char *end;unsigned long v=strtoul(argv[1],&end,0);if(*end||v>15){fprintf(stderr,"shape must be 0..15\n");return 2;}shape=(unsigned)v;}
 if(argc>2){char *end;unsigned long v=strtoul(argv[2],&end,0);if(*end||v<1||v>65535){fprintf(stderr,"period must be 1..65535\n");return 2;}period=(unsigned)v;}
 if(argc>3){char *end;unsigned long v=strtoul(argv[3],&end,0);if(*end||v<1||v>100000){fprintf(stderr,"ticks must be 1..100000\n");return 2;}ticks=(unsigned)v;}
 if(argc>4){fprintf(stderr,"usage: %s [shape=12] [period=1] [ticks=100]\n",argv[0]);return 2;}
 YmEnvelope env={0};
 sauti_ym_env_init_asm(&env,shape);
 printf("tick,level_before,level_returned,phase_q16,index,attack,alternate,hold,holding\n");
 for(unsigned t=0;t<ticks;t++){
  unsigned before=env.index^env.attack;
  /* Exactly one PSG envelope tick, expressed in the NASM Q16 units. */
  unsigned returned=sauti_ym_env_step_asm(&env,period*65536u,period);
  printf("%u,%u,%u,%u,%u,%u,%u,%u,%u\n",t,before,returned,
         env.phase,env.index,env.attack,env.alternate,env.hold,env.holding);
  if(returned>31||before>31){fprintf(stderr,"invalid envelope level\n");return 1;}
 }
 return 0;
}
