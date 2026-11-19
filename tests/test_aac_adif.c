/* AAC w ADIF: demuxer (sauti_aac_adif) wyznaczajacy granice blokow parsowaniem elementow, i adapter aac-native-adif.  Pliki skladane w pamieci bit po bicie; bloki to
 * prawdziwe ramki AAC-LC (cisza: max_sfb = 0), wiec adapter dekoduje je naprawde.  Zgodnosc z prawdziwymi strumieniami: scripts/test_he_adapter_vs_ffmpeg.sh. */
#include "sauti_decoder_aac_native.h"
#include "sauti_aac_adif.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t b[16384]; size_t bit; } Bw;
static void put(Bw *w,unsigned n,uint32_t v){
    for(unsigned i=0;i<n;i++){
        if((v>>(n-1u-i))&1u)w->b[w->bit>>3]|=(uint8_t)(0x80u>>(w->bit&7u));
        w->bit++;
    }
}
static void align(Bw *w){while(w->bit&7u)put(w,1,0);}
typedef struct {
    unsigned stereo;          /* 0 = SCE (mono), 1 = CPE */
    unsigned copyright;       /* copyright_id_present */
    unsigned vbr;             /* bitstream_type 1 */
    unsigned object_type;     /* 1 = LC */
    unsigned extra_pce;       /* num_program_config_elements + 1 > 1 */
    unsigned side;            /* a side channel element in the PCE */
    unsigned mixdown;         /* mono/stereo/matrix mixdown present */
    unsigned comment;         /* comment bytes in the PCE */
    unsigned fill_dse;        /* a DSE and a FIL element before the channel element, a FIL after it */
    unsigned blocks;
    unsigned rate_index;
} Spec;
/* one raw_data_block: [DSE][FIL] SCE|CPE (silence) [FIL] END, byte aligned */
static void block(Bw *w,const Spec *s,unsigned n){
    if(s->fill_dse){
        put(w,3,6);put(w,4,2);put(w,8,0x11);put(w,8,0x22);                                                   /* FIL, 2 bytes (23 bits: the DSE below starts unaligned) */
        put(w,3,4);put(w,4,3);put(w,1,1);put(w,8,3);align(w);put(w,8,0xaa);put(w,8,0xbb);put(w,8,0xcc);   /* DSE: 3 data bytes after alignment */
    }
    if(!s->stereo){
        put(w,3,0);put(w,4,0);put(w,8,100);put(w,1,0);put(w,2,0);put(w,1,0);put(w,6,0);put(w,1,0);put(w,1,0);put(w,1,0);put(w,1,0);
    }else{
        put(w,3,1);put(w,4,0);put(w,1,1);                                                                     /* CPE, tag, common_window */
        put(w,1,0);put(w,2,0);put(w,1,0);put(w,6,0);put(w,1,0);put(w,2,0);                                  /* ics_info (max_sfb 0), ms_mask_present 0 */
        put(w,8,100);put(w,1,0);put(w,1,0);put(w,1,0);                                                       /* channel 0: global_gain, pulse, tns, gain */
        put(w,8,100+n%3u);put(w,1,0);put(w,1,0);put(w,1,0);                                                  /* channel 1 */
    }
    if(s->fill_dse){put(w,3,6);put(w,4,1);put(w,8,0x33);}
    put(w,3,7);align(w);
}
static size_t build(uint8_t *out,const Spec *s){
    Bw w;memset(&w,0,sizeof w);
    memcpy(w.b,"ADIF",4);w.bit=32;
    put(&w,1,s->copyright);if(s->copyright){for(unsigned i=0;i<9;i++)put(&w,8,0x41u+i);}
    put(&w,1,0);put(&w,1,0);put(&w,1,s->vbr);put(&w,23,128000);put(&w,4,s->extra_pce);
    for(unsigned p=0;p<=s->extra_pce;p++){
        if(!s->vbr)put(&w,20,0x7ffff);
        put(&w,4,0);put(&w,2,s->object_type);put(&w,4,s->rate_index);put(&w,4,1);put(&w,4,s->side?1u:0u);put(&w,4,0);put(&w,2,0);put(&w,3,0);put(&w,4,0);
        put(&w,1,s->mixdown);if(s->mixdown)put(&w,4,1);put(&w,1,s->mixdown);if(s->mixdown)put(&w,4,2);put(&w,1,s->mixdown);if(s->mixdown){put(&w,2,1);put(&w,1,0);}
        put(&w,1,s->stereo);put(&w,4,0);                                                                      /* front element: is_cpe, tag */
        if(s->side){put(&w,1,0);put(&w,4,1);}
        align(&w);
        put(&w,8,s->comment);for(unsigned i=0;i<s->comment;i++)put(&w,8,0x30u+i);
    }
    align(&w);
    for(unsigned i=0;i<s->blocks;i++)block(&w,s,i);
    const size_t n=w.bit/8u;memcpy(out,w.b,n);return n;
}
int main(void){
    static uint8_t f[65536];uint8_t *adts=NULL;size_t al=0;SautiAacAdifInfo info;
    Spec s;memset(&s,0,sizeof s);s.object_type=1;s.rate_index=3;s.blocks=5;
    size_t n=build(f,&s);
    assert(sauti_aac_adif_probe(f,n)&&!sauti_aac_adif_probe(f,5)&&!sauti_aac_adif_probe(NULL,100));
    assert(sauti_aac_adif_to_adts(f,n,&adts,&al,&info)==1&&info.frames==5u&&info.rate_index==3u&&info.sample_rate==48000u&&info.channel_config==1u&&info.bitstream_type==0u);
    assert(adts[0]==0xff&&adts[1]==0xf1&&((adts[2]>>2)&15u)==3u&&(((adts[3]&3u)<<11)|(adts[4]<<3)|(adts[5]>>5))==11u);   /* 4-bajtowy blok */
    free(adts);
    for(unsigned variant=0;variant<8;variant++){
        Spec v=s;
        switch(variant){
            case 0:v.stereo=1;break;                       /* CPE */
            case 1:v.vbr=1;break;                          /* bitstream_type 1: bez adif_buffer_fullness */
            case 2:v.copyright=1;break;                    /* copyright_id (72 bity) */
            case 3:v.mixdown=1;v.comment=7;break;          /* mixdown w PCE, komentarz */
            case 4:v.fill_dse=1;break;                     /* DSE i FIL przed i po elemencie kanalowym */
            case 5:v.stereo=1;v.fill_dse=1;v.vbr=1;v.copyright=1;v.mixdown=1;v.comment=3;break;
            case 6:v.rate_index=6;break;                   /* 24 kHz */
            default:v.blocks=1;break;
        }
        n=build(f,&v);adts=NULL;
        assert(sauti_aac_adif_to_adts(f,n,&adts,&al,&info)==1&&info.frames==v.blocks&&info.channel_config==(v.stereo?2u:1u)&&info.rate_index==v.rate_index);
        /* kazdy blok ma ta sama dlugosc, ADTS wskazuje ja poprawnie i nastepny naglowek zaczyna sie od synchronizacji */
        size_t pos=0;unsigned count=0;
        while(pos<al){
            assert(adts[pos]==0xff&&(adts[pos+1]&0xf0u)==0xf0u);
            const size_t l=((size_t)(adts[pos+3]&3u)<<11)|((size_t)adts[pos+4]<<3)|(adts[pos+5]>>5);
            assert(l>7u&&pos+l<=al);pos+=l;count++;
        }
        assert(pos==al&&count==v.blocks);
        free(adts);
    }
    /* nieobslugiwane: wiele programow, inny profil (Main), kanal boczny, losowe dane; bez PCM zastepczego */
    {   Spec v=s;v.extra_pce=1;n=build(f,&v);adts=NULL;assert(sauti_aac_adif_to_adts(f,n,&adts,&al,&info)==0&&!adts);}
    {   Spec v=s;v.object_type=0;n=build(f,&v);adts=NULL;assert(sauti_aac_adif_to_adts(f,n,&adts,&al,&info)==0&&!adts);}
    {   Spec v=s;v.side=1;n=build(f,&v);adts=NULL;assert(sauti_aac_adif_to_adts(f,n,&adts,&al,&info)==0&&!adts);}
    { uint8_t g[200];memcpy(g,"ADIF",4);for(unsigned i=4;i<sizeof g;i++)g[i]=(uint8_t)(i*37u+11u);adts=NULL;assert(sauti_aac_adif_to_adts(g,sizeof g,&adts,&al,&info)==0);}
    /* ucięty ostatni blok jest pomijany, kompletne zostaja */
    n=build(f,&s);
    assert(sauti_aac_adif_to_adts(f,n-2u,&adts,&al,&info)==1&&info.frames==4u);free(adts);
    /* adapter: dekoduje (cisza), seek */
    {   Spec v=s;v.stereo=1;n=build(f,&v);
        SautiDecoder dec;const SautiDecoderOps *const reg[]={&sauti_decoder_aac_adif_native_ops};
        assert(sauti_legacy_decoder_open(&dec,reg,1,f,n));
        assert(dec.info.channels==2&&dec.info.sample_rate==48000u&&dec.info.total_frames==5u*1024u&&dec.ops==&sauti_decoder_aac_adif_native_ops);
        static float pcm[16384];size_t got,all=0;
        while((got=sauti_legacy_decoder_read(&dec,pcm,1000))>0){for(size_t i=0;i<got*2u;i++)assert(pcm[i]==0.0f);all+=got;}
        assert(all==5u*1024u);
        assert(sauti_legacy_decoder_seek(&dec,1500)&&sauti_legacy_decoder_read(&dec,pcm,8000)==5u*1024u-1500u);
        sauti_legacy_decoder_close(&dec);
    }
    puts("PASS AAC ADIF: naglowek i PCE, granice blokow przez parsowanie (SCE/CPE/FIL/DSE/END), CBR/VBR, odrzucenia, adapter");
    return 0;
}
