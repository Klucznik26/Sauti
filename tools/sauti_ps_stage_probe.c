/* SBR-84 tool (not a product): feeds the PS input recorded by the instrumented FFmpeg (SAUTI_PS_FFMPEG_DUMP, "SPD1") through ONE Sauti PS stage at a
 * time and writes that stage's output for comparison.  usage: sauti_ps_stage_probe DUMP.bin STAGE OUT.bin
 *   STAGE 1: hybrid analysis (sauti_aac_ps_qmf_bank_analyze): OUT = per call: u32 call, then [32][71] complex floats, time-major.
 *   STAGE 3: stereo mixing: needs the ADTS file as 5th argument (its PS parameters; frame f belongs to call f+1); OUT = per call u32 call, [32][71] L then [32][71] R.
 *   STAGE 2: decorrelation (sauti_aac_ps_decorrelator_apply) fed with the RECORDED analysis output: same record layout. */
#include "sauti_aac_ps_qmf_bank.h"
#include "sauti_aac_ps_decorrelator.h"
#include "sauti_aac_ps_map.h"
#include "sauti_aac_ps_stereo.h"
#include "sauti_aac_ps_render.h"
#include "sauti_aac_he_adts_mono.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc,char **argv){
    if(argc<4)return 2;
    FILE *f=fopen(argv[1],"rb");
    if(!f){perror(argv[1]);return 2;}
    char magic[4];
    if(fread(magic,1,4,f)!=4 || memcmp(magic,"SPD1",4))return 2;
    FILE *out=fopen(argv[3],"wb");
    const int stage=atoi(argv[2]);
    static SautiAacPsQmfLowState low;
    static SautiAacPsDecorrelatorState dstate;
    static SautiAacPsStereoState sstate;
    static SautiAacPsRenderState rstate;
    memset(&sstate,0,sizeof(sstate));memset(&rstate,0,sizeof(rstate));
    static SautiAacHeAdtsMonoState core_state;
    static uint8_t *adts;size_t adts_n=0,adts_pos=0;
    static SautiAacSbrPayloadFrame payloads[2048];int npayloads=0;
    if(stage==3){
        if(argc<5)return 2;
        FILE *af=fopen(argv[4],"rb");
        if(!af)return 2;
        fseek(af,0,SEEK_END);adts_n=(size_t)ftell(af);fseek(af,0,SEEK_SET);adts=malloc(adts_n);
        if(!adts || fread(adts,1,adts_n,af)!=adts_n)return 2;
        fclose(af);
        sauti_aac_he_adts_mono_init(&core_state);
        while(adts_pos+7<=adts_n && npayloads<2048){
            const unsigned len=((unsigned)(adts[adts_pos+3]&3u)<<11)|((unsigned)adts[adts_pos+4]<<3)|((unsigned)adts[adts_pos+5]>>5);
            float c[1024];SautiAacReason r=0;
            if(sauti_aac_he_adts_mono_temporal_decode_ex(adts+adts_pos,adts_n-adts_pos,&core_state,c,&payloads[npayloads],&r)!=1)break;
            adts_pos+=len;npayloads++;
        }
        fprintf(stderr,"payloads %d\n",npayloads);
    }
    memset(&low,0,sizeof(low));memset(&dstate,0,sizeof(dstate));
    for(;;){
        unsigned h[2];
        if(fread(h,sizeof(h),1,f)!=1)break;
        const unsigned nr=h[1]?91u:71u;
        static float lin[2*38*64],hyb[91*32*2],dec[91*32*2],lo[91*32*2],ro[91*32*2],lout[2*38*64],rout[2*38*64];
        if(fread(lin,4,2*38*64,f)!=2*38*64 || fread(hyb,4,nr*32*2,f)!=nr*32*2 || fread(dec,4,nr*32*2,f)!=nr*32*2 ||
           fread(lo,4,nr*32*2,f)!=nr*32*2 || fread(ro,4,nr*32*2,f)!=nr*32*2 ||
           fread(lout,4,2*38*64,f)!=2*38*64 || fread(rout,4,2*38*64,f)!=2*38*64)break;
        if(stage==1){
            static SautiAacSbrComplex qmf[38*64],res[32*91];
            for(unsigned s=0;s<38;s++)for(unsigned k=0;k<64;k++){qmf[s*64+k].re=lin[s*64+k];qmf[s*64+k].im=lin[38*64+s*64+k];}
            SautiAacPsHybridSchedule sch;memset(&sch,0,sizeof(sch));
            sch.parameter_bands=h[1]?34u:20u;sch.hybrid_bands=nr;sch.env_count=1u;   /* analysis ignores the schedule values */
            if(!sauti_aac_ps_qmf_bank_analyze(&sch,qmf,38u*64u,&low,res,32u*nr)){fprintf(stderr,"call %u: analysis rejected\n",h[0]);break;}
            fwrite(&h[0],4,1,out);fwrite(res,sizeof(res[0]),32u*nr,out);
        }
        if(stage==2){
            static SautiAacSbrComplex in[32*91],res[32*91];
            for(unsigned b=0;b<nr;b++)for(unsigned t=0;t<32;t++){in[t*nr+b].re=hyb[(b*32+t)*2];in[t*nr+b].im=hyb[(b*32+t)*2+1];}
            SautiAacPsFrame fr;SautiAacPsMappedFrame mp;SautiAacPsHybridSchedule sch;
            memset(&fr,0,sizeof(fr));memset(&mp,0,sizeof(mp));
            fr.resolved_num_env=1u;fr.border_position[0]=31u;mp.env_count=1u;mp.bands=h[1]?34u:20u;
            if(!sauti_aac_ps_hybrid_schedule_build(&fr,&mp,&sch)){fprintf(stderr,"schedule\n");return 2;}
            if(!sauti_aac_ps_decorrelator_apply(&sch,in,32u*nr,&dstate,res,32u*nr)){fprintf(stderr,"call %u: decorrelation rejected\n",h[0]);break;}
            fwrite(&h[0],4,1,out);fwrite(res,sizeof(res[0]),32u*nr,out);
        }
        if(stage==3){
            if(h[0]==0u || (int)h[0]-1>=npayloads)continue;
            const SautiAacSbrPayloadFrame *pl=&payloads[h[0]-1u];
            if(!pl->has_ps){fprintf(stderr,"call %u: payload without PS\n",h[0]);continue;}
            static SautiAacPsRenderPlan plan;
            if(!sauti_aac_ps_render_plan_build(&pl->ps,&rstate,&plan)){fprintf(stderr,"call %u: render plan rejected\n",h[0]);break;}
            static SautiAacSbrComplex qmf[38*64],lres[32*91],rres[32*91];
            for(unsigned s2=0;s2<38;s2++)for(unsigned k=0;k<64;k++){qmf[s2*64+k].re=lin[s2*64+k];qmf[s2*64+k].im=lin[38*64+s2*64+k];}
            if(!sauti_aac_ps_stereo_hybrid_apply(&plan.schedule,&plan.complex_matrix,qmf,38u*64u,&sstate,lres,32u*nr,rres,32u*nr)){fprintf(stderr,"call %u: stereo rejected\n",h[0]);break;}
            fwrite(&h[0],4,1,out);fwrite(lres,sizeof(lres[0]),32u*nr,out);fwrite(rres,sizeof(rres[0]),32u*nr,out);
        }
    }
    fclose(out);fclose(f);
    return 0;
}
