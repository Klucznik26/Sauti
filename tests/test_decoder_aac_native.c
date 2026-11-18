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
    printf("PASS native AAC-LC real ADTS: %u Hz, %llu PCM frames, repeatable seek\n",
           decoder.info.sample_rate,
           (unsigned long long)decoder.info.total_frames);
    sauti_decoder_close(&decoder);
    free(bytes);
}

/* Adapter MONO LC (AAC-LC SCE): cisza + FIL za elementem (typ 1, jak fdk-aac) -- ramka, ktora wczesniej byla odrzucana; HE-AAC (FIL z SBR) musi zostac odrzucone przy otwarciu. */
static size_t make_mono_frame(uint8_t *out,unsigned fil_type,unsigned fil_count){
    Bits raw={0};
    put(&raw,3,0);put(&raw,4,0);                     /* ID_SCE, tag */
    put(&raw,8,100);                                 /* global_gain */
    put(&raw,1,0);put(&raw,2,0);put(&raw,1,0);       /* reserved, ONLY_LONG, sin */
    put(&raw,6,0);put(&raw,1,0);                     /* max_sfb=0, brak predykcji */
    put(&raw,1,0);put(&raw,1,0);put(&raw,1,0);       /* pulse, TNS, gain control */
    if(fil_count){
        put(&raw,3,6);put(&raw,4,fil_count);put(&raw,4,fil_type);
        for(unsigned i=4;i<fil_count*8u;i++)put(&raw,1,0);
    }
    put(&raw,3,7);while(raw.bits%8u)put(&raw,1,0);
    const size_t length=7u+raw.bits/8u;
    memset(out,0,length);
    out[0]=0xff;out[1]=0xf1;out[2]=0x50;             /* LC, 44.1 kHz, channel_config bit 2 = 0 */
    out[3]=(uint8_t)(0x40u|((length>>11u)&3u));      /* channel_config=1 (mono) */
    out[4]=(uint8_t)(length>>3u);
    out[5]=(uint8_t)(((length&7u)<<5u)|0x1fu);
    out[6]=0xfcu;
    memcpy(out+7,raw.data,raw.bits/8u);
    return length;
}
static void mono_adapter(void){
    uint8_t f[64];
    const size_t n=make_mono_frame(f,1,3);
    uint8_t stream[3*64];
    for(unsigned i=0;i<3;i++)memcpy(stream+i*n,f,n);
    const SautiDecoderOps *mono[]={&sauti_decoder_aac_mono_native_ops};
    const SautiDecoderOps *all[]={&sauti_decoder_aac_native_ops,&sauti_decoder_aac_mono_native_ops,&sauti_decoder_aac_he_native_ops};
    assert(sauti_decoder_ops_valid(mono[0]));
    SautiDecoder d={0};
    assert(sauti_decoder_open(&d,mono,1,stream,3*n));
    assert(d.info.channels==1 && d.info.sample_rate==44100 && d.info.total_frames==3072);
    float pcm[4096];
    assert(sauti_decoder_read(&d,pcm,1500)==1500);
    for(unsigned i=0;i<1500;i++)assert(pcm[i]==0.0f);
    assert(sauti_decoder_seek(&d,2047));
    assert(sauti_decoder_read(&d,pcm,2000)==1025);          /* do konca: 3072 - 2047 */
    assert(sauti_decoder_read(&d,pcm,1)==0);
    assert(!sauti_decoder_seek(&d,3073));
    sauti_decoder_close(&d);
    /* rejestr z wszystkimi adapterami: mono LC trafia do adaptera mono, nie do stereo ani HE */
    assert(sauti_decoder_open(&d,all,3,stream,3*n));
    assert(d.ops==&sauti_decoder_aac_mono_native_ops && d.info.channels==1);
    sauti_decoder_close(&d);
    /* HE-AAC mono (FIL z SBR, typ 13/14): rdzen LC go nie pomija, adapter nie otwiera strumienia */
    for(unsigned t=13;t<=14;t++){
        uint8_t he[64];
        const size_t hn=make_mono_frame(he,t,3);
        assert(!sauti_decoder_open(&d,mono,1,he,hn));
    }
    /* stereo nie jest mono: adapter mono nie probuje stereo */
    uint8_t st[64]={0};
    const size_t sn=make_frame(st);
    assert(!sauti_decoder_open(&d,mono,1,st,sn));
    /* ucieta ramka w srodku strumienia: otwarcie odrzucone (naglowki sprawdzane w calosci) */
    assert(!sauti_decoder_open(&d,mono,1,stream,3*n-2));
}

int main(int argc,char **argv){
    if(argc!=1 && argc!=2)return 2;
    mono_adapter();
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

    /* Exercise the 64-frame state checkpoint boundary, both directions,
     * with a 130-frame ADTS stream. Seeking must reconstruct bit-identical
     * PCM after history has been populated through sequential playback. */
    {
        enum { COUNT = 130 };
        uint8_t *long_adts=malloc(COUNT*n);
        assert(long_adts);
        for(unsigned i=0;i<COUNT;i++)memcpy(long_adts+i*n,bytes,n);
        assert(sauti_decoder_open(&decoder,registry,1,long_adts,COUNT*n));
        float chunk[4096];
        for(unsigned i=0;i<COUNT;i++)
            assert(sauti_decoder_read(&decoder,chunk,1024)==1024);
        assert(sauti_decoder_seek(&decoder,64u*1024u+511u));
        assert(sauti_decoder_read(&decoder,chunk,2048)==2048);
        for(unsigned i=0;i<4096;i++)assert(chunk[i]==0);
        assert(sauti_decoder_seek(&decoder,129u*1024u+100u));
        assert(sauti_decoder_read(&decoder,chunk,924)==924);
        for(unsigned i=0;i<1848;i++)assert(chunk[i]==0);
        assert(sauti_decoder_seek(&decoder,0));
        assert(sauti_decoder_read(&decoder,chunk,1024)==1024);
        sauti_decoder_close(&decoder);
        free(long_adts);
    }

    /* Strict fail closed: truncated second ADTS or invalid tool must
     * not produce a partially playable decoder. */
    assert(!sauti_decoder_open(&decoder,registry,1,bytes,2*n-1));
    bytes[n]=0;
    assert(!sauti_decoder_open(&decoder,registry,1,bytes,2*n));
    if(argc==2)real_stream(argv[1]);
    puts("PASS AAC-LC native adapter: complete ADTS, read, seek, EOF, fail-closed");
    return 0;
}
