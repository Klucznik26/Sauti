#include "sauti_aac_ps_qmf_high.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void){
    SautiAacPsFrame frame={0};
    SautiAacPsMappedFrame mapped={0};
    SautiAacPsHybridSchedule schedule={0};
    SautiAacSbrComplex qmf[38u*64u]={0};
    SautiAacSbrComplex out[32u*61u+1u];
    const SautiAacSbrComplex sentinel={-999.0f,77.0f};
    for(unsigned s=0;s<38u;s++)for(unsigned b=0;b<64u;b++){
        qmf[s*64u+b].re=(float)(s*100u+b);
        qmf[s*64u+b].im=-(float)(s*100u+b);
    }
    frame.resolved_num_env=1u;frame.border_position[0]=31u;
    for(unsigned mode=0;mode<2u;mode++){
        mapped.bands=mode?34u:20u;mapped.env_count=1u;
        assert(sauti_aac_ps_hybrid_schedule_build(&frame,&mapped,&schedule));
        const unsigned first=mode?5u:3u,bands=64u-first;
        for(unsigned i=0;i<32u*61u+1u;i++)out[i]=sentinel;
        assert(sauti_aac_ps_qmf_high_extract(&schedule,qmf,38u*64u,out,32u*bands));
        for(unsigned s=0;s<32u;s++)for(unsigned b=0;b<bands;b++){
            const SautiAacSbrComplex expected=qmf[s*64u+first+b];
            assert(out[s*bands+b].re==expected.re);
            assert(out[s*bands+b].im==expected.im);
        }
        assert(out[32u*bands].re==sentinel.re);
        assert(!sauti_aac_ps_qmf_high_extract(&schedule,qmf,38u*64u-1u,out,32u*bands));
        assert(!sauti_aac_ps_qmf_high_extract(&schedule,qmf,38u*64u,out,32u*bands-1u));
        assert(out[0].re==qmf[first].re);
        SautiAacSbrComplex saved=qmf[first];
        qmf[first].re=NAN;
        assert(!sauti_aac_ps_qmf_high_extract(&schedule,qmf,38u*64u,out,32u*bands));
        assert(out[0].re==(float)first);
        qmf[first]=saved;
        const uint8_t prev=schedule.slot_to_env[0];
        schedule.slot_to_env[0]=1u;
        assert(!sauti_aac_ps_qmf_high_extract(&schedule,qmf,38u*64u,out,32u*bands));
        schedule.slot_to_env[0]=prev;
    }
    puts("PASS AAC PS-1a: 20/34 high QMF, direct slot alignment, failure atomicity");
    return 0;
}
