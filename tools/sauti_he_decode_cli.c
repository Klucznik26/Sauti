/* SBR-82 tool (not a product): decodes a mono HE-AAC ADTS file through the new channel decoder and writes f32le PCM (mono, 2x core rate).
 * Usage: sauti_he_decode_cli FILE.aac OUT.f32 [MAX_FRAMES].  SAUTI_SBR82_GRID=path additionally dumps the assembled 32x64 QMF grid of every
 * frame (record: u32 frame index + 32*64 complex float, header "SBQ1"), for comparison with another decoder's grid. */
#include "sauti_aac_he_adts_mono.h"
#include "sauti_aac_sbr_channel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc,char **argv){
    if(argc<3)return 2;
    FILE *f=fopen(argv[1],"rb");
    if(!f){perror(argv[1]);return 2;}
    fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
    uint8_t *buf=malloc((size_t)n);
    if(!buf || fread(buf,1,(size_t)n,f)!=(size_t)n){fclose(f);return 2;}
    fclose(f);
    FILE *out=fopen(argv[2],"wb");
    if(!out)return 2;
    const char *gp=getenv("SAUTI_SBR82_GRID");
    FILE *gf=gp?fopen(gp,"wb"):NULL;
    if(gf)fwrite("SBQ1",1,4,gf);
    const long max_frames=argc>3?atol(argv[3]):1000000;
    static SautiAacHeAdtsMonoState core_state;
    static SautiAacSbrChannel ch;
    if(!sauti_aac_he_adts_mono_init(&core_state) || !sauti_aac_sbr_channel_init(&ch))return 2;
    size_t pos=0;long frames=0;
    while(pos+7<=(size_t)n && frames<max_frames){
        const unsigned len=((unsigned)(buf[pos+3]&3u)<<11)|((unsigned)buf[pos+4]<<3)|((unsigned)buf[pos+5]>>5);
        if(len<7 || pos+len>(size_t)n)break;
        float core[1024];
        SautiAacSbrPayloadFrame payload;
        SautiAacReason reason=SAUTI_AAC_REASON_OK;
        const int r=sauti_aac_he_adts_mono_temporal_decode_ex(buf+pos,(size_t)n-pos,&core_state,core,&payload,&reason);
        if(r!=1){
            SautiAacSbrPayloadStage stage=SAUTI_SBR_PAYLOAD_INPUT;unsigned seen=0;float c2[1024];SautiAacSbrPayloadFrame p2;SautiAacReason r2=SAUTI_AAC_REASON_OK;
            (void)sauti_aac_he_adts_mono_temporal_diagnose_ex(buf+pos,(size_t)n-pos,&core_state,c2,&p2,&r2,&stage,&seen);
            fprintf(stderr,"frame %ld: core/payload rejected (%d, reason %d, SBR stage %d, payload seen %u)\n",frames,r,(int)reason,(int)stage,seen);break;}
        static SautiAacSbrComplex grid[SAUTI_AAC_SBR_CHANNEL_GRID];
        float pcm[2048];
        if(!sauti_aac_sbr_channel_frame(&ch,core,&payload,grid)){fprintf(stderr,"frame %ld: SBR channel rejected\n",frames);break;}
        if(getenv("SAUTI_SBR82_FLAGS")){
            unsigned any=0,tones=0;for(unsigned b=0;b<payload.tables.n_high;b++)any|=payload.harmonic[b];
            for(unsigned k=0;k<64;k++)tones+=ch.last_sine[k];
            printf("BINS frame %ld:",frames);
            for(unsigned b=0;b<payload.tables.n_high;b++)if(payload.harmonic[b])printf(" %u",(unsigned)(payload.tables.high[b]+payload.tables.high[b+1])/2u);
            printf("\n");
            printf("FLAGS frame %ld t_env %u envs %u harm_any %u prev_last_sine_bins %u smooth %u limiter %u gain %u kx %u m %u\n",frames,payload.transient_env,payload.grid.env_count,any,tones,payload.bs_smoothing_mode,payload.limiter_mode,payload.gain_mode,payload.tables.kx,payload.tables.m);
        }
        if(gf){const uint32_t ix=(uint32_t)frames;fwrite(&ix,4,1,gf);fwrite(grid,sizeof(grid[0]),SAUTI_AAC_SBR_CHANNEL_GRID,gf);}
        if(!sauti_aac_sbr_channel_synthesize(&ch,grid,pcm)){fprintf(stderr,"frame %ld: synthesis rejected\n",frames);break;}
        fwrite(pcm,sizeof(float),2048,out);
        pos+=len;frames++;
    }
    fclose(out);if(gf)fclose(gf);
    printf("frames=%ld discontinuities=%llu\n",frames,(unsigned long long)ch.discontinuities);
    return frames>0?0:1;
}
