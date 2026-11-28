/* Test-fixture tool (not a product): writes TwinVQ access units (MPEG-4 Audio object type 7, base layer) field by field with plausible random values, so that every tool of the syntax (stereo mask,
 * long term prediction of long and short windows, TNS, band limiting, pitch, all window sequences and shapes) occurs and the amplitude stays moderate.  The frames are valid by construction; the
 * decoder under test and the reference decoder both decode them, and their output is compared (see docs/AAC_COMPLETENESS_PLAN.md, section TwinVQ).
 * usage: twinvq_gen SEED RATE_INDEX CHANNELS BLOCK AU_BYTES FRAMES OUT.aus      (OUT: 4-byte big-endian length + bytes per access unit, the format of tools/mp4_dump_aus.py)
 * build: cc -O1 -Iinclude -o twinvq_gen tools/twinvq_gen.c src/decoders/modern/aac/sauti_aac_twinvq.c src/decoders/modern/aac/sauti_aac_twinvq_tables.c <the AAC filter bank sources> -lm */
#include "sauti_aac_twinvq.h"
#include "sauti_aac_bands.h"
#include "sauti_aac_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char au[8192];static size_t nbits;
static unsigned rnd(unsigned n){return n?(unsigned)(((unsigned long long)rand()*(unsigned)n)/((unsigned)RAND_MAX+1ull)):0u;}
static void put(unsigned v,unsigned w){while(w--){const unsigned b=(v>>w)&1u;if(b)au[nbits>>3]|=(unsigned char)(0x80u>>(nbits&7u));nbits++;}}
static void putr(unsigned w){put(rnd(1u<<w),w);}
int main(int argc,char **argv){
    if(argc<8){fprintf(stderr,"usage: %s SEED RATE_INDEX CHANNELS BLOCK AU_BYTES FRAMES OUT.aus\n",argv[0]);return 2;}
    const unsigned seed=(unsigned)atoi(argv[1]),ri=(unsigned)atoi(argv[2]),ch=(unsigned)atoi(argv[3]),block=(unsigned)atoi(argv[4]),len=(unsigned)atoi(argv[5]),n=(unsigned)atoi(argv[6]);
    FILE *o=fopen(argv[7],"wb");
    SautiAacTwinVq *st=sauti_aac_twinvq_new(ri,ch,block,len);
    static float pcm[4096];
    if(!o||!st||len>sizeof au){fprintf(stderr,"bad parameters\n");return 1;}
    srand(seed);
    const unsigned idx=SAUTI_AAC_IDX(ri,block==960u?SAUTI_AAC_LAYOUT_960:SAUTI_AAC_LAYOUT_1024);
    unsigned made=0,tries=0,f_vq=0,f_bits=0,f_dec=0,f_amp=0;
    while(made<n&&tries<n*50u){
        tries++;
        memset(au,0,sizeof au);nbits=0;
        const unsigned ws=rnd(4),shape=rnd(2),longf=ws!=2u,nsf=longf?1u:8u;
        const unsigned max_sfb=sauti_aac_twinvq_max_sfb(st,ws);
        const uint16_t *off=NULL;unsigned nb=0;
        if(!sauti_aac_band_offsets(idx,longf?0u:1u,&off,&nb))return 1;
        put(ws,2);put(shape,1);
        if(ch>1u){
            const unsigned ms=rnd(3);put(ms,2);
            if(ms==1u){
                unsigned ng=1;
                if(!longf){const unsigned gc=rnd(128);put(gc,7);ng=1;for(unsigned m=0;m<7u;m++)if(((gc>>(6u-m))&1u)==0u)ng++;}
                for(unsigned g=0;g<ng;g++)for(unsigned sfb=0;sfb<max_sfb;sfb++)putr(1);
            }
        }
        for(unsigned c=0;c<ch;c++){
            if(rnd(2)){                                              /* long term prediction */
                put(1,1);put(100+rnd(1900),11);putr(3);
                if(longf){const unsigned last=(max_sfb<40u?max_sfb:40u);for(unsigned i=0;i<last;i++)putr(1);}
                else for(unsigned i=0;i<8u;i++){const unsigned u=rnd(2);put(u,1);if(u){const unsigned lp=rnd(2);put(lp,1);if(lp)put(rnd(32),5);}}
            }else put(0,1);
            if(rnd(5)<2){                                            /* TNS */
                put(1,1);
                for(unsigned w=0;w<(longf?1u:8u);w++){
                    const unsigned maxf=longf?2u:1u,nf=rnd(maxf+1u);put(nf,longf?2u:1u);
                    if(!nf)continue;
                    const unsigned res=rnd(2);put(res,1);
                    unsigned top=nb;
                    for(unsigned f=0;f<nf;f++){
                        unsigned lenb=1+rnd(top>1u?(top/2u>1u?top/2u:1u):1u);if(lenb>top)lenb=top;
                        if(longf&&lenb>63u)lenb=63u;
                        if(!longf&&lenb>15u)lenb=15u;
                        put(lenb,longf?6u:4u);top-=lenb;
                        const unsigned order=rnd((longf?12u:7u)+1u);put(order,longf?5u:3u);
                        if(order){const unsigned cmp=rnd(2);put(rnd(2),1);put(cmp,1);const unsigned w2=res+3u-cmp;for(unsigned k=0;k<order;k++)putr(w2);}
                    }
                }
            }else put(0,1);
        }
        /* BitUnPack */
        const unsigned bandlimit=rnd(4)==0,pitchq=longf?rnd(2):0,pf=longf?rnd(2):0;
        if(longf){put(bandlimit,1);put(pitchq,1);put(pf,1);}else put(bandlimit,1);
        const unsigned side=longf?(19u+9u+43u)*ch+3u:(19u+(9u+4u*8u))*ch+1u;
        int vq=(int)(len*8u)-(int)nbits-(int)side+(longf?3:1);          /* the three (one) flag bits are part of `side` and are already written */
        if(longf&&pitchq)vq-=(int)((28u+8u+7u)*ch);
        if(bandlimit)vq-=(int)(3u*ch);
        if(vq<=24){f_vq++;continue;}
        if(bandlimit)for(unsigned c=0;c<ch;c++){put(rnd(4),2);put(rnd(2),1);}
        if(longf&&pitchq){
            const unsigned nd=2u*ch;
            for(unsigned i=0;i<nd;i++){const unsigned bits=(28u*ch+nd-1u-i)/nd;put(rnd(1u<<((bits+1u)/2u)),(bits+1u)/2u);put(rnd(1u<<(bits/2u)),bits/2u);}
            for(unsigned c=0;c<ch;c++){put(rnd(256),8);put(rnd(128),7);}
        }
        const unsigned ndiv=((unsigned)vq+11u)/12u;
        for(unsigned i=0;i<ndiv;i++){const unsigned bits=((unsigned)vq+ndiv-1u-i)/ndiv;putr((bits+1u)/2u);putr(bits/2u);}
        if(longf){for(unsigned c=0;c<ch;c++)for(unsigned i=0;i<7u;i++)putr(6);for(unsigned c=0;c<ch;c++)putr(1);}
        for(unsigned c=0;c<ch;c++){
            put(10+rnd(110),9);                                      /* gain: moderate */
            if(!longf)for(unsigned i=0;i<nsf;i++)put(4+rnd(8),4);
        }
        for(unsigned c=0;c<ch;c++){putr(1);putr(6);putr(4);putr(4);putr(4);}
        if(nbits!=len*8u){f_bits++;continue;}
        SautiAacTwinVq *cl=sauti_aac_twinvq_dup(st);SautiAacReason why;
        if(!sauti_aac_twinvq_decode(cl,au,len,pcm,&why)){sauti_aac_twinvq_free(cl);f_dec++;continue;}
        float mx=0;for(unsigned i=0;i<block*ch;i++){const float a=pcm[i]<0?-pcm[i]:pcm[i];if(a>mx)mx=a;}
        if(mx>0.9f){sauti_aac_twinvq_free(cl);f_amp++;continue;}
        sauti_aac_twinvq_free(st);st=cl;
        unsigned char b4[4]={0,0,(unsigned char)(len>>8),(unsigned char)len};fwrite(b4,1,4,o);fwrite(au,1,len,o);made++;
    }
    fclose(o);
    printf("made %u frames in %u tries (rejected: size %u, bits %u, decode %u, amplitude %u)\n",made,tries,f_vq,f_bits,f_dec,f_amp);
    return made==n?0:1;
}
