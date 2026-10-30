#include "sauti_decoder.h"
#include "sauti_decoder_wav.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* 44-byte mono PCM16 WAV, two samples: -32768 and +32767. */
static const unsigned char wave[]={
    'R','I','F','F',40,0,0,0,'W','A','V','E',
    'f','m','t',' ',16,0,0,0,
    1,0,1,0,0x80,0xbb,0,0,0,0x77,1,0,2,0,16,0,
    'd','a','t','a',4,0,0,0,
    0,0x80,0xff,0x7f
};
int main(int argc,char **argv){
    const SautiDecoderOps *registry[]={&sauti_decoder_wav_ops};
    SautiDecoder d={0};float pcm[2]={0};
    assert(sauti_decoder_open(&d,registry,1,wave,sizeof(wave)));
    assert(d.info.sample_rate==48000 && d.info.channels==1);
    assert(d.info.total_frames==2);
    assert(sauti_decoder_read(&d,pcm,2)==2);
    assert(pcm[0]==-1.0f && pcm[1]>0.999f);
    assert(!sauti_decoder_read(&d,pcm,1));
    assert(sauti_decoder_seek(&d,1));
    assert(sauti_decoder_read(&d,pcm,1)==1 && pcm[0]>0.999f);
    sauti_decoder_close(&d);
    if(argc==2){
        FILE *f=fopen(argv[1],"rb");
        if(!f){perror("WAV fopen");return 1;}
        if(fseek(f,0,SEEK_END)!=0){fclose(f);return 1;}
        long length=ftell(f);
        if(length<=0 || fseek(f,0,SEEK_SET)!=0){fclose(f);return 1;}
        unsigned char *bytes=malloc((size_t)length);
        if(!bytes){fclose(f);return 1;}
        size_t got=fread(bytes,1,(size_t)length,f);
        fclose(f);
        if(got!=(size_t)length){free(bytes);return 1;}
        int ok=sauti_decoder_open(&d,registry,1,bytes,got);
        if(ok){
            float buffer[4096];
            size_t n=sauti_decoder_read(&d,buffer,4096/d.info.channels);
            printf("WAV PCM: %u Hz, %u channels, %zu decoded frames\n",
                   d.info.sample_rate,d.info.channels,n);
            ok=n>0 && sauti_decoder_seek(&d,0) &&
               sauti_decoder_read(&d,buffer,4096/d.info.channels)>0;
            sauti_decoder_close(&d);
        }
        free(bytes);
        return ok?0:1;
    }
    return 0;
}
