/* AAC-LC wielokanalowy: petla po elementach raw_data_block (SCE/CPE/LFE/DSE/PCE/FIL/END), jeden wspolny strumien PNS, atomowa aktualizacja stanu. */
#include "sauti_aac_lc_multi.h"
#include "sauti_aac_syntax.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int get(const uint8_t *p,size_t n,size_t *pos,unsigned bits,uint32_t *v){
    return sauti_aac_bits_read_asm(p,n,pos,bits,v);
}
unsigned sauti_aac_lc_multi_config_channels(unsigned config){
    static const uint8_t channels[8]={0,1,2,3,4,5,6,8};
    return config<8u?channels[config]:0u;
}
int sauti_aac_lc_multi_output_map(unsigned config,uint8_t map[SAUTI_AAC_LC_MULTI_MAX]){
    /* kolejnosc strumienia -> kolejnosc FFmpeg/WAVE:
     *  1: C                      -> C
     *  2: L R                    -> FL FR
     *  3: C L R                  -> FL FR FC
     *  4: C L R S                -> FL FR FC BC
     *  5: C L R Ls Rs            -> FL FR FC BL BR
     *  6: C L R Ls Rs LFE        -> FL FR FC LFE BL BR
     *  7: C L R SL SR BL BR LFE  -> FL FR FC LFE BL BR SL SR */
    static const uint8_t m[8][SAUTI_AAC_LC_MULTI_MAX]={
        {0},{0},{0,1},{1,2,0},{1,2,0,3},{1,2,0,3,4},{1,2,0,5,3,4},{1,2,0,7,5,6,3,4}};
    if(!map||config<1u||config>7u)return 0;
    memcpy(map,m[config],SAUTI_AAC_LC_MULTI_MAX);
    return 1;
}
int sauti_aac_lc_multi_init(SautiAacLcMulti *state,unsigned config,unsigned previous_shape){
    const unsigned channels=sauti_aac_lc_multi_config_channels(config);
    if(!state||!channels)return 0;
    SautiAacLcMulti next;
    memset(&next,0,sizeof(next));
    next.channels=channels;next.config=config;
    for(unsigned i=0;i<channels;i++)
        if(!sauti_aac_lc_synthesis_init(&next.ch[i],previous_shape))return 0;
    next.pns_seed=next.ch[0].pns_seed;
    *state=next;
    return 1;
}
/* data_stream_element: tag(4) align(1) count(8)[+esc(8)] [align] bajty.  Wyrownanie wzgledem poczatku raw_data_block (tak samo robi FFmpeg). */
static int skip_dse(const uint8_t *raw,size_t length,size_t *pos){
    uint32_t tag,align,count,esc=0;
    if(!get(raw,length,pos,4,&tag)||!get(raw,length,pos,1,&align)||!get(raw,length,pos,8,&count))return 0;
    if(count==255u){if(!get(raw,length,pos,8,&esc))return 0;count+=esc;}
    if(align)while(*pos&7u){uint32_t b;if(!get(raw,length,pos,1,&b))return 0;}
    if(count>(length*8u-*pos)/8u)return 0;
    *pos+=(size_t)count*8u;
    return 1;
}
/* program_config_element: tylko pominiecie (uklad bierzemy z channel_configuration ADTS). */
static int skip_pce(const uint8_t *raw,size_t length,size_t *pos){
    uint32_t v,front,side,back,lfe,assoc,cc;
    if(!get(raw,length,pos,4,&v)||!get(raw,length,pos,2,&v)||!get(raw,length,pos,4,&v)||
       !get(raw,length,pos,4,&front)||!get(raw,length,pos,4,&side)||!get(raw,length,pos,4,&back)||
       !get(raw,length,pos,2,&lfe)||!get(raw,length,pos,3,&assoc)||!get(raw,length,pos,4,&cc))return 0;
    for(unsigned k=0;k<2;k++){                                /* mono_mixdown, stereo_mixdown */
        if(!get(raw,length,pos,1,&v))return 0;
        if(v&&!get(raw,length,pos,4,&v))return 0;
    }
    if(!get(raw,length,pos,1,&v))return 0;                    /* matrix_mixdown_idx_present */
    if(v&&(!get(raw,length,pos,2,&v)||!get(raw,length,pos,1,&v)))return 0;
    for(uint32_t i=0;i<front+side+back;i++)if(!get(raw,length,pos,1,&v)||!get(raw,length,pos,4,&v))return 0;
    for(uint32_t i=0;i<lfe+assoc;i++)if(!get(raw,length,pos,4,&v))return 0;
    for(uint32_t i=0;i<cc;i++)if(!get(raw,length,pos,1,&v)||!get(raw,length,pos,4,&v))return 0;
    while(*pos&7u)if(!get(raw,length,pos,1,&v))return 0;
    if(!get(raw,length,pos,8,&v))return 0;                    /* comment_field_bytes */
    if(v>(length*8u-*pos)/8u)return 0;
    *pos+=(size_t)v*8u;
    return 1;
}
/* Pozycje wyjsciowe (kolejnosc bitow ukladu FFmpeg): FL0 FR1 FC2 LFE3 BL4 BR5 FLC6 FRC7 BC8 SL9 SR10. */
enum { POS_FL=0,POS_FR=1,POS_FC=2,POS_LFE=3,POS_BL=4,POS_BR=5,POS_FLC=6,POS_FRC=7,POS_BC=8,POS_SL=9,POS_SR=10 };
/* PCE -> uklad.  Zwraca 1 (ok), 0 (bledne/ucieta) albo -1 (uklad nieobslugiwany). */
static int parse_pce_layout(const uint8_t *raw,size_t length,size_t *pos,SautiAacLcLayout *out){
    uint32_t v,front,side,back,lfe,assoc,cc;
    if(!get(raw,length,pos,4,&v)||!get(raw,length,pos,2,&v)||!get(raw,length,pos,4,&v)||
       !get(raw,length,pos,4,&front)||!get(raw,length,pos,4,&side)||!get(raw,length,pos,4,&back)||
       !get(raw,length,pos,2,&lfe)||!get(raw,length,pos,3,&assoc)||!get(raw,length,pos,4,&cc))return 0;
    for(unsigned k=0;k<2;k++){
        if(!get(raw,length,pos,1,&v))return 0;
        if(v&&!get(raw,length,pos,4,&v))return 0;
    }
    if(!get(raw,length,pos,1,&v))return 0;
    if(v&&(!get(raw,length,pos,2,&v)||!get(raw,length,pos,1,&v)))return 0;
    uint8_t cls[16],cpe[16];unsigned n=0;
    const uint32_t counts[3]={front,side,back};
    for(unsigned c=0;c<3;c++)for(uint32_t i=0;i<counts[c];i++){
        uint32_t is_cpe,tag;
        if(!get(raw,length,pos,1,&is_cpe)||!get(raw,length,pos,4,&tag))return 0;
        if(n>=16u)return -1;
        cls[n]=(uint8_t)c;cpe[n]=(uint8_t)is_cpe;n++;
    }
    for(uint32_t i=0;i<lfe;i++){uint32_t tag;if(!get(raw,length,pos,4,&tag))return 0;if(n>=16u)return -1;cls[n]=3;cpe[n]=0;n++;}
    for(uint32_t i=0;i<assoc;i++)if(!get(raw,length,pos,4,&v))return 0;
    for(uint32_t i=0;i<cc;i++)if(!get(raw,length,pos,1,&v)||!get(raw,length,pos,4,&v))return 0;
    while(*pos&7u)if(!get(raw,length,pos,1,&v))return 0;
    if(!get(raw,length,pos,8,&v))return 0;
    if(v>(length*8u-*pos)/8u)return 0;
    *pos+=(size_t)v*8u;
    if(cc)return -1;                                    /* CCE */
    /* kanaly dekodowane w kolejnosci elementow; kazdemu przypisujemy pozycje wyjsciowa */
    int8_t position[16];unsigned channels=0;
    unsigned front_pairs=0,front_sce=0,back_pairs=0,back_sce=0,side_pairs=0,lfes=0,back_cpe_total=0;
    for(unsigned e=0;e<n;e++)if(cls[e]==2u&&cpe[e])back_cpe_total++;
    const unsigned second_back_pair=back_cpe_total==2u;
    for(unsigned e=0;e<n;e++){
        const unsigned need=cpe[e]?2u:1u;
        if(channels+need>SAUTI_AAC_LC_MULTI_MAX)return -1;
        if(cls[e]==0){
            if(cpe[e]){ if(front_pairs>=2u)return -1; position[channels]=front_pairs?POS_FLC:POS_FL; position[channels+1u]=front_pairs?POS_FRC:POS_FR; front_pairs++; }
            else { if(front_sce)return -1; position[channels]=POS_FC; front_sce=1u; }
        }else if(cls[e]==1){
            if(!cpe[e]||side_pairs)return -1;
            position[channels]=POS_SL; position[channels+1u]=POS_SR; side_pairs=1u;
        }else if(cls[e]==2){
            if(cpe[e]){
                /* dwie pary w klasie "back" bez pary bocznej (fdk-aac 7.1): pierwsza = boczna, druga = tylna, jak w FFmpeg */
                if(back_pairs>=2u||(back_pairs==1u&&side_pairs&&!second_back_pair))return -1;
                const int as_side=(back_pairs==0u&&!side_pairs&&second_back_pair);
                position[channels]=as_side?POS_SL:POS_BL; position[channels+1u]=as_side?POS_SR:POS_BR;
                back_pairs++;
            }
            else { if(back_sce)return -1; position[channels]=POS_BC; back_sce=1u; }
        }else{
            if(lfes)return -1;
            position[channels]=POS_LFE; lfes=1u;
        }
        channels+=need;
    }
    if(!channels)return -1;
    SautiAacLcLayout l;memset(&l,0,sizeof(l));l.channels=channels;
    unsigned o=0;
    for(int bit=0;bit<=POS_SR;bit++)for(unsigned c=0;c<channels;c++)if(position[c]==bit)l.map[o++]=(uint8_t)c;
    if(o!=channels)return -1;
    *out=l;
    return 1;
}
int sauti_aac_lc_multi_scan_pce(const uint8_t *frame,size_t length,SautiAacLcLayout *layout){
    SautiAacAdts adts={0};
    if(!frame||!layout||!sauti_aac_adts_parse_asm(frame,length,&adts)||adts.frame_bytes>length||adts.header_bytes>adts.frame_bytes||
       adts.core_channels!=0u)return 0;
    const uint8_t *raw=frame+adts.header_bytes;const size_t raw_length=adts.frame_bytes-adts.header_bytes;
    size_t pos=0;
    for(;;){
        uint32_t id=0;
        if(!get(raw,raw_length,&pos,3,&id))return 0;
        if(id==5u)return parse_pce_layout(raw,raw_length,&pos,layout);
        if(id==4u){if(!skip_dse(raw,raw_length,&pos))return 0;continue;}
        if(id==6u){if(sauti_aac_fill_skip_non_sbr(raw,raw_length,&pos,NULL)!=1)return 0;continue;}
        return 0;                                       /* element kanalowy albo END przed PCE */
    }
}
int sauti_aac_lc_multi_init_layout(SautiAacLcMulti *state,const SautiAacLcLayout *layout,unsigned previous_shape){
    if(!state||!layout||layout->channels<1u||layout->channels>SAUTI_AAC_LC_MULTI_MAX)return 0;
    SautiAacLcMulti next;memset(&next,0,sizeof(next));
    next.channels=layout->channels;next.config=0;
    memcpy(next.map,layout->map,sizeof(next.map));
    for(unsigned i=0;i<next.channels;i++)
        if(!sauti_aac_lc_synthesis_init(&next.ch[i],previous_shape))return 0;
    next.pns_seed=next.ch[0].pns_seed;
    *state=next;
    return 1;
}
static int scale_out(float *dst,const float *src,size_t stride,size_t n){
    for(size_t i=0;i<n;i++){
        const float v=ldexpf(src[i],-16);                   /* 2^-16: konwencja jednostek wewnetrznych jak w mono/stereo */
        if(!isfinite(v))return 0;
        dst[i*stride]=v;
    }
    return 1;
}
int sauti_aac_lc_adts_multi_decode_ex(const uint8_t *frame,size_t length,
                                      SautiAacLcMulti *state,float *pcm,
                                      SautiAacReason *reason){
    if(!reason)return 0;
    *reason=SAUTI_AAC_REASON_INVALID_ADTS;
    if(!frame||!state||!pcm||state->channels<1u||state->channels>SAUTI_AAC_LC_MULTI_MAX)return 0;
    SautiAacAdts adts={0};
    if(!sauti_aac_adts_parse_asm(frame,length,&adts))return 0;
    if(adts.frame_bytes>length||adts.header_bytes>adts.frame_bytes)return 0;
    if(adts.core_aot!=2u||adts.raw_blocks!=1u||adts.core_channels!=state->config||adts.crc_present){
        *reason=SAUTI_AAC_REASON_UNSUPPORTED_ADTS;return -1;
    }
    const unsigned rate=(frame[2]>>2u)&15u;
    if(rate>=13u)return 0;
    for(unsigned i=0;i<state->channels;i++)
        if(state->ch[i].initialized!=1u){*reason=SAUTI_AAC_REASON_UNSUPPORTED_HISTORY;return -1;}
    const uint8_t *raw=frame+adts.header_bytes;
    const size_t raw_length=adts.frame_bytes-adts.header_bytes;
    size_t pos=0;
    SautiAacLcMulti next=*state;                              /* atomowosc: pracujemy na kopii */
    float stage[SAUTI_AAC_LC_MULTI_MAX][1024];                /* kolejnosc dekodowania */
    unsigned used=0;
    uint32_t shared_pns=next.pns_seed;
    for(;;){
        size_t probe=pos;
        uint32_t id=0;
        if(!get(raw,raw_length,&probe,3,&id)){*reason=SAUTI_AAC_REASON_INVALID_TRAILING;return 0;}
        if(id==7u){pos=probe;break;}
        if(id==0u||id==3u){                                   /* SCE / LFE */
            if(used+1u>next.channels){*reason=SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT;return 0;}
            SautiAacLcMonoData *el=calloc(1,sizeof(*el));
            if(!el){*reason=SAUTI_AAC_REASON_INVALID_SYNTH;return 0;}
            const int st=sauti_aac_lc_element_parse(raw,raw_length,&pos,rate,id,el,reason);
            if(st!=1){free(el);return st;}
            float tmp[1024];
            next.ch[used].pns_seed=shared_pns;
            const int ok=sauti_aac_lc_synthesis_frame_tns(&next.ch[used],rate,&el->ics,&el->sections,&el->factors,
                                                          el->quant,el->tns_present?&el->tns:NULL,tmp);
            free(el);
            if(!ok||!scale_out(stage[used],tmp,1u,1024u)){*reason=SAUTI_AAC_REASON_INVALID_SYNTH;return 0;}
            shared_pns=next.ch[used].pns_seed;
            used++;
            continue;
        }
        if(id==1u){                                           /* CPE */
            if(used+2u>next.channels){*reason=SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT;return 0;}
            SautiAacCpeData *el=calloc(1,sizeof(*el));
            if(!el){*reason=SAUTI_AAC_REASON_INVALID_SYNTH;return 0;}
            const int st=sauti_aac_lc_cpe_parse(raw,raw_length,&pos,rate,el,reason);
            if(st!=1){free(el);return st;}
            SautiAacLcSynthesis pair[2]={next.ch[used],next.ch[used+1u]};
            pair[0].pns_seed=pair[1].pns_seed=shared_pns;
            float inter[2048];
            const int ss=sauti_aac_lc_cpe_synthesize(el,rate,pair,inter,reason);
            free(el);
            if(ss!=1)return ss;
            next.ch[used]=pair[0];next.ch[used+1u]=pair[1];
            shared_pns=pair[0].pns_seed;
            for(unsigned i=0;i<1024u;i++){stage[used][i]=inter[2u*i];stage[used+1u][i]=inter[2u*i+1u];}
            used+=2u;                                           /* cpe_synthesize zwraca juz PCM w jednostkach wyjsciowych (skalowanie 2^-16 w srodku) */
            continue;
        }
        pos=probe;
        if(id==4u){if(!skip_dse(raw,raw_length,&pos)){*reason=SAUTI_AAC_REASON_INVALID_TRAILING;return 0;}continue;}
        if(id==5u){if(!skip_pce(raw,raw_length,&pos)){*reason=SAUTI_AAC_REASON_INVALID_TRAILING;return 0;}continue;}
        if(id==6u){
            const int sk=sauti_aac_fill_skip_non_sbr(raw,raw_length,&pos,NULL);
            if(sk==-1){*reason=SAUTI_AAC_REASON_UNSUPPORTED_FIL;return -1;}
            if(sk!=1){*reason=SAUTI_AAC_REASON_INVALID_TRAILING;return 0;}
            continue;
        }
        *reason=SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT;           /* 2 = CCE */
        return -1;
    }
    while(pos&7u){
        uint32_t pad=1;
        if(!get(raw,raw_length,&pos,1,&pad)||pad){*reason=SAUTI_AAC_REASON_INVALID_TRAILING;return 0;}
    }
    if(pos!=raw_length*8u||used!=next.channels){*reason=SAUTI_AAC_REASON_INVALID_TRAILING;return 0;}
    next.pns_seed=shared_pns;
    uint8_t map[SAUTI_AAC_LC_MULTI_MAX];
    if(next.config==0u)memcpy(map,next.map,sizeof(map));
    else if(!sauti_aac_lc_multi_output_map(next.config,map)){*reason=SAUTI_AAC_REASON_UNSUPPORTED_ADTS;return -1;}
    float out[SAUTI_AAC_LC_MULTI_MAX*1024];
    for(unsigned c=0;c<next.channels;c++)
        for(unsigned i=0;i<1024u;i++)out[(size_t)i*next.channels+c]=stage[map[c]][i];
    *state=next;
    memcpy(pcm,out,sizeof(float)*1024u*next.channels);
    *reason=SAUTI_AAC_REASON_OK;
    return 1;
}
