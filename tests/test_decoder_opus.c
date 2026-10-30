#include "sauti_decoder.h"
#include "sauti_decoder_opus.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv){
    const SautiDecoderOps *ops[]={&sauti_decoder_opus_ops};
    SautiDecoder decoder={0};
    const unsigned char invalid[]={'O','g','g','S',0,0,0,0};
    assert(sauti_decoder_ops_valid(ops[0]));
    assert(!sauti_decoder_open(&decoder,ops,1,invalid,sizeof(invalid)));
    if(argc!=2)return 0;
    FILE *f=fopen(argv[1],"rb");
    if(!f){perror("Opus fopen");return 1;}
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return 1;}
    long len=ftell(f);
    if(len<=0 || fseek(f,0,SEEK_SET)!=0){fclose(f);return 1;}
    uint8_t *bytes=malloc((size_t)len);
    if(!bytes){fclose(f);return 1;}
    int ok=fread(bytes,1,(size_t)len,f)==(size_t)len;
    fclose(f);
    if(ok)ok=sauti_decoder_open(&decoder,ops,1,bytes,(size_t)len);
    if(ok){
        float pcm[4096];
        size_t frames=sauti_decoder_read(&decoder,pcm,2048);
        printf("Opus: %u Hz, %u channels, %zu decoded frames\n",
               decoder.info.sample_rate,decoder.info.channels,frames);
        ok=frames>0 && sauti_decoder_seek(&decoder,0) &&
           sauti_decoder_read(&decoder,pcm,2048)>0;
        sauti_decoder_close(&decoder);
    }
    free(bytes);
    return ok?0:1;
}
