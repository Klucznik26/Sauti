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
    if(argc!=2)return 2;
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
    size_t offset=0,decoded=0,unsupported=0,invalid=0,total=0;
    float samples[1024];
    while(offset<(size_t)end){
        SautiAacAdts adts={0};
        if(!sauti_aac_adts_parse_asm(bytes+offset,(size_t)end-offset,
                                    &adts) || adts.frame_bytes==0){
            invalid++;break;
        }
        total++;
        int result=sauti_aac_lc_adts_mono_decode(bytes+offset,
                                            adts.frame_bytes,&state,samples);
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
    free(bytes);
    if(total==0 || invalid!=0)return 1;
    if(decoded==0){
        puts("SKIP native AAC real PCM: all valid frames use unsupported tools");
    }else{
        puts("PASS AAC real mono native PCM: decoded samples are finite");
    }
    return 0;
}
