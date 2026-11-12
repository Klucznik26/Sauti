#include "sauti_aac_he_adts_ps.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Reuse the pre-existing real ADTS/SBR bit-writer as a fixture.
 * It emits an actual AAC-LC SCE + full SBR FIL; its PS flag is
 * absent, so this strict PS adapter must reject the whole frame. */
#define main sauti_existing_mono_fixture_main
#include "test_aac_sbr_adts_mono.c"
#undef main

/* A genuine ADTS SCE + entropy-coded SBR payload with standardized
 * EXTENSION_ID_PS side data. No PS structs are injected into the decoder.
 * The first SBR frame has a header; following frames inherit it.
 * The SCE is silent, but the SBR noise-floor syntax is nonzero. */
static Bits append_ps(const Bits *base,size_t content_bits){
    assert(content_bits>4u&&content_bits<=base->bit);
    Bits extended={0};
    /* Replace the trailing bs_extended_data=0 with a bounded
     * three-byte extension containing PS id=2 and 10 IID bands. */
    for(size_t i=0;i<content_bits-1u;i++)
        put(&extended,1u,(base->bytes[i/8u]>>(7u-(unsigned)(i%8u)))&1u);
    put(&extended,1u,1u); /* bs_extended_data */
    put(&extended,4u,3u);put(&extended,2u,2u);
    put(&extended,1u,1u); /* ps_header_flag */
    put(&extended,1u,1u);put(&extended,3u,0u); /* enable_iid, mode0 */
    put(&extended,1u,0u);put(&extended,1u,0u); /* no ICC/ext */
    put(&extended,1u,0u);put(&extended,2u,1u); /* class0, one env */
    put(&extended,1u,0u);
    for(unsigned i=0;i<10u;i++)put(&extended,1u,0u);
    put(&extended,1u,0u); /* SBR extended_data fill bit */
    pad(&extended);
    return extended;
}
static void pick_ps_covered_header(SautiAacSbrHeader *h,
                                  SautiAacSbrTables *tab){
    SautiAacSbrHeader z={0};
    z.amp_res=1u;z.freq_scale=0u;z.alter_scale=1u;
    z.noise_bands=2u;z.limiter_bands=2u;z.limiter_gains=2u;
    z.interpol_freq=1u;z.smoothing_mode=0u;
    z.extra_1=1u;z.extra_2=1u;
    for(unsigned st=0;st<16u;st++)
        for(unsigned sp=0;sp<16u;sp++){
            SautiAacSbrLimits lim;
            SautiAacSbrMaster master;
            SautiAacSbrPatches patch;
            if(!sauti_aac_sbr_limits_compute(48000u,st,sp,&lim) ||
               !sauti_aac_sbr_master_linear(lim.k0,lim.k2,1u,0u,&master) ||
               !sauti_aac_sbr_tables_build(&master,2u,tab) ||
               !sauti_aac_sbr_patch_build(48000u,&master,tab,&patch) ||
               tab->n_high<3u||tab->n_high>20u||tab->n_noise>4u)
                continue;
            unsigned complete=1u;
            for(unsigned k=tab->kx;k<tab->kx+tab->m;k++)
                if(patch.source_for_target[k]<0)complete=0u;
            if(!complete)continue;
            z.start_freq=st;z.stop_freq=sp;*h=z;return;
        }
    assert(!"no SBR patch coverage fixture");
}
static void test_positive_adts_ps_stream(void){
    SautiAacSbrHeader h;
    SautiAacSbrTables t;
    pick_ps_covered_header(&h,&t);
    /* A syntactically complete PS extension must reach the real SBR
     * parser, the real AAC-LC and HF QMF stages, and then the PS FIFO. */
    SautiAacHeAdtsPsState state={0};
    assert(sauti_aac_he_adts_ps_init(&state));
    SautiAacReason reason=SAUTI_AAC_REASON_INVALID_ADTS;
    SautiAacSbrLimiterReport report={0};
    float pcm[4096u+1u];
    unsigned produced=77u;
    for(unsigned frame_no=0u;frame_no<3u;frame_no++){
        Bits original,raw;
        size_t used=0u;
        sbr_bits(&original,&h,&t,frame_no==0u,frame_no!=0u,&used);
        Bits ps=append_ps(&original,used);
        uint8_t bytes[1024u];
        const size_t n=frame(&raw,bytes,&ps,0u,NULL);
        /* Independent confirmation: the generated ADTS contains
         * a real SBR/PS extension, not injected synthetic PS data. */
        SautiAacHeAdtsMonoState parser={0};
        assert(sauti_aac_he_adts_mono_init(&parser));
        /* Standalone decode is valid on frame zero (new SBR header).
         * Subsequent frames rely on the real persistent driver. */
        if(frame_no==0u){
            float core[1024u];
            SautiAacSbrPayloadFrame p={0};
            assert(sauti_aac_he_adts_mono_decode_ex(
                      bytes,n,&parser,core,&p,&reason)==1);
            assert(p.has_ps==1u && p.ps.header.enable_iid==1u &&
                   p.ps.iid_count==10u);
        }
        for(size_t i=0;i<4097u;i++)pcm[i]=99.0f;
        produced=77u;
        const int status=sauti_aac_he_adts_ps_decode_ex(
            bytes,n,&state,pcm,4096u,&produced,&report,&reason);
        if(status!=1){
            fprintf(stderr,"PS-4c3 frame=%u status=%d reason=%d\\n",
                    frame_no,status,(int)reason);
        }
        assert(status==1);
        assert(produced==(frame_no==0u?0u:1u));
        assert(pcm[4096u]==99.0f);
        if(frame_no==0u){
            for(unsigned i=0;i<4096u;i++)assert(pcm[i]==99.0f);
        }else{
            for(unsigned i=0;i<4096u;i++)assert(isfinite(pcm[i]));
        }
        assert(state.frames==frame_no+1u);
        assert(state.ps.received_frames==frame_no+1u);
        assert(state.ps.bridge.frames==frame_no);
    }
    /* Broken next ADTS must leave all warm histories/outputs intact. */
    const SautiAacHeAdtsPsState saved=state;
    const SautiAacSbrLimiterReport saved_stats=report;
    const unsigned saved_produced=produced;
    float old_pcm[4097u];
    memcpy(old_pcm,pcm,sizeof(old_pcm));
    uint8_t invalid[8u]={0};
    const int rejected=sauti_aac_he_adts_ps_decode_ex(
        invalid,sizeof(invalid),&state,pcm,4096u,
        &produced,&report,&reason);
    assert(rejected!=1);
    assert(memcmp(&state,&saved,sizeof(state))==0);
    assert(memcmp(&report,&saved_stats,sizeof(report))==0);
    assert(memcmp(pcm,old_pcm,sizeof(old_pcm))==0);
    assert(produced==saved_produced);
    assert(state.ps.received_frames==3u &&
           state.ps.bridge.frames==2u && state.ps.pending==1u);
    puts("PASS AAC PS-4c3: generated real ADTS SCE+SBR+PS syntax, three frames, delayed stereo PCM");
}

/* PS-5a CLI smoke fixture: three complete SCE/SBR/PS ADTS
 * frames from the independently serialized PS-4c3 bit writer.
 * This is NOT an external-encoder conformance fixture. */
static int emit_ps_smoke(const char *path){
    SautiAacSbrHeader h;
    SautiAacSbrTables tab;
    pick_ps_covered_header(&h,&tab);
    FILE *out=fopen(path,"wb");
    if(!out){perror(path);return 2;}
    for(unsigned i=0;i<3u;i++){
        Bits base,raw;
        size_t content_bits=0u;
        sbr_bits(&base,&h,&tab,i==0u,i!=0u,&content_bits);
        const Bits ps=append_ps(&base,content_bits);
        uint8_t adts_bytes[1024u];
        const size_t len=frame(&raw,adts_bytes,&ps,0u,NULL);
        if(fwrite(adts_bytes,1u,len,out)!=len){
            perror("write PS smoke");
            fclose(out);return 2;
        }
    }
    if(fclose(out)!=0){perror(path);return 2;}
    return 0;
}
int main(int argc,char **argv){
    if(argc==3&&strcmp(argv[1],"--emit-ps-smoke")==0)
        return emit_ps_smoke(argv[2]);
    if(argc!=1){fprintf(stderr,"Usage: %s [--emit-ps-smoke output.adts]\\n",argv[0]);return 2;}
    SautiAacSbrHeader header;
    SautiAacSbrTables tables;
    find_header(&header,&tables);
    Bits payload,raw;
    sbr_bits(&payload,&header,&tables,1u,0u,NULL);
    uint8_t bytes[1024u];
    const size_t count=frame(&raw,bytes,&payload,0u,NULL);

    SautiAacHeAdtsPsState state={0};
    assert(sauti_aac_he_adts_ps_init(&state));
    const SautiAacHeAdtsPsState original=state;
    SautiAacSbrLimiterReport report,saved_report;
    memset(&report,0x59,sizeof(report));
    saved_report=report;
    float pcm[4096u+1u],saved_pcm[4096u+1u];
    for(size_t i=0;i<4097u;i++)pcm[i]=(-13.0f);
    memcpy(saved_pcm,pcm,sizeof(pcm));
    unsigned produced=81u;
    SautiAacReason reason=SAUTI_AAC_REASON_OK;
    assert(sauti_aac_he_adts_ps_decode_ex(
        bytes,count,&state,pcm,4096u,&produced,&report,&reason)==-1);
    assert(reason==SAUTI_AAC_REASON_UNSUPPORTED_FIL);
    assert(memcmp(&state,&original,sizeof(state))==0);
    assert(memcmp(&report,&saved_report,sizeof(report))==0);
    assert(memcmp(pcm,saved_pcm,sizeof(pcm))==0);
    assert(produced==81u);
    assert(sauti_aac_he_adts_ps_decode_ex(
        bytes,count-1u,&state,pcm,4096u,&produced,&report,&reason)!=1);
    assert(memcmp(&state,&original,sizeof(state))==0);
    assert(memcmp(&report,&saved_report,sizeof(report))==0);
    assert(memcmp(pcm,saved_pcm,sizeof(pcm))==0);
    assert(produced==81u);
    assert(sauti_aac_he_adts_ps_decode_ex(
        bytes,count,&state,pcm,4095u,&produced,&report,&reason)==0);
    assert(memcmp(&state,&original,sizeof(state))==0);
    assert(memcmp(&report,&saved_report,sizeof(report))==0);
    assert(produced==81u);
    state.frames=1u;
    const SautiAacHeAdtsPsState bad=state;
    assert(sauti_aac_he_adts_ps_decode_ex(
        bytes,count,&state,pcm,4096u,&produced,&report,&reason)==0);
    assert(memcmp(&state,&bad,sizeof(state))==0);
    assert(memcmp(&report,&saved_report,sizeof(report))==0);
    assert(memcmp(pcm,saved_pcm,sizeof(pcm))==0);
    assert(produced==81u);
    test_positive_adts_ps_stream();
    puts("PASS AAC PS-4c2: genuine ADTS SCE/SBR without PS rejected atomically; truncation, sizes, history");
    return 0;
}
