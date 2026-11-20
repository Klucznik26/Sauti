/* Test tool (not a product): decodes AAC-LD / AAC-ELD access units with the native Sauti decoder (sauti_aac_er) to interleaved float32 LE.
 * usage: sauti_er_cli ASC.bin AUS.bin OUT.f32   (the two inputs come from tools/mp4_dump_aus.py).  Prints `ER aot=.. rate_index=.. channels=.. frame=.. frames=..`. */
#include "sauti_aac_er.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *slurp(const char *path,size_t *n){
    FILE *f=fopen(path,"rb");long end;uint8_t *b;
    if(!f)return NULL;
    if(fseek(f,0,SEEK_END)||(end=ftell(f))<0||fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
    b=(uint8_t *)malloc((size_t)end+1u);
    if(!b||fread(b,1,(size_t)end,f)!=(size_t)end){fclose(f);free(b);return NULL;}
    fclose(f);*n=(size_t)end;return b;
}
int main(int argc,char **argv){
    size_t asc_n=0,aus_n=0,pos=0,frames=0,failed=0;
    uint8_t *asc,*aus;SautiAacErAsc a;SautiAacErConfig cfg;static SautiAacErState state;static float pcm[2*1024];FILE *out;int r;
    if(argc!=4)return 2;
    asc=slurp(argv[1],&asc_n);aus=slurp(argv[2],&aus_n);
    if(!asc||!aus)return 2;
    r=sauti_aac_er_asc_parse(asc,asc_n,&a);
    if(r!=1){fprintf(stderr,"ASC parse: %d\n",r);return 3;}
    memset(&cfg,0,sizeof(cfg));cfg.aot=a.aot;cfg.rate_index=a.rate_index;cfg.frame480=a.frame480;cfg.channel_config=a.channel_config;
    if(a.ld_sbr){fprintf(stderr,"ELD with SBR is not supported yet\n");return 3;}
    if(!sauti_aac_er_frame_samples(&cfg)){fprintf(stderr,"configuration not supported\n");return 3;}
    sauti_aac_er_state_init(&state);
    out=fopen(argv[3],"wb");if(!out)return 2;
    while(pos+4u<=aus_n){
        const size_t len=((size_t)aus[pos]<<24)|((size_t)aus[pos+1]<<16)|((size_t)aus[pos+2]<<8)|aus[pos+3];
        SautiAacReason reason;
        pos+=4u;
        if(pos+len>aus_n)break;
        r=sauti_aac_er_decode(&cfg,&state,aus+pos,len,pcm,&reason);
        if(r==1){fwrite(pcm,sizeof(float),(size_t)sauti_aac_er_frame_samples(&cfg)*cfg.channel_config,out);frames++;}
        else{failed++;if(failed<4)fprintf(stderr,"frame %zu: %d (%s)\n",frames+failed,r,sauti_aac_reason_name(reason));}
        pos+=len;
    }
    fclose(out);
    printf("ER aot=%u rate_index=%u channels=%u frame=%u frames=%zu failed=%zu\n",cfg.aot,cfg.rate_index,cfg.channel_config,sauti_aac_er_frame_samples(&cfg),frames,failed);
    return failed?1:0;
}
