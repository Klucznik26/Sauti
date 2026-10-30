#include "sauti_decoder.h"
#include "sauti_decoder_midi.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv){
    /* Format 0, one channel, 1 note held for one quarter note. */
    const unsigned char midi[]={
        'M','T','h','d',0,0,0,6,0,0,0,1,0,96,
        'M','T','r','k',0,0,0,12,
        0,0x90,69,100,0x60,0x80,69,0,0,0xff,0x2f,0
    };
    const SautiDecoderOps *ops[]={&sauti_decoder_midi_ops};
    SautiDecoder d={0};
    assert(sauti_decoder_open(&d,ops,1,midi,sizeof(midi)));
    assert(d.info.channels==2 && d.info.sample_rate==44100);
    float buffer[1024];
    assert(sauti_decoder_read(&d,buffer,512)==512);
    float peak=0.0f;
    for(size_t i=0;i<1024;++i){
        float a=buffer[i]<0.0f?-buffer[i]:buffer[i];
        if(a>peak)peak=a;
    }
    assert(peak>0.001f);
    assert(sauti_decoder_seek(&d,0));
    assert(sauti_decoder_read(&d,buffer,512)==512);
    sauti_decoder_close(&d);
    if(argc==2){
        FILE *f=fopen(argv[1],"rb");
        if(!f)return 1;
        if(fseek(f,0,SEEK_END)!=0){fclose(f);return 1;}
        long n=ftell(f);
        if(n<=0 || fseek(f,0,SEEK_SET)!=0){fclose(f);return 1;}
        unsigned char *b=malloc((size_t)n);
        if(!b){fclose(f);return 1;}
        int ok=fread(b,1,(size_t)n,f)==(size_t)n;
        fclose(f);
        if(ok)ok=sauti_decoder_open(&d,ops,1,b,(size_t)n);
        if(ok){
            printf("MIDI: %.2f s, %u Hz, %u channels\n",
                   (double)d.info.total_frames/d.info.sample_rate,
                   d.info.sample_rate,d.info.channels);
            ok=sauti_decoder_read(&d,buffer,512)>0;
            sauti_decoder_close(&d);
        }
        free(b);
        return ok?0:1;
    }
    return 0;
}
