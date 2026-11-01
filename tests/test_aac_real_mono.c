/* AAC-14: inspect every frame from a real mono AAC-LC ADTS fixture.
 * This is NOT a complete conformance test: unsupported tools are
 * counted and reported, not silently interpreted as valid PCM. */
#include "sauti_aac_lc_frame.h"
#include "sauti_aac_headers.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc,char **argv){
    if(argc!=2 && argc!=4)return 2;
    FILE *f=fopen(argv[1],"rb");
    if(!f){perror(argv[1]);return 2;}
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return 2;}
    long end=ftell(f);
    if(end<=0 || end>20000000L || fseek(f,0,SEEK_SET)!=0){
        fclose(f);return 2;
    }
    uint8_t *bytes=malloc((size_t)end);
    if(!bytes){fclose(f);return 2;}
    size_t read_count=fread(bytes,1,(size_t)end,f);
    fclose(f);
    if(read_count!=(size_t)end){free(bytes);return 2;}
    SautiAacLcSynthesis state={0};
    if(!sauti_aac_lc_synthesis_init(&state,0)){free(bytes);return 2;}
    FILE *pcm_out=NULL,*mask_out=NULL;
    if(argc==4){
        pcm_out=fopen(argv[2],"wb");mask_out=fopen(argv[3],"w");
        if(!pcm_out || !mask_out){
            if(pcm_out)fclose(pcm_out);
            if(mask_out)fclose(mask_out);
            free(bytes);return 2;
        }
    }
    size_t offset=0,decoded=0,unsupported=0,invalid=0,total=0;
    float samples[1024];
    while(offset<(size_t)end){
        SautiAacAdts adts={0};
        if(!sauti_aac_adts_parse_asm(bytes+offset,(size_t)end-offset,
                                    &adts) || adts.frame_bytes==0){
            invalid++;break;
        }
        total++;
        SautiAacReason reason=SAUTI_AAC_REASON_OK;
        int result=sauti_aac_lc_adts_mono_decode_ex(bytes+offset,
                                      adts.frame_bytes,&state,samples,&reason);
        printf("  frame %zu: %s (%s)\n",total-1,
               result==1?"decoded":result==-1?"unsupported":"invalid",
               sauti_aac_reason_name(reason));
        if(mask_out)fprintf(mask_out,"%zu %d %s\n",total-1,result,
                            sauti_aac_reason_name(reason));
        if(pcm_out){
            float silence[1024]={0};
            if(fwrite(result==1?samples:silence,sizeof(float),1024,
                      pcm_out)!=1024){invalid++;break;}
        }
        if(result==1){
            decoded++;
            for(unsigned k=0;k<1024;k++){
                if(!isfinite(samples[k])){invalid++;break;}
            }
        }else if(result==-1){
            unsupported++;
            /* Do not reuse prior overlap after a skipped frame. */
            if(!sauti_aac_lc_synthesis_init(&state,0)){invalid++;break;}
        }else invalid++;
        offset+=adts.frame_bytes;
        if(invalid)break;
    }
    printf("AAC real mono ADTS: %zu frames, %zu native PCM, %zu unsupported, %zu invalid\n",
           total,decoded,unsupported,invalid);
    if(pcm_out && fclose(pcm_out)!=0)invalid++;
    if(mask_out && fclose(mask_out)!=0)invalid++;
    free(bytes);
    if(total==0 || invalid!=0)return 1;
    if(decoded==0){
        puts("SKIP native AAC real PCM: all valid frames use unsupported tools");
    }else{
        puts("PASS AAC real mono native PCM: decoded samples are finite");
    }
    return 0;
}
