#include "sauti_decoder.h"
#include "sauti_decoder_aac_native.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

/* Two valid AAC-LC stereo ADTS frames, each containing a CPE with
 * common_window, max_sfb=0 (silence), followed by ID_END and padding. */
typedef struct {uint8_t data[32];unsigned bits;} Bits;
static void put(Bits *b,unsigned width,unsigned v){
    while(width--){
        assert(b->bits<sizeof(b->data)*8u);
        b->data[b->bits/8u]|=(uint8_t)(((v>>width)&1u)<<(7u-b->bits%8u));
        b->bits++;
    }
}
static size_t make_frame(uint8_t *out){
    Bits raw={0};
    put(&raw,3,1);put(&raw,4,0);put(&raw,1,1); /* ID_CPE */
    put(&raw,1,0);put(&raw,2,0);put(&raw,1,0); /* ICS long */
    put(&raw,6,0);put(&raw,1,0);             /* zero bands */
    put(&raw,2,0);                           /* no MS */
    for(unsigned ch=0;ch<2;ch++){
        put(&raw,8,100); /* global_gain */
        put(&raw,1,0);put(&raw,1,0);put(&raw,1,0);
    }
    put(&raw,3,7);while(raw.bits%8u)put(&raw,1,0);
    const size_t length=7u+raw.bits/8u;
    memset(out,0,length);
    out[0]=0xff;out[1]=0xf1;out[2]=0x50;
    out[3]=(uint8_t)(0x80u|((length>>11u)&3u));
    out[4]=(uint8_t)(length>>3u);
    out[5]=(uint8_t)(((length&7u)<<5u)|0x1fu);
    out[6]=0xfcu;
    memcpy(out+7,raw.data,raw.bits/8u);
    return length;
}
/* Optional real-world ADTS path: check repeated random-access reads of
 * non-silent native PCM without using FFmpeg in the decoder itself. */
static void real_stream(const char *path){
    FILE *f=fopen(path,"rb");
    assert(f);
    assert(fseek(f,0,SEEK_END)==0);
    const long len=ftell(f);
    assert(len>0 && len<20000000L);
    assert(fseek(f,0,SEEK_SET)==0);
    uint8_t *bytes=malloc((size_t)len);
    assert(bytes);
    assert(fread(bytes,1,(size_t)len,f)==(size_t)len);
    fclose(f);
    const SautiDecoderOps *registry[]={&sauti_decoder_aac_native_ops};
    SautiDecoder decoder={0};
    assert(sauti_decoder_open(&decoder,registry,1,bytes,(size_t)len));
    assert(decoder.info.channels==2 && decoder.info.total_frames>=2048);
    const uint64_t target=decoder.info.total_frames/2u+13u;
    float a[512],b[512];
    assert(sauti_decoder_seek(&decoder,target));
    assert(sauti_decoder_read(&decoder,a,256)==256);
    assert(sauti_decoder_seek(&decoder,0));
    float trash[512];
    assert(sauti_decoder_read(&decoder,trash,256)==256);
    assert(sauti_decoder_seek(&decoder,target));
    assert(sauti_decoder_read(&decoder,b,256)==256);
    assert(memcmp(a,b,sizeof(a))==0);
    double energy=0;
    for(unsigned i=0;i<512;i++){
        assert(isfinite(a[i]));
        energy+=(double)a[i]*a[i];
    }
    assert(energy>0);
    printf("PASS native AAC-LC real ADTS: %u Hz, %llu PCM frames, repeatable seek\\n",
           decoder.info.sample_rate,
           (unsigned long long)decoder.info.total_frames);
    sauti_decoder_close(&decoder);
    free(bytes);
}
int main(int argc,char **argv){
    if(argc!=1 && argc!=2)return 2;
    uint8_t bytes[64]={0};
    size_t n=make_frame(bytes);
    memcpy(bytes+n,bytes,n);
    const SautiDecoderOps *registry[]={&sauti_decoder_aac_native_ops};
    SautiDecoder decoder={0};
    assert(sauti_decoder_ops_valid(registry[0]));
    assert(sauti_decoder_open(&decoder,registry,1,bytes,2*n));
    assert(decoder.info.channels==2);
    assert(decoder.info.sample_rate==44100);
    assert(decoder.info.total_frames==2048);
    float pcm[4096];
    assert(sauti_decoder_read(&decoder,pcm,37)==37);
    for(unsigned i=0;i<74;i++)assert(pcm[i]==0);
    assert(sauti_decoder_seek(&decoder,1023));
    assert(sauti_decoder_read(&decoder,pcm,3)==3);
    for(unsigned i=0;i<6;i++)assert(pcm[i]==0);
    assert(sauti_decoder_seek(&decoder,0));
    assert(sauti_decoder_read(&decoder,pcm,2048)==2048);
    for(unsigned i=0;i<4096;i++)assert(pcm[i]==0);
    assert(sauti_decoder_read(&decoder,pcm,1)==0);
    assert(sauti_decoder_seek(&decoder,2048));
    assert(sauti_decoder_read(&decoder,pcm,1)==0);
    assert(!sauti_decoder_seek(&decoder,2049));
    sauti_decoder_close(&decoder);

    /* Strict fail closed: truncated second ADTS or invalid tool must
     * not produce a partially playable decoder. */
    assert(!sauti_decoder_open(&decoder,registry,1,bytes,2*n-1));
    bytes[n]=0;
    assert(!sauti_decoder_open(&decoder,registry,1,bytes,2*n));
    if(argc==2)real_stream(argv[1]);
    puts("PASS AAC-LC native adapter: complete ADTS, read, seek, EOF, fail-closed");
    return 0;
}
