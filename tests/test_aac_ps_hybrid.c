#include "sauti_aac_ps_hybrid.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void){
    SautiAacPsFrame frame={0};
    SautiAacPsMappedFrame mapped={0};
    SautiAacPsHybridSchedule out={0},saved;

    frame.resolved_num_env=3u;
    frame.border_position[0]=5u;
    frame.border_position[1]=17u;
    frame.border_position[2]=31u;
    mapped.bands=20u;
    mapped.env_count=3u;
    assert(sauti_aac_ps_hybrid_schedule_build(&frame,&mapped,&out));
    assert(out.parameter_bands==20u&&out.hybrid_bands==71u&&out.env_count==3u);
    assert(out.slot_to_env[0]==0u&&out.slot_to_env[5]==0u&&
           out.slot_to_env[6]==1u&&out.slot_to_env[17]==1u&&
           out.slot_to_env[18]==2u&&out.slot_to_env[31]==2u);
    assert(out.hybrid_to_parameter[0]==1u);
    assert(out.hybrid_to_parameter[1]==0u);
    assert(out.hybrid_to_parameter[40]==18u);
    assert(out.hybrid_to_parameter[70]==19u);
    assert(out.phase_imaginary_sign[0]==-1);
    assert(out.phase_imaginary_sign[1]==-1);
    assert(out.phase_imaginary_sign[2]==1);

    frame=(SautiAacPsFrame){0};
    frame.resolved_num_env=1u;
    frame.border_position[0]=31u;
    mapped=(SautiAacPsMappedFrame){0};
    mapped.bands=34u;
    mapped.env_count=1u;
    assert(sauti_aac_ps_hybrid_schedule_build(&frame,&mapped,&out));
    assert(out.parameter_bands==34u&&out.hybrid_bands==91u&&
           out.hybrid_to_parameter[0]==0u&&out.hybrid_to_parameter[9]==2u&&
           out.hybrid_to_parameter[90]==33u&&out.phase_imaginary_sign[8]==1&&
           out.phase_imaginary_sign[9]==-1&&out.phase_imaginary_sign[13]==-1&&
           out.phase_imaginary_sign[14]==1);

    saved=out;
    frame.border_position[0]=30u;
    assert(!sauti_aac_ps_hybrid_schedule_build(&frame,&mapped,&out));
    assert(memcmp(&out,&saved,sizeof(out))==0);
    puts("PASS AAC PS hybrid schedule: 20/34 bands, slots and phase routing");
    return 0;
}
