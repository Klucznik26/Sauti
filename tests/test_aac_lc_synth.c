#include "sauti_aac_lc_synth.h"
#include "sauti_aac_imdct.h"
#include "sauti_aac_window.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void setup(unsigned sequence,unsigned shape,SautiAacIcsInfo *ics,
                  SautiAacSections *sections,SautiAacScaleFactors *sf,
                  int32_t q[1024]){
    memset(ics,0,sizeof(*ics));memset(sections,0,sizeof(*sections));
    memset(sf,0,sizeof(*sf));memset(q,0,1024*sizeof(int32_t));
    ics->window_sequence=sequence;
    ics->window_shape=shape;
    ics->window_count=sequence==2u?8u:1u;
    ics->group_count=sequence==2u?2u:1u;
    ics->group_length[0]=sequence==2u?3u:1u;
    if(sequence==2u)ics->group_length[1]=5u;
    ics->max_sfb=1;
    sections->group_count=ics->group_count;
    sections->max_sfb=1;
    for(unsigned g=0;g<ics->group_count;g++){
        SautiAacSection *s=&sections->sections[sections->section_count++];
        s->group=(uint8_t)g;s->codebook=1;s->first_sfb=0;s->end_sfb=1;
        sf->codebook[g][0]=1;sf->value[g][0]=100;
    }
    for(unsigned w=0;w<ics->window_count;w++){
        q[(sequence==2u?128u:1024u)*w]=1;
        q[(sequence==2u?128u:1024u)*w+1u]=-1;
    }
}
static void reference(const SautiAacIcsInfo *ics,
                      const SautiAacSections *sections,
                      const SautiAacScaleFactors *sf,const int32_t q[1024],
                      const SautiAacLcSynthesis *state,float result[1024],
                      float history[1024]){
    float time[2048],spec[1024];
    assert(sauti_aac_lc_dequantize(q,4,ics,sections,sf,spec));
    if(ics->window_sequence==2u){
        for(unsigned i=0;i<8;i++)
            assert(sauti_aac_imdct(spec+128u*i,128u,time+256u*i));
    }else assert(sauti_aac_imdct(spec,1024u,time));
    assert(sauti_aac_window_synth(ics->window_sequence,
                                 state->previous_shape,ics->window_shape,
                                 time,state->overlap,result,history));
}
static int close_enough(float a,float b){
    return fabsf(a-b)<=0.0001f+fabsf(b)*0.00001f;
}
int main(void){
    SautiAacLcSynthesis state={0};
    assert(!sauti_aac_lc_synthesis_init(NULL,0));
    assert(!sauti_aac_lc_synthesis_init(&state,2));
    assert(!sauti_aac_lc_synthesis_frame(&state,4,NULL,NULL,NULL,NULL,NULL));
    assert(sauti_aac_lc_synthesis_init(&state,0));
    const unsigned sequences[]={0,1,2,3,0};
    const unsigned shapes[]={0,1,1,0,0};
    for(unsigned frame=0;frame<5;frame++){
        SautiAacIcsInfo ics;
        SautiAacSections sections;
        SautiAacScaleFactors sf;
        int32_t q[1024];
        setup(sequences[frame],shapes[frame],&ics,&sections,&sf,q);
        float expected[1024],expected_history[1024];
        reference(&ics,&sections,&sf,q,&state,expected,expected_history);
        float pcm[1024];
        for(unsigned i=0;i<1024;i++)pcm[i]=37;
        assert(sauti_aac_lc_synthesis_frame(&state,4,&ics,&sections,
                                            &sf,q,pcm));
        assert(state.initialized && state.previous_shape==shapes[frame]);
        assert(state.previous_sequence==sequences[frame]);
        for(unsigned i=0;i<1024;i++){
            assert(close_enough(pcm[i],expected[i]));
            assert(close_enough(state.overlap[i],expected_history[i]));
        }
        /* Errors must not alter output or persistent overlap state. */
        SautiAacLcSynthesis old=state;
        float saved[1024];memcpy(saved,pcm,sizeof(saved));
        q[0]=8192;
        assert(!sauti_aac_lc_synthesis_frame(&state,4,&ics,&sections,
                                             &sf,q,pcm));
        q[0]=1;
        assert(!sauti_aac_lc_synthesis_frame(&state,13,&ics,&sections,
                                             &sf,q,pcm));
        sections.sections[0].codebook=13;sf.codebook[0][0]=13;
        assert(!sauti_aac_lc_synthesis_frame(&state,4,&ics,&sections,
                                             &sf,q,pcm));
        assert(memcmp(&old,&state,sizeof(old))==0);
        assert(memcmp(saved,pcm,sizeof(saved))==0);
    }
    /* Starting a short frame without LONG_START must be rejected. */
    assert(sauti_aac_lc_synthesis_init(&state,0));
    SautiAacIcsInfo ics;
    SautiAacSections sec;
    SautiAacScaleFactors sf;
    int32_t q[1024];float pcm[1024]={0};
    setup(2,0,&ics,&sec,&sf,q);
    SautiAacLcSynthesis prior=state;
    assert(!sauti_aac_lc_synthesis_frame(&state,4,&ics,&sec,&sf,q,pcm));
    assert(memcmp(&prior,&state,sizeof(state))==0);
    puts("PASS AAC-LC synthesis: dequant -> IMDCT -> window -> NASM overlap, state rollback");
    return 0;
}
