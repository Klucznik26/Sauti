#include "sauti_decoder.h"
#include "sauti_decoder_flac.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv){
    const SautiDecoderOps *registry[]={&sauti_decoder_flac_ops};
    SautiDecoder decoder={0};
    static const uint8_t broken[]={'f','L','a','C',0,0,0,0};
    assert(sauti_decoder_ops_valid(registry[0]));
    assert(!sauti_decoder_open(&decoder,registry,1,broken,sizeof(broken)));
    if(argc!=2)return 0;
    FILE *file=fopen(argv[1],"rb");
    if(!file){perror("FLAC fopen");return 1;}
    if(fseek(file,0,SEEK_END)!=0){fclose(file);return 1;}
    long len=ftell(file);
    if(len<=0 || fseek(file,0,SEEK_SET)!=0){fclose(file);return 1;}
    uint8_t *data=malloc((size_t)len);
    if(!data){fclose(file);return 1;}
    int ok=fread(data,1,(size_t)len,file)==(size_t)len;
    fclose(file);
    if(ok)ok=sauti_decoder_open(&decoder,registry,1,data,(size_t)len);
    if(ok){
        float pcm[4096];
        size_t frames=sauti_decoder_read(&decoder,pcm,4096/decoder.info.channels);
        printf("FLAC: %u Hz, %u channels, %llu frames, decoded %zu\n",
               decoder.info.sample_rate,decoder.info.channels,
               (unsigned long long)decoder.info.total_frames,frames);
        ok=frames>0 && sauti_decoder_seek(&decoder,0) &&
           sauti_decoder_read(&decoder,pcm,4096/decoder.info.channels)>0;
        sauti_decoder_close(&decoder);
    }
    free(data);
    return ok?0:1;
}
