/* Reuse the COMPLETE SBR-30 bitstream fixture: both this test and
 * SBR-30 begin with a genuine ADTS SCE + ID_FIL + ID_END sequence.
 * The included existing tests remain available through a renamed
 * test main, but only SBR-31 tests execute from this unit. */
#define main sbr30_fixture_main
#include "test_aac_sbr_adts_mono.c"
#undef main

#include "sauti_aac_he_adts_qmf.h"
#include "sauti_aac_tables.h"

static int full_patch_coverage(const SautiAacSbrTables *tab,
                               const SautiAacSbrPatches *patch){
    for(unsigned k=tab->kx;k<tab->kx+tab->m;k++)
        if(patch->source_for_target[k]<0)return 0;
    return 1;
}
static void pick_complete_header(SautiAacSbrHeader *h,
                                 SautiAacSbrTables *tab){
    SautiAacSbrHeader z={0};
    z.amp_res=1u;z.freq_scale=0u;z.alter_scale=1u;
    z.noise_bands=2u;z.limiter_bands=2u;z.limiter_gains=2u;
    z.interpol_freq=1u;z.smoothing_mode=0u;
    z.extra_1=1u;z.extra_2=1u;
    for(unsigned st=0;st<16u;st++)
        for(unsigned sp=0;sp<16u;sp++){
            SautiAacSbrLimits l;
            SautiAacSbrMaster m;
            SautiAacSbrPatches p;
            if(!sauti_aac_sbr_limits_compute(48000u,st,sp,&l) ||
               !sauti_aac_sbr_master_linear(l.k0,l.k2,1u,0u,&m) ||
               !sauti_aac_sbr_tables_build(&m,2u,tab) ||
               !sauti_aac_sbr_patch_build(48000u,&m,tab,&p) ||
               tab->n_high<3u || tab->n_high>20u ||
               tab->n_noise>4u || !full_patch_coverage(tab,&p))
                continue;
            z.start_freq=st;z.stop_freq=sp;*h=z;return;
        }
    assert(!"No fully covered SBR patch configuration found");
}
static void codeword(Bits *b,unsigned book,unsigned symbol){
    uint32_t code=0;uint8_t width=0;
    assert(sauti_aac_standard_codeword(book,symbol,&code,&width));
    put(b,width,code);
}
static void sce_nonzero(Bits *b){
    put(b,3u,0u); /* ID_SCE */
    put(b,4u,0u); /* instance tag */
    put(b,8u,100u); /* global gain */
    put(b,1u,0u);put(b,2u,0u);put(b,1u,0u);
    put(b,6u,1u);put(b,1u,0u); /* one long-window SFB */
    put(b,4u,1u);put(b,5u,1u); /* signed quad codebook 1, one SFB */
    codeword(b,0u,60u); /* zero scalefactor delta */
    put(b,1u,0u);put(b,1u,0u);put(b,1u,0u);
    codeword(b,1u,0u); /* nonzero quantized spectral quad */
}
static size_t frame_tone(Bits *raw,uint8_t output[1024],
                         const Bits *payload){
    *raw=(Bits){0};
    sce_nonzero(raw);
    fil(raw,payload,NULL);
    put(raw,3u,7u); /* ID_END */
    pad(raw);
    return adts(output,raw,1u);
}
static void independent_chain(const uint8_t *bytes,size_t length,
                              SautiAacHeAdtsQmfState *state,
                              float pcm[2048],
                              SautiAacSbrLimiterReport *report){
    SautiAacSbrPayloadFrame payload;
    SautiAacReason reason;
    float core[1024];
    assert(sauti_aac_he_adts_mono_decode_ex(
        bytes,length,&state->adts,core,&payload,&reason)==1);
    SautiAacSbrHfJointConfig hf;
    assert(sauti_aac_sbr_payload_make_hf(&payload,&hf));
    SautiAacSbrQmfFrameConfig config={
        .hf=&hf,.attack=payload.attack,
        .bs_smoothing_mode=payload.bs_smoothing_mode
    };
    assert(sauti_aac_sbr_qmf_frame_process(
        &config,core,1024u,pcm,2048u,&state->qmf,report));
    state->frames++;
}
static void test_two_real_adts_frames_vs_independent_chain(void){
    SautiAacSbrHeader header;
    SautiAacSbrTables tab;
    pick_complete_header(&header,&tab);
    Bits payload,raw;
    uint8_t bytes[1024];
    SautiAacHeAdtsQmfState channel={0},separate={0};
    assert(sauti_aac_he_adts_qmf_init(&channel));
    assert(sauti_aac_he_adts_qmf_init(&separate));
    SautiAacSbrLimiterReport report,separate_report;
    SautiAacReason reason=SAUTI_AAC_REASON_INVALID_ADTS;
    float result[2048+2u],reference[2048];
    memset(result,0x73,sizeof(result));
    float guard[2];memcpy(guard,result+2048u,sizeof(guard));

    for(unsigned frame_no=0;frame_no<2u;frame_no++){
        sbr_bits(&payload,&header,&tab,
                 frame_no==0u?1u:0u,
                 frame_no==0u?0u:1u);
        const size_t len=frame_tone(&raw,bytes,&payload);
        assert(sauti_aac_he_adts_qmf_decode_ex(
            bytes,len,&channel,result,&report,&reason)==1);
        assert(reason==SAUTI_AAC_REASON_OK);
        independent_chain(bytes,len,&separate,reference,&separate_report);
        assert(memcmp(result,reference,sizeof(reference))==0);
        assert(memcmp(&report,&separate_report,sizeof(report))==0);
        assert(memcmp(&channel,&separate,sizeof(channel))==0);
        assert(memcmp(result+2048u,guard,sizeof(guard))==0);
        assert(channel.frames==frame_no+1u &&
               channel.adts.frames==frame_no+1u &&
               channel.qmf.frames==frame_no+1u);
        assert(channel.qmf.analysis.samples_consumed==
               (uint64_t)(frame_no+1u)*1024u);
        assert(channel.qmf.hf.noise_rom.emitted==
               (uint64_t)(frame_no+1u)*32u*tab.m);
        double power=0;
        for(unsigned i=0;i<2048u;i++){
            assert(isfinite(result[i]));
            power+=(double)result[i]*result[i];
        }
        assert(power>0.0);
    }
    printf("SBR-31: two full ADTS 24 kHz non-silent SCE+FIL frames -> 2048 native QMF samples at 48 kHz, exact manual chain PASS\n");
}

static void test_late_failure_rolls_back_every_state(void){
    SautiAacSbrHeader header;
    SautiAacSbrTables tab;
    pick_complete_header(&header,&tab);
    Bits payload,raw;
    uint8_t bytes[1024];
    sbr_bits(&payload,&header,&tab,1u,0u);
    size_t size=frame_tone(&raw,bytes,&payload);
    SautiAacHeAdtsQmfState state={0};
    assert(sauti_aac_he_adts_qmf_init(&state));
    SautiAacReason reason;
    float result[2048],before_pcm[2048];
    memset(result,0x35,sizeof(result));
    memcpy(before_pcm,result,sizeof(result));
    SautiAacSbrLimiterReport stats,before_stats;
    memset(&stats,0x96,sizeof(stats));before_stats=stats;
    SautiAacHeAdtsQmfState previous=state;
#define FAIL() do { \
    assert(sauti_aac_he_adts_qmf_decode_ex( \
        bytes,size,&state,result,&stats,&reason)!=1); \
    assert(memcmp(&state,&previous,sizeof(previous))==0); \
    assert(memcmp(result,before_pcm,sizeof(result))==0); \
    assert(memcmp(&stats,&before_stats,sizeof(stats))==0); \
} while(0)
    const uint8_t saved=bytes[1];
    bytes[1]&=(uint8_t)~1u;FAIL();bytes[1]=saved; /* CRC unsupported */
    const uint8_t stereo=bytes[3];
    bytes[3]|=0x80u;FAIL();bytes[3]=stereo;
    /* Valid AAC-LC silence + SBR with positive target energy cannot
     * generate a correct HF source with current SBR-16 limiter.
     * A negative test ensures the ENTIRE state rolls back rather than
     * publishing the decoded core PCM plus fake high-band output. */
    size_t silent_size=frame(&raw,bytes,&payload,0u,NULL);
    size=silent_size;
    FAIL();
    size=frame_tone(&raw,bytes,&payload);

    state.qmf.synthesis.ready=1u;state.qmf.synthesis.head=10u;
    previous=state;FAIL(); /* QMF fails AFTER ADTS core+SBR parsed */
    state.qmf.synthesis.ready=0u;state.qmf.synthesis.head=0u;
    previous=state;

    state.frames=UINT64_MAX;previous=state;FAIL();
    state.frames=0u;previous=state;
    /* Skip one byte of an otherwise valid ADTS frame. */
    assert(sauti_aac_he_adts_qmf_decode_ex(
        bytes,size-1u,&state,result,&stats,&reason)!=1);
    assert(memcmp(&state,&previous,sizeof(previous))==0);
    assert(memcmp(result,before_pcm,sizeof(result))==0);
    assert(memcmp(&stats,&before_stats,sizeof(stats))==0);
#undef FAIL
    puts("SBR-31: unpatched/no-source HF, malformed ADTS and late QMF failure rollback all AAC/SBR/QMF histories");
}

/* Create an ADTS file for SBR-32 CLI I/O smoke testing. This
 * generated file is NOT a third-party HE-AAC conformance fixture:
 * it is the same independently serialized SBR-31 unit-test syntax. */
static int emit_smoke_adts(const char *path){
    SautiAacSbrHeader header;
    SautiAacSbrTables tab;
    pick_complete_header(&header,&tab);
    FILE *f=fopen(path,"wb");
    if(!f){perror(path);return 2;}
    for(unsigned frame_no=0;frame_no<2u;frame_no++){
        Bits payload,raw;
        uint8_t bytes[1024];
        sbr_bits(&payload,&header,&tab,
                 frame_no==0u?1u:0u,
                 frame_no==0u?0u:1u);
        const size_t length=frame_tone(&raw,bytes,&payload);
        if(fwrite(bytes,1u,length,f)!=length){
            perror(path);
            fclose(f);
            return 2;
        }
    }
    if(fclose(f)!=0){perror(path);return 2;}
    return 0;
}
int main(int argc,char **argv){
    if(argc==3 && strcmp(argv[1],"--emit")==0)
        return emit_smoke_adts(argv[2]);
    if(argc!=1){
        fprintf(stderr,"Usage: %s [--emit test_fixture.aac]\n",argv[0]);
        return 2;
    }
    test_two_real_adts_frames_vs_independent_chain();
    test_late_failure_rolls_back_every_state();
    puts("PASS HE-AAC SBR-31 actual ADTS SCE+SBR to 2048 f32 QMF samples (experimental full chain)");
    return 0;
}
