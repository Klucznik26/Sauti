/* SBR-83 tool (not a product): builds HE-AAC test streams with a REAL AAC-LC core and a GENERATED SBR payload.
 *
 * Encoders do not emit every SBR feature (all of ours leave bs_smoothing_mode at 1), so a decoder cannot be checked against another decoder
 * on those features with encoder output alone.  Here the SCE of each frame of an existing mono ADTS stream is copied bit for bit and its FIL
 * element is replaced by a valid SBR payload written below: FIXFIX grid with 2 or 4 envelopes, frequency-direction coding with random levels
 * (first value + zero deltas), random inverse-filtering modes, random noise floor levels and random harmonic flags.  Both decoders then read
 * the same bits.  The header goes into frame 0 only.
 *
 * usage: sauti_he_remix IN.aac OUT.aac SEED SMOOTHING(0|1) [MAX_FRAMES] [ENVS(2|4|0=random)] [ALL_CLASSES(0|1)]
 * IN must be a mono 24 kHz-core ADTS stream without CRC (fdkaac -f 2 output). */
#include "sauti_aac_lc_frame.h"
#include "sauti_aac_sbr_limits.h"
#include "sauti_aac_sbr_master.h"
#include "sauti_aac_sbr_patch.h"
#include "sauti_aac_sbr_tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {uint8_t bytes[2048];size_t bit;} Bits;
static void put(Bits *b,unsigned n,unsigned v){
    for(unsigned i=0;i<n;i++){
        const unsigned bit=(v>>(n-1u-i))&1u;
        if(bit)b->bytes[b->bit/8u]|=(uint8_t)(1u<<(7u-(unsigned)(b->bit&7u)));
        b->bit++;
    }
}
static unsigned rnd_state;
static unsigned rnd(void){rnd_state=rnd_state*1664525u+1013904223u;return rnd_state>>8;}

static int full_patch_coverage(const SautiAacSbrTables *tab,const SautiAacSbrPatches *patch){
    for(unsigned k=tab->kx;k<tab->kx+tab->m;k++)if(patch->source_for_target[k]<0)return 0;
    return 1;
}
/* A header whose patches cover every target band (the same search the unit tests use), for a 24 kHz core / 48 kHz output. */
static int pick_header(unsigned *start,unsigned *stop,SautiAacSbrTables *tab,unsigned skip){
    for(unsigned st=0;st<16u;st++)
        for(unsigned sp=0;sp<16u;sp++){
            SautiAacSbrLimits l;SautiAacSbrMaster m;SautiAacSbrPatches p;
            if(!sauti_aac_sbr_limits_compute(48000u,st,sp,&l) ||
               !sauti_aac_sbr_master_linear(l.k0,l.k2,1u,0u,&m) ||
               !sauti_aac_sbr_tables_build(&m,2u,tab) ||
               !sauti_aac_sbr_patch_build(48000u,&m,tab,&p) ||
               tab->n_high<3u || tab->n_high>20u || tab->n_noise>4u || !full_patch_coverage(tab,&p))continue;
            if(skip){skip--;continue;}
            *start=st;*stop=sp;return 1;
        }
    return 0;
}

static unsigned prev_trail=16u;
static void sbr_payload(Bits *b,const SautiAacSbrTables *t,unsigned start,unsigned stop,unsigned smoothing,unsigned header,unsigned envs_opt,unsigned all_classes){
    *b=(Bits){{0},0};
    put(b,4u,13u);                                   /* EXT_SBR_DATA */
    put(b,1u,header);
    if(header){
        put(b,1u,1u);put(b,4u,start);put(b,4u,stop);put(b,3u,0u);  /* amp_res=1, start, stop, xover */
        put(b,2u,0u);put(b,1u,1u);put(b,1u,1u);                    /* reserved, extra_1, extra_2 */
        put(b,2u,0u);put(b,1u,1u);put(b,2u,2u);                    /* freq_scale 0, alter_scale 1, noise_bands 2 */
        put(b,2u,2u);put(b,2u,2u);put(b,1u,1u);put(b,1u,smoothing);/* limiter_bands 2, limiter_gains 2, interpol 1, smoothing */
    }
    put(b,1u,0u);                                    /* bs_data_extra */
    unsigned envs=0,freq_bits=0;
    if(all_classes){
        /* classes and borders as in the standard; a frame must start where the previous one ended (lead border = previous trail - 16) */
        for(;;){
            unsigned cls,lead=0,trail=16,rel0[3]={0},rel1[3]={0},n0=0,n1=0,vb0=0,vb1=0;
            if(prev_trail==16u)cls=rnd()&3u;else cls=2u+(rnd()&1u);
            if(getenv("SAUTI_REMIX_CLASS") && prev_trail==16u)cls=(unsigned)atoi(getenv("SAUTI_REMIX_CLASS"));
            if(cls==2u||cls==3u){vb0=prev_trail-16u;lead=vb0;}
            if(cls==1u||cls==3u){vb1=rnd()&3u;trail=16u+vb1;}
            if(cls==1u)n1=1u+rnd()%3u;
            else if(cls==2u)n0=1u+rnd()%3u;
            else if(cls==3u){n0=rnd()%3u;n1=rnd()%3u;if(n0+n1==0u)n1=1u;}
            if(cls==0u){envs=2u<<(rnd()&1u);}
            else envs=n0+n1+1u;
            if(envs<2u || envs>5u)continue;
            unsigned edges[8],ne=0;
            if(cls==0u){for(unsigned i=0;i<=envs;i++)edges[ne++]=16u*i/envs;}
            else{
                for(unsigned i=0;i<n0;i++)rel0[i]=rnd()&3u;
                for(unsigned i=0;i<n1;i++)rel1[i]=rnd()&3u;
                unsigned pos=lead;edges[ne++]=pos;
                for(unsigned i=0;i<n0;i++){pos+=2u*rel0[i]+2u;edges[ne++]=pos;}
                unsigned back[4],nb=0,q=trail;
                for(unsigned i=0;i<n1;i++){q-=2u*rel1[i]+2u;back[nb++]=q;}
                for(int i=(int)nb-1;i>=0;i--)edges[ne++]=back[i];
                edges[ne++]=trail;
            }
            int ok=ne==envs+1u;
            for(unsigned i=0;ok && i+1<ne;i++)if(edges[i+1]<=edges[i])ok=0;
            if(ok && cls!=0u && edges[0]!=lead)ok=0;
            if(!ok)continue;
            put(b,2u,cls);
            if(cls==0u){put(b,2u,envs==2u?1u:2u);freq_bits=1u;}
            else{
                if(cls==3u){put(b,2u,vb0);put(b,2u,vb1);put(b,2u,n0);put(b,2u,n1);}
                else if(cls==1u){put(b,2u,vb1);put(b,2u,n1);}
                else{put(b,2u,vb0);put(b,2u,n0);}
                for(unsigned i=0;i<n0;i++)put(b,2u,rel0[i]);
                for(unsigned i=0;i<n1;i++)put(b,2u,rel1[i]);
                if(cls!=0u){
                    unsigned ptr_bits=0;while((1u<<ptr_bits)<envs+1u)ptr_bits++;   /* ceil(log2(num_env+1)) */
                    unsigned ptr=rnd()%(envs+1u);       /* noise border = envelope border (pointer-1 or envs+1-pointer): keep it inside */
                    put(b,ptr_bits,ptr);
                    if(getenv("SAUTI_REMIX_PLAN"))fprintf(stderr,"PTR %u bits %u ",ptr,ptr_bits);
                }
                freq_bits=envs;
            }
            if(getenv("SAUTI_REMIX_PLAN")){fprintf(stderr,"PLAN class %u envs %u edges",cls,envs);for(unsigned i=0;i<ne;i++)fprintf(stderr," %u",edges[i]);fprintf(stderr,"\n");}
            prev_trail=trail;
            break;
        }
    }else{
        unsigned log2env=envs_opt==2u?1u:envs_opt==4u?2u:1u+(rnd()&1u);
        envs=1u<<log2env;
        put(b,2u,0u);put(b,2u,log2env);freq_bits=1u;
    }
    unsigned freq_res=0;
    freq_res=rnd()&1u;
    if(getenv("SAUTI_REMIX_PLAN"))fprintf(stderr,"FR %u ",freq_res);
    /* one value for every envelope: the order of the bits then does not matter */
    for(unsigned i=0;i<freq_bits;i++)put(b,1u,freq_res);
    const unsigned noises=2u;                        /* envs > 1 */
    for(unsigned i=0;i<envs+noises;i++)put(b,1u,0u); /* all frequency direction */
    for(unsigned i=0;i<t->n_noise;i++)put(b,2u,rnd()&3u);   /* inverse filtering mode per noise band */
    const unsigned nb=freq_res?t->n_high:t->n_low;
    for(unsigned e=0;e<envs;e++){
        put(b,6u,18u+rnd()%30u);                     /* first band level */
        for(unsigned i=1;i<nb;i++)put(b,1u,0u);      /* Huffman code of delta 0 */
    }
    for(unsigned e=0;e<noises;e++){
        put(b,5u,2u+rnd()%10u);
        for(unsigned i=1;i<t->n_noise;i++)put(b,1u,0u);
    }
    const unsigned harm=(rnd()%3u)==0u;
    put(b,1u,harm);
    if(harm)for(unsigned i=0;i<t->n_high;i++)put(b,1u,(rnd()%5u)==0u);
    put(b,1u,0u);                                    /* no extended data */
    while(b->bit&7u)put(b,1u,0u);
}

int main(int argc,char **argv){
    if(argc<5)return 2;
    FILE *f=fopen(argv[1],"rb");
    if(!f){perror(argv[1]);return 2;}
    fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
    uint8_t *buf=malloc((size_t)n);
    if(!buf || fread(buf,1,(size_t)n,f)!=(size_t)n){fclose(f);return 2;}
    fclose(f);
    rnd_state=(unsigned)atoi(argv[3]);
    const unsigned smoothing=(unsigned)atoi(argv[4]);
    const long max_frames=argc>5?atol(argv[5]):1000000;
    const unsigned envs_opt=argc>6?(unsigned)atoi(argv[6]):0u;
    const unsigned all_classes=argc>7?(unsigned)atoi(argv[7]):0u;
    SautiAacSbrTables tab;unsigned start,stop;
    if(!pick_header(&start,&stop,&tab,getenv("SAUTI_REMIX_HEADER")?(unsigned)atoi(getenv("SAUTI_REMIX_HEADER")):0u)){fprintf(stderr,"no header\n");return 2;}
    FILE *out=fopen(argv[2],"wb");
    if(!out)return 2;
    size_t pos=0;long frames=0;
    while(pos+7<=(size_t)n && frames<max_frames){
        const unsigned len=((unsigned)(buf[pos+3]&3u)<<11)|((unsigned)buf[pos+4]<<3)|((unsigned)buf[pos+5]>>5);
        if(len<7 || pos+len>(size_t)n || (buf[pos+1]&1u)==0u)break;       /* CRC-less frames only */
        const uint8_t *raw=buf+pos+7;const size_t raw_len=len-7u;
        size_t bit=0;
        static SautiAacLcMonoData data;
        if(!sauti_aac_lc_mono_parse(raw,raw_len,&bit,6u,&data)){fprintf(stderr,"frame %ld: SCE parse failed\n",frames);break;}
        Bits nb={{0},0};
        for(size_t i=0;i<bit;i++)put(&nb,1u,(raw[i/8u]>>(7u-(unsigned)(i&7u)))&1u);
        Bits pl;sbr_payload(&pl,&tab,start,stop,smoothing,frames==0,envs_opt,all_classes);
        const unsigned cnt=(unsigned)(pl.bit/8u);
        put(&nb,3u,6u);                                                     /* ID_FIL */
        if(cnt<15u)put(&nb,4u,cnt);else{put(&nb,4u,15u);put(&nb,8u,cnt-14u);}
        for(size_t i=0;i<pl.bit;i++)put(&nb,1u,(pl.bytes[i/8u]>>(7u-(unsigned)(i&7u)))&1u);
        put(&nb,3u,7u);                                                     /* ID_END */
        while(nb.bit&7u)put(&nb,1u,0u);
        const unsigned nlen=7u+(unsigned)(nb.bit/8u);
        uint8_t hdr[7];memcpy(hdr,buf+pos,7);
        hdr[3]=(uint8_t)((hdr[3]&0xfcu)|((nlen>>11u)&3u));hdr[4]=(uint8_t)(nlen>>3u);hdr[5]=(uint8_t)(((nlen&7u)<<5u)|(hdr[5]&0x1fu));
        fwrite(hdr,1,7,out);fwrite(nb.bytes,1,nb.bit/8u,out);
        pos+=len;frames++;
    }
    fclose(out);
    printf("frames=%ld start=%u stop=%u kx=%u m=%u smoothing=%u\n",frames,start,stop,tab.kx,tab.m,smoothing);
    return frames>0?0:1;
}
