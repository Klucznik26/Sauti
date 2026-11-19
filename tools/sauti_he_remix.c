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
#include "sauti_aac_syntax.h"
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
static void ps_extension(Bits *b,unsigned frame_no);
static void sbr_payload(Bits *b,const SautiAacSbrTables *t,unsigned start,unsigned stop,unsigned smoothing,unsigned header,unsigned envs_opt,unsigned all_classes,unsigned frame_no){
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
    if(getenv("SAUTI_REMIX_PS"))ps_extension(b,frame_no);
    else put(b,1u,0u);                               /* no extended data */
    while(b->bit&7u)put(b,1u,0u);
}

/* SBR-89: generated parametric-stereo data (ps_data() of the SBR extension, id 2).  Written from the syntax of the standard; only the VLC codewords come
 * from the decoder's tables (sauti_aac_ps_huffman_codeword), so a wrong table would still show up against the other decoder.
 * Environment: SAUTI_REMIX_PS=1 switches it on; _IID_MODE/_ICC_MODE 0..5 (10/20/34 bands, coarse/fine IID); _IPD=1 sends IPD/OPD (extension id 0);
 * _CHANGE=n re-sends the header with random modes every n frames (the first frame after a change uses frequency coding only);
 * _NODATA=p percent of the frames carry no PS data at all; _CLASS=0|1 forces fixed/variable frame class (default random);
 * _NOENV=p percent of the frames signal zero envelopes (frame class 0, num_env_idx 0).  Deterministic for the seed. */
typedef struct {
    int iid[5][34],icc[5][34],ipd[5][17],opd[5][17];                       /* values of the frame being written */
    int last_iid[34],last_icc[34],last_ipd[17],last_opd[17];              /* last envelope of the previous frame */
    unsigned iid_mode,icc_mode,iid_on,icc_on,ext_on;
    unsigned started,need_df;                                           /* need_df: a topology change was not followed by coded values yet */
} PsGen;
static PsGen psg;
static unsigned ps_env_int(const char *name,unsigned def){const char *v=getenv(name);return v?(unsigned)atoi(v):def;}
static void ps_put_vlc(Bits *b,SautiAacPsHuffmanTable t,unsigned symbol){
    uint32_t code;uint8_t width;
    if(!sauti_aac_ps_huffman_codeword(t,symbol,&code,&width)){fprintf(stderr,"ps vlc table %d symbol %u missing\n",(int)t,symbol);exit(3);}
    put(b,width,code);
}
/* one parameter set group (IID or ICC) for `env` envelopes, `count` bands */
static void ps_put_levels(Bits *b,unsigned env,unsigned count,int lo,int hi,int (*vals)[34],const int *last,
                          unsigned force_df,SautiAacPsHuffmanTable df,SautiAacPsHuffmanTable dt,int offset,unsigned iid_step){
    for(unsigned e=0;e<env;e++){
        const unsigned use_dt=!force_df && (rnd()%3u)!=0u;
        const int *base=e?vals[e-1]:last;
        for(unsigned i=0;i<count;i++){
            int target;
            const int prev=use_dt?base[i]:(i?vals[e][i-1]:0);
            if(rnd()%4u==0u)target=lo+(int)(rnd()%(unsigned)(hi-lo+1));
            else{target=(use_dt?base[i]:(i?vals[e][i-1]:0))+(int)(rnd()%5u)-2;if(target<lo)target=lo;if(target>hi)target=hi;}
            (void)prev;(void)iid_step;
            vals[e][i]=target;
        }
        put(b,1u,use_dt);
        for(unsigned i=0;i<count;i++){
            const int ref=use_dt?base[i]:(i?vals[e][i-1]:0);
            ps_put_vlc(b,use_dt?dt:df,(unsigned)(vals[e][i]-ref+offset));
        }
    }
}
/* IPD and OPD of one envelope: the standard interleaves them per envelope (ipd e, opd e, ipd e+1, ...) */
static void ps_put_phase_env(Bits *b,unsigned count,int *vals,const int *base_dt,unsigned force_df,SautiAacPsHuffmanTable df,SautiAacPsHuffmanTable dt){
    const unsigned use_dt=!force_df && (rnd()%3u)!=0u;
    for(unsigned i=0;i<count;i++)vals[i]=(int)(rnd()&7u);
    put(b,1u,use_dt);
    for(unsigned i=0;i<count;i++){
        const int ref=use_dt?base_dt[i]:(i?vals[i-1]:0);
        ps_put_vlc(b,use_dt?dt:df,(unsigned)((vals[i]-ref)&7));
    }
}
/* Appends the SBR extension (extended_data flag + byte count + id 2 + ps_data) to b. */
static void ps_extension(Bits *b,unsigned frame_no){
    static const unsigned par[6]={10,20,34,10,20,34},phase[6]={5,11,17,5,11,17};
    if(ps_env_int("SAUTI_REMIX_PS_NODATA",0)>0 && frame_no>0 && rnd()%100u<ps_env_int("SAUTI_REMIX_PS_NODATA",0)){
        if(!ps_env_int("SAUTI_REMIX_PS_NODATA_EMPTY",0)){put(b,1u,0u);return;}     /* no PS extension at all */
        /* the same frame sent explicitly as ps_data() without header and without envelopes (and, if the header enables it, an empty extension) */
        Bits ps={{0},0};put(&ps,1u,0u);put(&ps,1u,0u);put(&ps,2u,0u);
        if(psg.ext_on)put(&ps,4u,0u);
        const unsigned cnt=(unsigned)((2u+ps.bit+7u)/8u);
        put(b,1u,1u);if(cnt<15u)put(b,4u,cnt);else{put(b,4u,15u);put(b,8u,cnt-15u);}
        put(b,2u,2u);for(size_t i=0;i<ps.bit;i++)put(b,1u,(ps.bytes[i/8u]>>(7u-(unsigned)(i&7u)))&1u);
        for(size_t i=2u+ps.bit;i<(size_t)cnt*8u;i++)put(b,1u,0u);
        return;
    }
    const unsigned change=ps_env_int("SAUTI_REMIX_PS_CHANGE",0);
    unsigned send_header=!psg.started,topology_changed=0;
    if(!psg.started){
        psg.iid_on=1;psg.icc_on=1;psg.iid_mode=ps_env_int("SAUTI_REMIX_PS_IID_MODE",1);psg.icc_mode=ps_env_int("SAUTI_REMIX_PS_ICC_MODE",1);
        psg.ext_on=ps_env_int("SAUTI_REMIX_PS_IPD",0)?1u:0u;
    }else if(change && frame_no%change==0){
        send_header=1;
        const unsigned im=rnd()%6u,cm=rnd()%6u;
        topology_changed=par[im]!=par[psg.iid_mode]||par[cm]!=par[psg.icc_mode]||(im>2u)!=(psg.iid_mode>2u);
        psg.iid_mode=im;psg.icc_mode=cm;
    }
    Bits ps={{0},0};
    put(&ps,1u,send_header);
    if(send_header){
        put(&ps,1u,psg.iid_on);if(psg.iid_on)put(&ps,3u,psg.iid_mode);
        put(&ps,1u,psg.icc_on);if(psg.icc_on)put(&ps,3u,psg.icc_mode);
        put(&ps,1u,psg.ext_on);
    }
    unsigned cls=ps_env_int("SAUTI_REMIX_PS_CLASS",2u);if(cls>1u)cls=rnd()&1u;
    unsigned idx=rnd()&3u;
    if(rnd()%100u<ps_env_int("SAUTI_REMIX_PS_NOENV",0)){cls=0;idx=0;}
    if(topology_changed && cls==0u && idx==0u)idx=1u;   /* a header that changes the band layout must come with values: with none, the new layout has no previous
                                                         * parameters and the standard does not say what a decoder uses (the reference decoder reads stale memory) */
    const unsigned num_env=cls?idx+1u:(const unsigned[4]){0,1,2,4}[idx];
    put(&ps,1u,cls);put(&ps,2u,idx);
    if(cls){
        /* num_env strictly ascending borders in 0..31, the last one at 31 a quarter of the time */
        unsigned chosen[4],n=0;
        while(n<num_env){
            const unsigned v=rnd()%32u;unsigned dup=0;
            for(unsigned i=0;i<n;i++)if(chosen[i]==v)dup=1;
            if(!dup)chosen[n++]=v;
        }
        for(unsigned i=0;i<num_env;i++)for(unsigned j=i+1;j<num_env;j++)if(chosen[j]<chosen[i]){const unsigned t=chosen[i];chosen[i]=chosen[j];chosen[j]=t;}
        if(rnd()%4u==0u && chosen[num_env-1u]!=31u && (num_env<2u || chosen[num_env-2u]<31u))chosen[num_env-1u]=31u;
        for(unsigned i=0;i<num_env;i++)put(&ps,5u,chosen[i]);
    }
    const unsigned force_df=topology_changed||!psg.started||psg.need_df;
    if(force_df)psg.need_df=num_env==0u;      /* a frame without envelopes leaves the new topology without a reference */
    if(getenv("SAUTI_REMIX_PS_TRACE"))fprintf(stderr,"PS frame %u header %u iid_mode %u icc_mode %u changed %u class %u env %u\n",frame_no,send_header,psg.iid_mode,psg.icc_mode,topology_changed,cls,num_env);
    if(psg.iid_on){
        const unsigned fine=psg.iid_mode>2u,range=fine?15:7;
        ps_put_levels(&ps,num_env,par[psg.iid_mode],-range,range,psg.iid,psg.last_iid,force_df,
                      fine?SAUTI_AAC_PS_IID_DF1:SAUTI_AAC_PS_IID_DF0,fine?SAUTI_AAC_PS_IID_DT1:SAUTI_AAC_PS_IID_DT0,fine?30:14,0);
        if(num_env)memcpy(psg.last_iid,psg.iid[num_env-1u],sizeof(psg.last_iid));
    }else memset(psg.last_iid,0,sizeof(psg.last_iid));
    if(psg.icc_on){
        ps_put_levels(&ps,num_env,par[psg.icc_mode],0,7,psg.icc,psg.last_icc,force_df,SAUTI_AAC_PS_ICC_DF,SAUTI_AAC_PS_ICC_DT,7,0);
        if(num_env)memcpy(psg.last_icc,psg.icc[num_env-1u],sizeof(psg.last_icc));
    }else memset(psg.last_icc,0,sizeof(psg.last_icc));
    if(psg.ext_on){
        Bits ex={{0},0};
        put(&ex,2u,0u);                                              /* ps_extension_id 0: IPD/OPD */
        const unsigned ipd=psg.iid_on && ps_env_int("SAUTI_REMIX_PS_IPD",0);
        put(&ex,1u,ipd);
        if(ipd){
            const unsigned n=phase[psg.iid_mode];
            for(unsigned e=0;e<num_env;e++){
                ps_put_phase_env(&ex,n,psg.ipd[e],e?psg.ipd[e-1u]:psg.last_ipd,force_df,SAUTI_AAC_PS_IPD_DF,SAUTI_AAC_PS_IPD_DT);
                ps_put_phase_env(&ex,n,psg.opd[e],e?psg.opd[e-1u]:psg.last_opd,force_df,SAUTI_AAC_PS_OPD_DF,SAUTI_AAC_PS_OPD_DT);
            }
            if(num_env){memcpy(psg.last_ipd,psg.ipd[num_env-1u],sizeof(psg.last_ipd));memcpy(psg.last_opd,psg.opd[num_env-1u],sizeof(psg.last_opd));}
        }else{memset(psg.last_ipd,0,sizeof(psg.last_ipd));memset(psg.last_opd,0,sizeof(psg.last_opd));}
        put(&ex,1u,0u);                                              /* reserved */
        const unsigned cnt=(unsigned)((ex.bit+7u)/8u);
        if(cnt<15u)put(&ps,4u,cnt);else{put(&ps,4u,15u);put(&ps,8u,cnt-15u);}
        for(size_t i=0;i<ex.bit;i++)put(&ps,1u,(ex.bytes[i/8u]>>(7u-(unsigned)(i&7u)))&1u);
        for(size_t i=ex.bit;i<(size_t)cnt*8u;i++)put(&ps,1u,0u);
    }
    psg.started=1;
    /* the SBR extension: byte count of [extension id (2) + ps_data], padded with zero bits */
    const unsigned cnt=(unsigned)((2u+ps.bit+7u)/8u);
    put(b,1u,1u);
    if(cnt<15u)put(b,4u,cnt);else{put(b,4u,15u);put(b,8u,cnt-15u);}
    put(b,2u,2u);
    for(size_t i=0;i<ps.bit;i++)put(b,1u,(ps.bytes[i/8u]>>(7u-(unsigned)(i&7u)))&1u);
    for(size_t i=2u+ps.bit;i<(size_t)cnt*8u;i++)put(b,1u,0u);
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
        Bits pl;sbr_payload(&pl,&tab,start,stop,smoothing,frames==0,envs_opt,all_classes,(unsigned)frames);
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
