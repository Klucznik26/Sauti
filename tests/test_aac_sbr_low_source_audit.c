#include "sauti_aac_sbr_low_source_audit.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define N (32u*32u)
int main(void){
    SautiAacSbrComplex low[N]={0};
    SautiAacSbrLowSourceAuditState s={0};
    SautiAacSbrTemporalSource6Report r={
        .current_source_slots=26u,.missing_preroll=6u,.frame_index=0u
    };
    for(unsigned t=0;t<32u;t++)
        for(unsigned b=0;b<4u;b++){
            low[t*32u+b+2u].re=3.0f;
            low[t*32u+b+2u].im=4.0f;
        }
    assert(sauti_aac_sbr_low_source_audit_add(low,N,&r,&s));
    assert(s.frames==1u && s.count[0]==26u && s.adjacent_count[0]==25u);
    assert(s.energy[0]==650.0 && s.real_energy[0]==234.0 &&
           s.imag_energy[0]==416.0 && s.max_magnitude[0]==5.0);
    assert(s.adjacent_real[0]==625.0 && s.adjacent_imag[0]==0.0);
    assert(s.adjacent_norm_prev[0]==625.0 && s.adjacent_norm_next[0]==625.0);
    r.frame_index=1u;r.carried_from_previous=6u;r.missing_preroll=0u;
    for(unsigned t=0;t<32u;t++){
        low[t*32u+2u].re=(t&1u)?0.0f:1.0f;
        low[t*32u+2u].im=(t&1u)?1.0f:0.0f;
    }
    assert(sauti_aac_sbr_low_source_audit_add(low,N,&r,&s));
    assert(s.frames==2u && s.count[0]==58u && s.adjacent_count[0]==56u);
    assert(s.adjacent_real[0]==625.0 && s.adjacent_imag[0]==1.0);
    const SautiAacSbrLowSourceAuditState saved=s;
    r.frame_index=2u;low[0].re=NAN;
    assert(!sauti_aac_sbr_low_source_audit_add(low,N,&r,&s));
    assert(memcmp(&saved,&s,sizeof(s))==0);
    low[0].re=0.0f;
    assert(!sauti_aac_sbr_low_source_audit_add(low,N-1u,&r,&s));
    assert(memcmp(&saved,&s,sizeof(s))==0);
    r.frame_index=1u;
    assert(!sauti_aac_sbr_low_source_audit_add(low,N,&r,&s));
    assert(memcmp(&saved,&s,sizeof(s))==0);
    r.frame_index=2u;r.missing_preroll=6u;
    assert(!sauti_aac_sbr_low_source_audit_add(low,N,&r,&s));
    assert(memcmp(&saved,&s,sizeof(s))==0);
    puts("PASS SBR-54v LOW source magnitude, phase, preroll, replay and rollback (NOT FFmpeg/PCM)");
    return 0;
}
