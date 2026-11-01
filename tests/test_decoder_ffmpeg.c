#include "sauti_decoder.h"
#include "sauti_decoder_ffmpeg.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void invalid(void){
    const SautiDecoderOps *decoders[]={
        &sauti_decoder_aac_ops,&sauti_decoder_alac_ops,&sauti_decoder_webm_ops
    };
    const uint8_t not_audio[]={'R','I','F','F',0,0,0,0,'W','A','V','E'};
    const uint8_t fake_mp4[]={0,0,0,12,'f','t','y','p','M','4','A',' '};
    const uint8_t fake_ebml[]={0x1a,0x45,0xdf,0xa3,0,0,0,0};
    for(size_t i=0;i<3;i++)assert(sauti_decoder_ops_valid(decoders[i]));
    SautiDecoder d={0};
    assert(!sauti_decoder_open(&d,decoders,3,not_audio,sizeof(not_audio)));
    assert(!sauti_decoder_open(&d,decoders,3,fake_mp4,sizeof(fake_mp4)));
    assert(!sauti_decoder_open(&d,decoders,3,fake_ebml,sizeof(fake_ebml)));
}
int main(int argc,char **argv){
    invalid();
    if(argc==1){puts("PASS FFmpeg adapter invalid-input checks");return 0;}
    if(argc!=3)return 2;
    const SautiDecoderOps *codec=NULL;
    if(strcmp(argv[2],"aac")==0)codec=&sauti_decoder_aac_ops;
    else if(strcmp(argv[2],"alac")==0)codec=&sauti_decoder_alac_ops;
    else if(strcmp(argv[2],"webm")==0)codec=&sauti_decoder_webm_ops;
    else return 2;
    FILE *f=fopen(argv[1],"rb");
    if(!f){perror("open fixture");return 1;}
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return 1;}
    long len=ftell(f);
    if(len<=0 || fseek(f,0,SEEK_SET)!=0){fclose(f);return 1;}
    uint8_t *bytes=malloc((size_t)len);
    if(!bytes){fclose(f);return 1;}
    size_t got=fread(bytes,1,(size_t)len,f);
    fclose(f);
    if(got!=(size_t)len){free(bytes);return 1;}
    SautiDecoder d={0};
    const SautiDecoderOps *registry[]={codec};
    assert(sauti_decoder_open(&d,registry,1,bytes,got));
    assert(d.info.channels>=1 && d.info.channels<=2);
    assert(d.info.sample_rate>=8000 && d.info.total_frames>=1000);
    float buffer[1024*2]={0};
    size_t frames=sauti_decoder_read(&d,buffer,1024);
    assert(frames>0);
    float peak=0.f;
    for(size_t i=0;i<frames*d.info.channels;i++){
        float a=buffer[i]>=0?buffer[i]:-buffer[i];
        if(a>peak)peak=a;
    }
    assert(peak>0.001f && peak<2.f);
    uint64_t midpoint=d.info.total_frames/2;
    assert(sauti_decoder_seek(&d,midpoint));
    assert(sauti_decoder_read(&d,buffer,128)>0);
    assert(sauti_decoder_seek(&d,d.info.total_frames));
    assert(sauti_decoder_read(&d,buffer,128)==0);
    assert(!sauti_decoder_seek(&d,d.info.total_frames+1));
    printf("PASS %s: %u Hz, %u ch, %llu frames, seek\n",
           codec->name,d.info.sample_rate,d.info.channels,
           (unsigned long long)d.info.total_frames);
    sauti_decoder_close(&d);free(bytes);
    return 0;
}
