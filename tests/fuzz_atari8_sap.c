/* Deterministic mutation smoke test. Run with ASan/UBSan.
 * No external fixtures, no guest execution outside the emulator.
 * Arbitrary header mutations are parser-only; executable mutations
 * retain a short FASTPLAY and returning B/C INIT to bound work. */
#include "../src/decoders/retro/atari8/atari8_sap_stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static uint32_t rng=0x6502a8u;
static uint32_t rnd(void)
{
    rng^=rng<<13; rng^=rng>>17; rng^=rng<<5;
    return rng;
}

static size_t fixture(uint8_t *buf,unsigned type,size_t *payload)
{
    static const char *const headers[]={
        "SAP\r\nTYPE B\r\nINIT 2000\r\nPLAYER 2006\r\nFASTPLAY 1\r\nTIME 00:00.003\r\nNAME \"Fuzz\"\r\n",
        "SAP\r\nTYPE C\r\nMUSIC 2000\r\nPLAYER 2000\r\nFASTPLAY 1\r\nTIME 00:00.003\r\n",
        "SAP\r\nTYPE D\r\nINIT 2006\r\nPLAYER 2003\r\nFASTPLAY 1\r\nTIME 00:00.003\r\n",
        "SAP\r\nTYPE R\r\nSTEREO\r\nFASTPLAY 1\r\n\r\n",
        "SAP\r\nTYPE S\r\nINIT 2006\r\nFASTPLAY 1\r\nTIME 00:00.003\r\n"
    };
    size_t n=strlen(headers[type]);
    memcpy(buf,headers[type],n);
    if (type==3) {
        *payload=n;
        memset(buf+n,0,18u*8u);
        for(unsigned i=0;i<8;++i) {
            buf[n+i*18u]=31; buf[n+i*18u+1]=0xaf;
            buf[n+i*18u+9]=57; buf[n+i*18u+10]=0xa7;
        }
        return n+18u*8u;
    }
    static const uint8_t seg[]={0xff,0xff,0,0x20,37,0x20};
    memcpy(buf+n,seg,sizeof(seg)); n+=sizeof(seg);
    memset(buf+n,0xea,38);
    buf[n]=buf[n+3]=0x60;
    *payload=n+6;
    static const uint8_t tone[]={0xa9,0xaf,0x8d,1,0xd2,0x60};
    memcpy(buf+n+6,tone,sizeof(tone));
    if(type==2 || type==4) {
        buf[n+11]=0x4c; buf[n+12]=6; buf[n+13]=0x20;
    }
    return n+38;
}

static void parser_case(const uint8_t *data,size_t n,
                        unsigned *accepted_b,unsigned *accepted_r)
{
    /* Copy into an exactly-sized allocation to catch reads at the
     * actual input boundary, not just beyond a large scratch buffer. */
    uint8_t *p=malloc(n?n:1);
    CHECK(p);
    memcpy(p,data,n);
    Atari8SapbInfo b;
    Atari8SaprInfo r;
    if (atari8_sapb_parse(p,n,&b)==ATARI8_SAPB_OK) {
        ++*accepted_b;
        CHECK(b.songs && b.songs<=256u && b.default_song<b.songs);
        CHECK(b.binary_offset<n && b.interval);
        CHECK(b.type=='B' || b.type=='C' || b.type=='D' || b.type=='S');
    }
    if (atari8_sapr_parse(p,n,&r)==ATARI8_SAPR_OK) {
        ++*accepted_r;
        size_t stride=r.stereo?18u:9u;
        CHECK(r.data>=p && r.data<p+n && r.frame_count && r.cycles_per_frame);
        CHECK((size_t)(p+n-r.data)/stride==r.frame_count);
        CHECK((size_t)(p+n-r.data)%stride==0);
    }
    free(p);
}

static void runtime_case(uint8_t *data,size_t n,unsigned type,
                         unsigned *opened,unsigned *faults)
{
    Atari8SapStream *s=NULL;
    bool restart=(type==0 || type==2) && (rnd()&1u);
    Atari8SapbResult r=atari8_sap_stream_open(&s,data,n,0,8000,true,restart);
    if(r!=ATARI8_SAPB_OK) {
        CHECK(!s);
        ++*faults;
        return;
    }
    ++*opened;
    const Atari8SapStreamInfo *info=atari8_sap_stream_info(s);
    CHECK(info && info->type=="BCDRS"[type]);
    CHECK(info->channels==1u || info->channels==2u);
    uint64_t length=0;
    bool known=atari8_sap_stream_length(s,&length);
    CHECK(known);
    int16_t pcm[16][2];
    size_t got=0;
    r=atari8_sap_stream_read(s,pcm,16,&got);
    CHECK(got<=16u && atari8_sap_stream_tell(s)==got);
    CHECK(r!=ATARI8_SAPB_OK || got==16u);
    if(info->channels==1)
        for(size_t i=0;i<got;++i) CHECK(pcm[i][1]==0);
    if(r!=ATARI8_SAPB_OK && r!=ATARI8_SAPB_EOF) ++*faults;
    uint64_t before=atari8_sap_stream_tell(s);
    CHECK(atari8_sap_stream_seek(s,length+1u)==ATARI8_SAPB_BAD_FORMAT);
    CHECK(atari8_sap_stream_tell(s)==before);
    uint64_t target=length<8u?length:8u;
    r=atari8_sap_stream_seek(s,target);
    CHECK(atari8_sap_stream_tell(s)==(r==ATARI8_SAPB_OK?target:before));
    if(r!=ATARI8_SAPB_OK) ++*faults;
    if(atari8_sap_stream_reset(s)==ATARI8_SAPB_OK) {
        CHECK(atari8_sap_stream_tell(s)==0);
        CHECK(atari8_sap_stream_set_repeat(s,true)==(length!=0));
        r=atari8_sap_stream_read(s,pcm,16,&got);
        CHECK(got<=16u && (r!=ATARI8_SAPB_OK || got==16u));
    }
    atari8_sap_stream_close(s);
}

int main(void)
{
    uint8_t base[512],data[512];
    unsigned accepted_b=0,accepted_r=0,parser_cases=0;
    unsigned opened[5]={0},faults[5]={0};
    for(unsigned type=0;type<5;++type) {
        size_t payload,n=fixture(base,type,&payload);
        /* Exhaustive truncations of each canonical input. */
        for(size_t k=0;k<=n;++k) {
            parser_case(base,k,&accepted_b,&accepted_r);
            ++parser_cases;
        }
        for(unsigned trial=0;trial<10000u;++trial) {
            memcpy(data,base,n);
            size_t len=n;
            unsigned edits=1u+rnd()%8u;
            for(unsigned j=0;j<edits;++j) {
                size_t pos=rnd()%len;
                switch(rnd()%4u) {
                case 0: data[pos]^=(uint8_t)(1u<<(rnd()%8u)); break;
                case 1: data[pos]=(uint8_t)rnd(); break;
                case 2: if(len>1) { memmove(data+pos,data+pos+1,len-pos-1); --len; } break;
                default:
                    if(len<sizeof(data)) {
                        memmove(data+pos+1,data+pos,len-pos);
                        data[pos]=(uint8_t)rnd(); ++len;
                    }
                    break;
                }
            }
            parser_case(data,len,&accepted_b,&accepted_r);
            ++parser_cases;
        }
        /* Unmutated baseline plus random register/player bytes.
         * B/C INIT and D PLAYER remain RTS; D/S INIT is bounded by
         * the short interval. No TIME or FASTPLAY mutations here. */
        for(unsigned trial=0;trial<500u;++trial) {
            memcpy(data,base,n);
            if(trial) {
                unsigned edits=1u+rnd()%12u;
                for(unsigned j=0;j<edits;++j)
                    data[payload+rnd()%(n-payload)]=(uint8_t)rnd();
            }
            runtime_case(data,n,type,&opened[type],&faults[type]);
        }
        CHECK(opened[type]>0);
        printf("TYPE %c: 500 runtime cases, %u opened, %u fault observations\n",
               "BCDRS"[type],opened[type],faults[type]);
    }
    /* Unstructured bytes include embedded NULs and high-bit characters. */
    for(unsigned i=0;i<5000u;++i) {
        size_t n=rnd()%sizeof(data);
        for(size_t k=0;k<n;++k) data[k]=(uint8_t)rnd();
        parser_case(data,n,&accepted_b,&accepted_r);
        ++parser_cases;
    }
    CHECK(accepted_b && accepted_r);
    printf("PASS: %u parser cases (%u executable, %u register accepted), 2500 runtime cases\n",
           parser_cases,accepted_b,accepted_r);
    return 0;
}
