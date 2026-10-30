#include "sauti_decoder.h"
#include "sauti_decoder_mp3.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char **argv){
    const SautiDecoderOps *reg[]={&sauti_decoder_mp3_ops};
    const uint8_t bad[]={'R','I','F','F',0,0,0,0};
    SautiDecoder dec={0};
    float samples[4096]={0};
    assert(sauti_decoder_ops_valid(reg[0]));
    assert(!reg[0]->probe(bad,sizeof(bad)));
    assert(!sauti_decoder_open(&dec,reg,1,bad,sizeof(bad)));
    assert(sauti_decoder_read(&dec,samples,4)==0);
    sauti_decoder_close(&dec);

    /* Optional real-file integration test; file I/O is test-only. */
    if(argc<2)return 0;
    FILE *f=fopen(argv[1],"rb");
    if(!f){perror("MP3 test fopen");return 1;}
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return 1;}
    long size=ftell(f);
    if(size<=0 || fseek(f,0,SEEK_SET)!=0){fclose(f);return 1;}
    uint8_t *bytes=malloc((size_t)size);
    if(!bytes){fclose(f);return 1;}
    size_t n=fread(bytes,1,(size_t)size,f);
    fclose(f);
    if(n!=(size_t)size){free(bytes);return 1;}
    int success=sauti_decoder_open(&dec,reg,1,bytes,n);
    if(success){
        printf("MP3: %u Hz, %u channels, %llu frames\n",
               dec.info.sample_rate,dec.info.channels,
               (unsigned long long)dec.info.total_frames);
        size_t frames=sauti_decoder_read(&dec,samples,4096/dec.info.channels);
        printf("MP3: decoded %zu frames\n",frames);
        success=frames>0 && sauti_decoder_seek(&dec,0) &&
                sauti_decoder_read(&dec,samples,4096/dec.info.channels)>0;
        sauti_decoder_close(&dec);
    }
    free(bytes);
    return success?0:1;
}
