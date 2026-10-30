#include "sauti_decoder.h"
#include "sauti_decoder_vorbis.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
int main(int argc,char **argv){
    const SautiDecoderOps *list[]={&sauti_decoder_vorbis_ops};
    SautiDecoder d={0};
    const uint8_t invalid[]={'O','g','g','S',0,0,0,0};
    assert(sauti_decoder_ops_valid(list[0]));
    assert(!sauti_decoder_open(&d,list,1,invalid,sizeof(invalid)));
    if(argc!=2)return 0;
    FILE *f=fopen(argv[1],"rb");
    if(!f){perror("Vorbis fopen");return 1;}
    if(fseek(f,0,SEEK_END)){fclose(f);return 1;}
    long length=ftell(f);
    if(length<=0 || fseek(f,0,SEEK_SET)){fclose(f);return 1;}
    uint8_t *bytes=malloc((size_t)length);
    if(!bytes){fclose(f);return 1;}
    size_t got=fread(bytes,1,(size_t)length,f);
    fclose(f);
    if(got!=(size_t)length){free(bytes);return 1;}
    int ok=sauti_decoder_open(&d,list,1,bytes,got);
    if(ok){
        float pcm[4096];
        size_t frames=sauti_decoder_read(&d,pcm,4096/d.info.channels);
        printf("OGG Vorbis: %u Hz, %u channels, %zu decoded frames\n",
               d.info.sample_rate,d.info.channels,frames);
        ok=frames>0 && sauti_decoder_seek(&d,0) &&
            sauti_decoder_read(&d,pcm,4096/d.info.channels)>0;
        sauti_decoder_close(&d);
    }
    free(bytes);
    return ok?0:1;
}
