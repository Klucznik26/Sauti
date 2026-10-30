/* Dump Sauti DVSM frames as little-endian signed 16-bit for independent audit. */
#include "sauti_decoder.h"
#include "sauti_decoder_dvsm.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
int main(int argc,char **argv){
    if(argc!=3){fprintf(stderr,"usage: %s input.dvs output.raw\n",argv[0]);return 2;}
    FILE *in=fopen(argv[1],"rb");
    if(!in){perror(argv[1]);return 1;}
    if(fseek(in,0,SEEK_END)||ftell(in)<1){fclose(in);return 1;}
    long end=ftell(in);
    if(fseek(in,0,SEEK_SET)){fclose(in);return 1;}
    uint8_t *bytes=malloc((size_t)end);
    if(!bytes){fclose(in);return 1;}
    if(fread(bytes,1,(size_t)end,in)!=(size_t)end){perror("read");fclose(in);free(bytes);return 1;}
    fclose(in);
    const SautiDecoderOps *ops[]={&sauti_decoder_dvsm_ops};
    SautiDecoder d={0};
    if(!sauti_decoder_open(&d,ops,1,bytes,(size_t)end)){
        fprintf(stderr,"DVSM decoder rejected input\n");free(bytes);return 1;
    }
    if(d.info.channels>2){sauti_decoder_close(&d);free(bytes);return 1;}
    FILE *out=fopen(argv[2],"wb");
    if(!out){perror(argv[2]);sauti_decoder_close(&d);free(bytes);return 1;}
    float f32[4096];
    uint64_t total=0;
    size_t got;
    int ok=1;
    while((got=sauti_decoder_read(&d,f32,4096/d.info.channels))!=0){
        for(size_t j=0;j<got*d.info.channels;j++){
            float scaled=f32[j]*32768.0f;
            if(!(scaled>=-32768.0f&&scaled<=32767.0f)){ok=0;break;}
            int sample=(int)scaled;
            unsigned char le[2]={(unsigned char)(sample&255),(unsigned char)(((unsigned)sample>>8)&255)};
            if(fwrite(le,1,2,out)!=2){ok=0;break;}
        }
        if(!ok)break;
        total+=got;
    }
    fprintf(stderr,"SAUTI: rate=%u channels=%u frames=%llu\n",
            d.info.sample_rate,d.info.channels,(unsigned long long)total);
    if(total!=d.info.total_frames)ok=0;
    if(fclose(out)!=0)ok=0;
    sauti_decoder_close(&d);free(bytes);
    return ok?0:1;
}
