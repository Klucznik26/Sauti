/* AAC-26: inspect every frame from a real stereo AAC-LC ADTS fixture.
 * This is NOT a complete conformance test: unsupported tools are
 * counted and reported, not silently interpreted as valid PCM. */
#include "sauti_aac_cpe.h"
#include "sauti_aac_headers.h"
#include "sauti_aac_syntax.h"
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
    SautiAacLcSynthesis state[2]={0};
    if(!sauti_aac_lc_synthesis_init(&state[0],0) ||
       !sauti_aac_lc_synthesis_init(&state[1],0)){
        free(bytes);return 2;
    }
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
    float samples[2048];
    const int trace_windows=getenv("SAUTI_AAC_TRACE_WINDOWS")!=NULL;
    while(offset<(size_t)end){
        SautiAacAdts adts={0};
        if(!sauti_aac_adts_parse_asm(bytes+offset,(size_t)end-offset,
                                    &adts) || adts.frame_bytes==0){
            invalid++;break;
        }
        total++;
        SautiAacReason reason=SAUTI_AAC_REASON_OK;
        const uint32_t seed_before=state[0].pns_seed;
        int result=sauti_aac_lc_adts_stereo_decode_ex(bytes+offset,
                                      adts.frame_bytes,state,samples,&reason);
        if(trace_windows && result==1)
            printf("    pns_rng: before=%08x after=%08x\\n",
                   (unsigned)seed_before,(unsigned)state[0].pns_seed);
        printf("  frame %zu: %s (%s)\n",total-1,
               result==1?"decoded":result==-1?"unsupported":"invalid",
               sauti_aac_reason_name(reason));
        if(mask_out)fprintf(mask_out,"%zu %d %s\n",total-1,result,
                            sauti_aac_reason_name(reason));
        if(trace_windows && result==1){
            /* Inspect the same CPE's tool metadata without changing the
             * decoder state or the frame's PCM. */
            const uint8_t *raw=bytes+offset+adts.header_bytes;
            const size_t raw_length=adts.frame_bytes-adts.header_bytes;
            const unsigned rate=(bytes[offset+2u]>>2u)&15u;
            size_t bitpos=0;
            SautiAacRawPrefix prefix;
            SautiAacCpeData cpe;
            SautiAacReason diagnostic_reason=SAUTI_AAC_REASON_OK;
            unsigned tns_count[2]={0,0},pns_count[2]={0,0};
            unsigned intensity_count=0,ms_count=0;
            int prefix_status=sauti_aac_raw_prefix_parse(raw,raw_length,
                                                          &bitpos,&prefix);
            if(prefix_status==2 &&
               sauti_aac_lc_cpe_parse(raw,raw_length,&bitpos,rate,&cpe,
                                      &diagnostic_reason)==1){
                for(unsigned ch=0;ch<2;ch++){
                    for(unsigned w=0;w<cpe.channel[ch].ics.window_count;w++)
                        tns_count[ch]+=cpe.channel[ch].tns.filter_count[w];
                    for(unsigned g=0;g<cpe.channel[ch].ics.group_count;g++)
                        for(unsigned b=0;b<cpe.channel[ch].ics.max_sfb;b++){
                            unsigned cb=cpe.channel[ch].factors.codebook[g][b];
                            if(cb==13u)pns_count[ch]++;
                            if(ch==1u && cb>=14u)intensity_count++;
                            if(ch==0u && cpe.ms_used[g][b])ms_count++;
                        }
                }
                printf("    tools: groups=%u max_sfb=%u TNS=%u/%u PNS=%u/%u MS=%u intensity=%u\n",
                       cpe.channel[0].ics.group_count,
                       cpe.channel[0].ics.max_sfb,
                       tns_count[0],tns_count[1],pns_count[0],pns_count[1],
                       ms_count,intensity_count);
            }
            printf("    windows L=sequence:%u shape:%u R=sequence:%u shape:%u\n",
                   state[0].previous_sequence,state[0].previous_shape,
                   state[1].previous_sequence,state[1].previous_shape);
        }
        if(pcm_out){
            float silence[2048]={0};
            if(fwrite(result==1?samples:silence,sizeof(float),2048,
                      pcm_out)!=2048){invalid++;break;}
        }
        if(result==1){
            decoded++;
            for(unsigned k=0;k<2048;k++){
                if(!isfinite(samples[k])){invalid++;break;}
            }
        }else if(result==-1){
            unsupported++;
            /* An unseen AAC frame destroys continuous overlap history;
             * do not silently replace it with a freshly initialized one. */
            state[0].initialized=0u;
            state[1].initialized=0u;
        }else invalid++;
        offset+=adts.frame_bytes;
        if(invalid)break;
    }
    printf("AAC real stereo ADTS: %zu frames, %zu native PCM, %zu unsupported, %zu invalid\n",
           total,decoded,unsupported,invalid);
    if(pcm_out && fclose(pcm_out)!=0)invalid++;
    if(mask_out && fclose(mask_out)!=0)invalid++;
    free(bytes);
    if(total==0 || invalid!=0)return 1;
    if(decoded==0){
        puts("SKIP native AAC stereo PCM: all valid frames use unsupported tools");
    }else{
        puts("PASS AAC real stereo native PCM: decoded samples are finite");
    }
    return 0;
}
