/* TwinVQ (MPEG-4 Audio object type 7) base layer, see sauti_aac_twinvq.h.  Written from the algorithm description of ISO/IEC 14496-3 subpart 4 and checked against the reference
 * decoder of ISO/IEC 14496-5 (spectrum and PCM of the conformance streams ac08 .. ac20 and of random frames); the data tables are in sauti_aac_twinvq_tables.c (provenance there). */
#include "sauti_aac_twinvq.h"
#include "sauti_aac_twinvq_tables.h"
#include "sauti_aac_bands.h"
#include "sauti_aac_layout.h"
#include "sauti_aac_lc_synth.h"
#include "sauti_aac_window.h"
#include "sauti_aac_imdct.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.141592653589793238462643383279
#define N_PR 20u                    /* LPC order */
#define LSP_SPLIT 3u
#define GAIN_BITS 9u
#define SUB_GAIN_BITS 4u
#define N_SHORT 8u
#define N_CRB 42u                   /* Bark bands */
#define FW_N_DIV 7u
#define FW_N_BIT 6u
#define FW_CB_LEN 6u
#define MAXBIT_SHAPE 5u
#define MAXBIT 6u
#define PIT_N_BIT 28u
#define BASF_BIT 8u
#define PGAIN_BIT 7u
#define N_FR_P 20u
#define N_DIV_P_PER_CH 2u
#define MAXBIT_SHAPE_P 6u
#define PIT_CB_SIZE 64u
#define CB_LEN_LONG 20u
#define CB_LEN_SHORT 17u
#define CB_MARGIN 40u
#define BLIM_BITS_H 2u
#define BLIM_BITS_L 1u
#define BWID_BITS (BLIM_BITS_H+BLIM_BITS_L)
#define BLIM_STEP_H 3
#define CUT_M_H 0.7
#define CUT_M_L 0.0025
#define AMP_NM 1024.0
#define AMP_MAX 16000.0
#define MU 100.0
#define SUB_AMP_MAX 4700.0
#define PGAIN_MAX 20000.0
#define PGAIN_MU 200.0
#define FW_ALF_STEP 0.5
#define FW_LLIM 0.1
#define MIN_GAP 0.0001
#define L_LIMIT (-10.0)
#define M_LIMIT 10.0
#define NIDX 5u                     /* LSP indices per channel: mode, first stage, 3 split vectors */
#define MAXBLOCK 1024u
#define TWINVQ_PCM_SCALE (1.0f/65536.0f)   /* the decoder units of the filter bank (as the other AAC decoders: ldexpf(x, -16)) */

static const unsigned bark16[N_CRB]={8,16,24,32,40,48,56,64,72,80,88,100,112,124,136,148,160,172,184,196,212,228,244,260,280,300,320,344,368,396,424,456,492,532,572,616,664,716,772,832,896,1024};
static const unsigned bark24[N_CRB]={4,8,12,16,20,24,28,32,36,40,44,52,60,68,76,84,92,100,108,116,124,136,148,160,172,188,204,220,240,260,284,308,336,364,396,432,468,508,552,600,652,1024};
static const unsigned rates[13]={96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,7350};

struct SautiAacTwinVq {
    unsigned rate_index,rate,channels,block,isampf;
    double band_upper;                                  /* upper edge of the coded band as a fraction of the block */
    unsigned bark[N_CRB];
    double *cos_tt;                                     /* [2 * block + 1] */
    double cb_long0[32*(CB_LEN_LONG+CB_MARGIN)],cb_long1[32*(CB_LEN_LONG+CB_MARGIN)];
    double cb_short0[32*(CB_LEN_SHORT+CB_MARGIN)],cb_short1[32*(CB_LEN_SHORT+CB_MARGIN)];
    short pleave[2048];                                 /* pitch vector samples in transmission order */
    /* state that lives from frame to frame */
    int ma_np;
    int prev_lsp[2][NIDX];
    int prev_fw[2*FW_N_DIV];
    SautiAacLcSynthesis syn[2];                         /* filter bank */
    double ltp[2][3*MAXBLOCK];                          /* history of the long term predictor: output two frames back, last output, last overlap */
    unsigned prev_shape[2];
    /* last frame */
    unsigned last_ws,last_shape;
    double spec[2][MAXBLOCK];
};

static unsigned vec_len_p(unsigned channels,unsigned *bits,unsigned *length){
    const unsigned nd=N_DIV_P_PER_CH*channels;
    for(unsigned i=0;i<nd;i++){
        bits[i]=(PIT_N_BIT*channels+nd-1u-i)/nd;
        length[i]=(N_FR_P*channels+nd-1u-i)/nd;
    }
    return nd;
}
static void fill_cb(double *dst,const double *src,unsigned rows,unsigned len,unsigned stride){
    memset(dst,0,(size_t)rows*stride*sizeof(double));
    for(unsigned r=0;r<rows;r++)for(unsigned c=0;c<len;c++)dst[(size_t)r*stride+c]=src[(size_t)r*len+c];
}

SautiAacTwinVq *sauti_aac_twinvq_new(unsigned rate_index,unsigned channels,unsigned block,size_t first_au_bytes){
    if(rate_index>12u||channels<1u||channels>2u||(block!=1024u&&block!=960u)||first_au_bytes<4u||first_au_bytes>8192u)return NULL;
    SautiAacTwinVq *s=(SautiAacTwinVq *)calloc(1,sizeof(*s));
    if(!s)return NULL;
    s->rate_index=rate_index;s->rate=rates[rate_index];s->channels=channels;s->block=block;
    s->isampf=(unsigned)((int)((float)s->rate+0.5f)/1000);
    if(s->isampf<4u){free(s);return NULL;}
    memcpy(s->bark,s->rate<=16000u?bark16:bark24,sizeof(s->bark));
    if(block==960u)s->bark[N_CRB-1u]=960u;
    {   /* bits per frame -> bandwidth, with the float arithmetic of the reference decoder */
        const float spf=(float)block;
        float brate=(float)(first_au_bytes*8u);
        brate=brate*(float)s->rate/spf;
        const int ibps=(int)brate;
        const float bps=((float)ibps-0.0f)/(float)channels;
        float ftmp=(float)(block*channels)*bps;
        ftmp/=(float)s->rate;
        ftmp+=0.5f;
        int nbits_fr=(int)ftmp;
        nbits_fr=((nbits_fr+7)/8)*8;
        int bps_i=nbits_fr*(int)s->rate/(int)block/(int)channels;
        int upper=(95*bps_i)/(int)s->isampf;
        if(upper>100000)upper=100000;
        upper*=16384;upper+=1562;upper/=3125;
        s->band_upper=(double)upper/16384.0/32.0;
        if(!(s->band_upper>0.0)||s->band_upper>1.0){free(s);return NULL;}
    }
    for(unsigned c=0;c<channels;c++)(void)sauti_aac_lc_synthesis_init(&s->syn[c],0u);
    s->cos_tt=(double *)calloc((size_t)block*2u+1u,sizeof(double));
    if(!s->cos_tt){free(s);return NULL;}
    {const double dt=PI/(double)(4u*block);for(unsigned i=0;i<block*2u;i++)s->cos_tt[i]=cos(dt*(double)i);s->cos_tt[block*2u]=0.0;}
    fill_cb(s->cb_long0,sauti_twinvq_cb_long0,32u,CB_LEN_LONG,CB_LEN_LONG+CB_MARGIN);
    fill_cb(s->cb_long1,sauti_twinvq_cb_long1,32u,CB_LEN_LONG,CB_LEN_LONG+CB_MARGIN);
    fill_cb(s->cb_short0,sauti_twinvq_cb_short0,32u,CB_LEN_SHORT,CB_LEN_SHORT+CB_MARGIN);
    fill_cb(s->cb_short1,sauti_twinvq_cb_short1,32u,CB_LEN_SHORT,CB_LEN_SHORT+CB_MARGIN);
    {   /* the order of the pitch vector samples on the line */
        unsigned bits[8],length[8];
        const unsigned nd=vec_len_p(channels,bits,length);
        unsigned serial=0;
        for(unsigned i=0;i<nd;i++){
            for(unsigned j=0;j+1u<length[0];j++){
                const unsigned acci=(channels==1u||(nd%channels)!=0u)?i+nd*j:((i+j)%nd)+nd*j;
                s->pleave[serial++]=(short)(acci/channels+(acci%channels)*N_FR_P);
            }
            for(unsigned j=length[0]-1u;j<length[i];j++){
                const unsigned acci=i+nd*j;
                s->pleave[serial++]=(short)(acci/channels+(acci%channels)*N_FR_P);
            }
        }
    }
    return s;
}
SautiAacTwinVq *sauti_aac_twinvq_dup(const SautiAacTwinVq *o){
    if(!o)return NULL;
    SautiAacTwinVq *s=(SautiAacTwinVq *)malloc(sizeof(*s));
    if(!s)return NULL;
    *s=*o;
    s->cos_tt=(double *)malloc(((size_t)o->block*2u+1u)*sizeof(double));
    if(!s->cos_tt){free(s);return NULL;}
    memcpy(s->cos_tt,o->cos_tt,((size_t)o->block*2u+1u)*sizeof(double));
    return s;
}
void sauti_aac_twinvq_free(SautiAacTwinVq *s){if(s){free(s->cos_tt);free(s);}}
unsigned sauti_aac_twinvq_block(const SautiAacTwinVq *s){return s?s->block:0u;}
unsigned sauti_aac_twinvq_channels(const SautiAacTwinVq *s){return s?s->channels:0u;}
unsigned sauti_aac_twinvq_max_sfb(const SautiAacTwinVq *s,unsigned window_sequence){
    if(!s)return 0;
    const unsigned idx=SAUTI_AAC_IDX(s->rate_index,s->block==960u?SAUTI_AAC_LAYOUT_960:SAUTI_AAC_LAYOUT_1024),longf=window_sequence!=2u;
    const uint16_t *off=NULL;unsigned nb=0;
    if(!sauti_aac_band_offsets(idx,longf?0u:1u,&off,&nb))return 0;
    float ft=(float)s->band_upper;ft*=(float)(longf?s->block:s->block/N_SHORT);
    const unsigned max_line=(unsigned)(int)ft;
    unsigned sb_ind=1;
    for(unsigned sb=1;sb<=nb&&off[sb]<max_line;sb++)sb_ind=sb+1u;
    return sb_ind>nb?nb:sb_ind;
}
const double *sauti_aac_twinvq_last_spectrum(const SautiAacTwinVq *s,unsigned ch,unsigned *ws,unsigned *shape){
    if(!s||ch>=s->channels)return NULL;
    if(ws)*ws=s->last_ws;
    if(shape)*shape=s->last_shape;
    return s->spec[ch];
}

/* ---- bits ---------------------------------------------------------------------------------------------------------------------------------------------------------------- */
typedef struct { const uint8_t *p; size_t nbits,pos; int bad; } Br;
static unsigned get(Br *b,unsigned n){
    unsigned v=0;
    if(b->pos+n>b->nbits){b->bad=1;b->pos=b->nbits;return 0;}
    while(n--){v=(v<<1)|((b->p[b->pos>>3]>>(7u-(b->pos&7u)))&1u);b->pos++;}
    return v;
}

/* ---- the layers of the spectrum --------------------------------------------------------------------------------------------------------------------------------------- */
static double mulaw_inv(double y,double xmax,double mu){
    if(y>xmax)y=xmax;else if(y< -xmax)y=-xmax;
    const double sgn=y<0.0?-1.0:1.0;
    return sgn*xmax*(pow(10.0,fabs(y)*log10(1.0+mu)/xmax)-1.0)/mu;
}
static void check_lsp(unsigned n,double *buf,double gap){      /* buf[-1] is part of the vector */
    for(unsigned j=0;j<n;j++)
        if(buf[(int)j-1]>buf[j]-gap){const double d=buf[(int)j-1]-buf[j];buf[(int)j-1]-=(d+gap)/2.0;buf[j]+=(d+gap)/2.0;}
}
static void check_lsp_sort(unsigned n,double *buf){
    for(unsigned ite=0;ite<n;ite++){
        int flag=0;
        for(unsigned j=0;j<n;j++)if(buf[(int)j-1]>buf[j]){const double t=buf[(int)j-1];buf[(int)j-1]=buf[j];buf[j]=t;flag=1;}
        if(!flag)return;
    }
}
static void set_isp(unsigned *isp){isp[0]=0;isp[1]=N_PR/3u-1u;isp[2]=N_PR-N_PR/3u;isp[3]=N_PR;}
/* one decoding of an LSP vector from its indices (first stage + split second stage), before the moving-average prediction */
static void redec(const int *index,const double *fg_sum,const double *pred_vec,double *lspq,double *out_vec){
    unsigned isp[4];
    set_isp(isp);
    const double *code=sauti_twinvq_lsp;
    for(unsigned d=0;d<LSP_SPLIT;d++)
        for(unsigned j=isp[d];j<isp[d+1];j++)
            lspq[j+1]=code[(size_t)index[1]*N_PR+j]+code[((size_t)64u+(unsigned)index[d+2])*N_PR+j];
    if(lspq[1]<L_LIMIT){const double diff=L_LIMIT-lspq[1];lspq[1]+=diff*1.2;}
    if(lspq[N_PR]>M_LIMIT){const double diff=lspq[N_PR]-M_LIMIT;lspq[N_PR]-=diff*1.2;}
    check_lsp(N_PR-1u,lspq+2,MIN_GAP);
    for(unsigned j=0;j<N_PR;j++)out_vec[j]=lspq[j+1u];
    for(unsigned j=0;j<N_PR;j++)lspq[j+1u]=lspq[j+1u]*fg_sum[j]+pred_vec[j];
    for(unsigned i=1;i<N_PR;i++)
        if(lspq[i]>lspq[i+1]-MIN_GAP){const double diff=lspq[i]-lspq[i+1];lspq[i]-=(diff+MIN_GAP)/2.0;lspq[i+1]+=(diff+MIN_GAP)/2.0;}
    for(unsigned i=1;i<N_PR;i++)
        if(lspq[i]>lspq[i+1]-MIN_GAP*0.95){const double diff=lspq[i]-lspq[i+1];lspq[i]-=(diff+MIN_GAP*0.95)/2.0;lspq[i+1]+=(diff+MIN_GAP*0.95)/2.0;}
    check_lsp_sort(N_PR-1u,lspq+2);
}
static void lsp_decode(int ma_np,const int *prev,const int *index,double *freq){
    const double *fg=sauti_twinvq_lsp+(size_t)80u*N_PR;          /* [mode][20] */
    const unsigned mode=(unsigned)index[0];
    double lspq[N_PR+1u],out_vec[N_PR],fg_sum[N_PR],pred_vec[N_PR],buf_prev[N_PR];
    for(unsigned j=0;j<N_PR;j++){fg_sum[j]=1.0;if(ma_np>=1)fg_sum[j]-=fg[(size_t)mode*N_PR+j];pred_vec[j]=0.0;}
    lspq[0]=0.0;
    redec(prev,fg_sum,pred_vec,lspq,buf_prev);
    if(ma_np==1)for(unsigned j=0;j<N_PR;j++)pred_vec[j]=fg[(size_t)mode*N_PR+j]*buf_prev[j];
    redec(index,fg_sum,pred_vec,lspq,out_vec);
    for(unsigned j=1;j<=N_PR;j++)freq[j]=lspq[j];
}
static void lsp_weight(const double *coslsp,double costblv,double *wtv){
    const double cosz2=2.0*costblv;
    unsigned jj=0;
    double a=cosz2-coslsp[jj++],b=cosz2-coslsp[jj++];
    for(;jj<N_PR;){a*=cosz2-coslsp[jj++];b*=cosz2-coslsp[jj++];}
    *wtv=1.0/(a*a*(1.0+costblv)+b*b*(1.0-costblv));
}
/* the spectral envelope of the LSP vector, interpolated between sparse points */
static void lsp_to_wt(unsigned nfr,unsigned block,const double *lsp,double *wt,const double *cos_tt){
    double coslsp[N_PR];
    const unsigned mag=block/nfr;
    unsigned istepl,isteph;
    if(mag==1u){istepl=4u;isteph=8u;}else if(mag>1u&&mag<=4u){istepl=2u;isteph=4u;}else{istepl=1u;isteph=1u;}
    for(unsigned i=0;i<N_PR;i++)coslsp[i]=2.0*cos(lsp[i+1u]);
    unsigned i;
    for(i=0;i<nfr/2u;i+=istepl)lsp_weight(coslsp,cos_tt[(i*4u+2u)*mag],wt+i);
    for(i=nfr/2u;i<nfr;i+=isteph)lsp_weight(coslsp,-cos_tt[block*4u-(i*4u+2u)*mag],wt+i);
    if(isteph==1u)return;
    const unsigned endpoint=i-isteph;
    for(i=istepl;i<nfr/2u;i+=istepl){
        if((wt[i]*1.95>wt[istepl+i]+wt[i-istepl])&&(wt[i-istepl]>wt[i+istepl])){
            lsp_weight(coslsp,cos_tt[((i-istepl/2u)*4u+2u)*mag],wt+i-istepl/2u);
            unsigned jjj=1;
            for(unsigned jj=i-istepl+1u;jj<i-istepl/2u;jj++){
                const double delta=(double)(jjj+1u)/(double)istepl;
                wt[jj]=wt[i-istepl]*(1.0-delta)+wt[i-istepl/2u]*delta;
                wt[jj+istepl/2u]=wt[i-istepl/2u]*(1.0-delta)+wt[i]*delta;
                jjj+=2u;
            }
        }else{
            unsigned jjj=0;
            for(unsigned jj=i-istepl+1u;jj<i;jj++){
                const double delta=(double)(jjj+1u)/(double)istepl;
                wt[jj]=wt[i-istepl]*(1.0-delta)+wt[i]*delta;
                jjj++;
            }
        }
    }
    {unsigned jjj=0;for(unsigned jj=nfr/2u-istepl+1u;jj<nfr/2u;jj++){const double delta=(double)(jjj+1u)/(double)istepl;wt[jj]=wt[nfr/2u-istepl]*(1.0-delta)+wt[nfr/2u]*delta;jjj++;}}
    for(i=nfr/2u+isteph;i<nfr-isteph;i+=isteph){
        if((wt[i]*1.95>wt[isteph+i]+wt[i-isteph])&&(wt[i-isteph]>wt[i+isteph])){
            lsp_weight(coslsp,-cos_tt[block*4u-((i-istepl)*4u+2u)*mag],wt+i-istepl);
            unsigned jjj=1;
            for(unsigned jj=i-isteph+1u;jj<i-istepl;jj++){
                const double delta=(double)(jjj+1u)/(double)isteph;
                wt[jj]=wt[i-isteph]*(1.0-delta)+wt[i-istepl]*delta;
                wt[jj+istepl]=wt[i-istepl]*(1.0-delta)+wt[i]*delta;
                jjj+=2u;
            }
        }else{
            unsigned jjj=0;
            for(unsigned jj=i-isteph+1u;jj<i;jj++){
                const double delta=(double)(jjj+1u)/(double)isteph;
                wt[jj]=wt[i-isteph]*(1.0-delta)+wt[i]*delta;
                jjj++;
            }
        }
    }
    {unsigned jjj=0;for(unsigned jj=endpoint-isteph+1u;jj<endpoint;jj++){const double delta=(double)(jjj+1u)/(double)isteph;wt[jj]=wt[i-isteph]*(1.0-delta)+wt[i]*delta;jjj++;}}
    for(i=endpoint+1u;i<nfr;i++)wt[i]=wt[endpoint];
}
static void bark_codes(const int *index,double *env){            /* 7 codevectors of 6, interleaved: env[k * 7 + d] */
    for(unsigned d=0;d<FW_N_DIV;d++)for(unsigned k=0;k<FW_CB_LEN;k++)env[k*FW_N_DIV+d]=sauti_twinvq_cb_env[(size_t)index[d]*FW_CB_LEN+k];
}
/* the harmonic (pitch) component: a sparse sequence placed at multiples of the fundamental */
static void extend_pitch(const SautiAacTwinVq *s,const int *index_pit,const double *reconst,double *pit_seq){
    const int bandUpper=(int)(s->band_upper*16384.0+0.1);
    const int fcmin=(int)((float)s->block/(float)s->isampf*0.2*1024.0+0.5);
    int bandwidth=8;
    if(s->isampf==8u)bandwidth=3;
    if(s->isampf>=11u)bandwidth=4;
    if(s->isampf>=22u)bandwidth=8;
    for(unsigned c=0;c<s->channels;c++){
        const unsigned top=c*s->block,ptop=c*N_FR_P;
        int pitch=(int)(pow(1.009792,(double)index_pit[c])*4096.0+0.5);
        pitch*=fcmin;pitch/=256;
        /* 32 bit arithmetic with wrap-around, as the reference decoder (which, for the narrow-band branch, always overflows: the products are part of the definition of the stream) */
        int npcount=0;
        if(bandUpper>0){
            if(bandwidth*bandUpper<32768){int t0=(int)((uint32_t)pitch*16384u)/bandUpper;int t1=(int)((uint32_t)N_FR_P*(uint32_t)t0);t0=t1/(int)s->block;npcount=t0/16384;}
            else{int t0=(int)((uint32_t)pitch*(uint32_t)bandwidth);int t1=(int)((uint32_t)N_FR_P*(uint32_t)t0);t0=t1/(int)s->block;npcount=t0/32768;}
        }
        unsigned iscount=0;
        for(unsigned jj=0;jj<s->block;jj++)pit_seq[jj+top]=0.0;
        for(int jj=0;jj<npcount/2&&iscount<N_FR_P;jj++){if(jj<(int)s->block)pit_seq[(unsigned)jj+top]=reconst[iscount+ptop];iscount++;}
        for(unsigned ii=0;ii<N_FR_P&&iscount<N_FR_P;ii++){
            int t0=pitch*(int)(ii+1u);t0+=8192;
            const int i_smp=t0/16384;
            if(i_smp+(npcount-1)/2+1>=(int)s->block)continue;
            for(int jj=-npcount/2;jj<(npcount-1)/2+1;jj++){
                if(i_smp+jj>=0&&i_smp+jj<(int)s->block)pit_seq[(unsigned)(i_smp+jj)+top]=reconst[iscount+ptop];
                iscount++;
                if(iscount>=N_FR_P)break;
            }
        }
    }
}

typedef struct { int wvq[1024],fw[2*FW_N_DIV],fw_alf[2],pow[2*(N_SHORT+1u)],lsp[2][NIDX],pit[2],pls[16],pgain[2],blim_h[2],blim_l[2],pf,pitch_q,bandlimit; unsigned vq_bits; } Idx;

typedef struct { uint8_t present; int lag[N_SHORT]; double weight; uint8_t sbk_used[N_SHORT+1u]; uint8_t sfb_used[64]; } Ltp;
typedef struct { uint8_t n_filt; uint8_t coef_res; struct { uint8_t len,start,stop,order,direction,compress; int8_t coef[32]; } f[3]; } TnsSub;
typedef struct { unsigned n_sub; TnsSub sub[N_SHORT]; uint8_t active; } Tns;
typedef struct { unsigned max_sfb,ms_mask,group_code; uint8_t ms[N_SHORT][64]; Ltp ltp[2]; Tns tns[2]; } Side;
static const double ltp_weight[8]={0.570829,0.696616,0.813004,0.911304,0.984900,1.067894,1.194601,1.369533};

/* indices -> spectrum of the frame (before the stereo matrix) */
static int spectrum(SautiAacTwinVq *s,Idx *x,unsigned ws,double *out){
    const unsigned ch=s->channels,block=s->block,longf=ws!=2u;
    const unsigned nfr=longf?block:block/N_SHORT,nsf=longf?1u:N_SHORT;
    static const unsigned zero=0;(void)zero;
    double *flat=(double *)calloc((size_t)block*ch,sizeof(double));
    double *pit=(double *)calloc((size_t)block*ch,sizeof(double));
    double *bark=(double *)calloc((size_t)block*ch,sizeof(double));
    double *inv=(double *)calloc((size_t)block*ch,sizeof(double));
    double *inv_all=(double *)calloc((size_t)block*ch,sizeof(double));
    if(!flat||!pit||!bark||!inv||!inv_all){free(flat);free(pit);free(bark);free(inv);free(inv_all);return 0;}
    /* --- requantisation of the flattened spectrum --- */
    {
        const unsigned n_sf=longf?ch:ch*N_SHORT,stride=longf?CB_LEN_LONG+CB_MARGIN:CB_LEN_SHORT+CB_MARGIN;
        const double *cv0=longf?s->cb_long0:s->cb_short0,*cv1=longf?s->cb_long1:s->cb_short1;
        unsigned data_len=block*ch/n_sf;
        float ft=(float)s->band_upper;ft*=(float)data_len;data_len=(unsigned)(int)ft;data_len*=n_sf;
        const unsigned n_div=(x->vq_bits+MAXBIT*2u-1u)/(MAXBIT*2u);
        const unsigned length0=(data_len+n_div-1u)/n_div;
        const int mask=(1<<MAXBIT_SHAPE)-1;
        for(unsigned id=0;id<n_div;id++){
            const unsigned i0=(unsigned)x->wvq[id]&(unsigned)mask,i1=(unsigned)x->wvq[id+n_div]&(unsigned)mask;
            const double pol0=1.0-2.0*(double)(((unsigned)x->wvq[id]>>MAXBIT_SHAPE)&1u),pol1=1.0-2.0*(double)(((unsigned)x->wvq[id+n_div]>>MAXBIT_SHAPE)&1u);
            const unsigned length=(data_len+n_div-1u-id)/n_div;
            for(unsigned icv=0;icv<length;icv++){
                unsigned itmp;
                if(icv+1u<length0&&((n_div%n_sf==0u&&n_sf>1u)||((n_div&1u)==0u&&(n_sf&1u)==0u)))itmp=((id+icv)%n_div)+icv*n_div;
                else itmp=id+icv*n_div;
                const unsigned ismp=itmp/n_sf+(itmp%n_sf)*(data_len/n_sf);
                if(ismp>=block*ch||icv>=stride){free(flat);free(pit);free(bark);free(inv);free(inv_all);return 0;}
                flat[ismp]=(pol0*cv0[(size_t)i0*stride+icv]+pol1*cv1[(size_t)i1*stride+icv])*0.5;
            }
        }
        {   /* spread the coded band of every subframe over its full length */
            const unsigned nf=block*ch/n_sf;float f2=(float)s->band_upper;f2*=(float)nf;const unsigned w=(unsigned)(int)f2;
            for(unsigned isf=n_sf-1u;isf>0u;isf--)for(int j=(int)w-1;j>=0;j--)flat[nf*isf+(unsigned)j]=flat[w*isf+(unsigned)j];
            for(unsigned isf=0;isf<n_sf;isf++)for(unsigned j=w;j<nf;j++)flat[nf*isf+j]=0.0;
        }
    }
    /* --- pitch component --- */
    double pgain[N_SHORT*2];
    for(unsigned i=0;i<N_SHORT*2u;i++)pgain[i]=0.0;
    if(longf&&x->pitch_q){
        unsigned bits[8],length[8];
        const unsigned nd=vec_len_p(ch,bits,length);
        double reconst[N_FR_P*2u];
        unsigned serial=0;
        for(unsigned c=0;c<ch;c++){
            const double step=PGAIN_MAX/127.0;
            pgain[c]=mulaw_inv((double)x->pgain[c]*step+step/2.0,PGAIN_MAX,PGAIN_MU);
        }
        for(unsigned id=0;id<nd;id++){
            const unsigned i0=(unsigned)x->pls[id]&(PIT_CB_SIZE-1u),i1=(unsigned)x->pls[id+nd]&(PIT_CB_SIZE-1u);
            const double p0=1.0-2.0*(double)(((unsigned)x->pls[id]>>MAXBIT_SHAPE_P)&1u),p1=1.0-2.0*(double)(((unsigned)x->pls[id+nd]>>MAXBIT_SHAPE_P)&1u);
            for(unsigned k=0;k<length[id];k++)
                reconst[s->pleave[serial++]]=(p0*sauti_twinvq_cb_pitch[(size_t)i0*10u+k]+p1*sauti_twinvq_cb_pitch[((size_t)i1+PIT_CB_SIZE)*10u+k])*0.5;
        }
        extend_pitch(s,x->pit,reconst,pit);
    }
    /* --- Bark envelope --- */
    if(longf){
        for(unsigned c=0;c<ch;c++){
            double env[N_CRB],penv[N_CRB];
            bark_codes(s->prev_fw+c*FW_N_DIV,penv);
            bark_codes(x->fw+c*FW_N_DIV,env);
            for(unsigned ib=FW_N_DIV*FW_CB_LEN;ib<N_CRB;ib++)env[ib]=0.0;
            const double alfq=(double)x->fw_alf[c]*FW_ALF_STEP;
            unsigned ismp=0;
            for(unsigned ib=0;ib<N_CRB;ib++){
                double d=env[ib]+alfq*penv[ib]+1.0;
                if(d<FW_LLIM)d=FW_LLIM;
                if(x->pf==1&&d<1.0)d=d*d;
                for(;ismp<s->bark[ib];ismp++)bark[ismp+c*block]=d;
            }
        }
        for(unsigned i=0;i<FW_N_DIV*ch;i++)s->prev_fw[i]=x->fw[i];
    }else for(unsigned i=0;i<block*ch;i++)bark[i]=1.0;
    /* --- gain --- */
    double gain[N_SHORT*2];
    {
        const double step=AMP_MAX/511.0,sstep=SUB_AMP_MAX/15.0;
        for(unsigned c=0;c<ch;c++){
            if(nsf==1u)gain[c]=mulaw_inv((double)x->pow[c]*step+step/2.0,AMP_MAX,MU)/AMP_NM;
            else{
                const unsigned top=c*(nsf+1u);
                const double g=mulaw_inv((double)x->pow[top]*step+step/2.0,AMP_MAX,MU)/AMP_NM;
                for(unsigned isf=0;isf<nsf;isf++)gain[isf+c*nsf]=g*mulaw_inv((double)x->pow[isf+1u+top]*sstep+sstep/2.0,SUB_AMP_MAX,MU)/1024.0;
            }
        }
    }
    /* --- LPC envelope --- */
    {
        double lspq[2*(N_PR+1u)];
        for(unsigned c=0;c<ch;c++)lsp_decode(s->ma_np,s->prev_lsp[c],x->lsp[c],lspq+c*(N_PR+1u));
        for(unsigned c=0;c<ch;c++)lsp_to_wt(nfr,block,lspq+c*(N_PR+1u),inv+c*nfr,s->cos_tt);
        s->ma_np=1;
        for(unsigned c=0;c<ch;c++)for(unsigned i=0;i<NIDX;i++)s->prev_lsp[c][i]=x->lsp[c][i];
        {   /* the envelope covers the coded band only */
            const unsigned nfr_l=0;
            float f=(float)s->band_upper-0.0f;f*=(float)nfr;const unsigned nfr_lu=(unsigned)(int)f;
            int fi=(int)((s->band_upper-0.0)*16384.0);
            if(fi<=0){free(flat);free(pit);free(bark);free(inv);free(inv_all);return 0;}
            fi=(16384*16384)/fi;
            for(unsigned c=0;c<ch;c++){
                for(unsigned i=0;i<nfr;i++)inv_all[c*nfr+i]=0.0;
                for(unsigned i=0;i<nfr_lu&&i<nfr;i++){long long u=(long long)fi*(long long)i/16384;if(u>=(long long)nfr)u=(long long)nfr-1;inv_all[i+c*nfr+nfr_l]=inv[(unsigned)u+c*nfr];}
            }
            for(unsigned i=0;i<nfr*ch;i++)inv[i]=inv_all[i];
        }
    }
    /* --- denormalisation and band limiting --- */
    for(unsigned c=0;c<ch;c++){
        const unsigned top=c*nfr*nsf;
        for(unsigned isf=0;isf<nsf;isf++){
            const unsigned sub=isf*nfr;
            for(unsigned i=0;i<nfr;i++){
                const double dt=pgain[isf+c*nsf]*pit[i+top+sub];
                out[i+top+sub]=(gain[isf+c*nsf]*flat[i+top+sub]*bark[i+top+sub]+dt)*inv[i+c*nfr];
            }
        }
    }
    for(unsigned c=0;c<ch;c++){
        double lo=0.0,hi=s->band_upper;
        if(x->bandlimit==1){
            float f=(float)x->blim_h[c]/(float)BLIM_STEP_H;f*=(float)(1.0-CUT_M_H);f=1.0f-f;f*=(float)s->band_upper;hi=(double)f;
            float g=(float)x->blim_l[c];g*=(float)CUT_M_L;g+=0.0f;lo=(double)g;
        }
        for(unsigned isf=0;isf<nsf;isf++){
            double *sp=out+c*block+isf*nfr;
            float f=(float)lo;f*=(float)nfr;const unsigned start=(unsigned)(int)f;
            float w=(float)hi-(float)lo;w*=(float)nfr;const unsigned width=(unsigned)(int)w;
            const unsigned stop=start+width;
            for(unsigned i=0;i<start&&i<nfr;i++)sp[i]=0.0;
            for(unsigned i=stop;i<nfr;i++)sp[i]=0.0;
        }
    }
    free(flat);free(pit);free(bark);free(inv);free(inv_all);
    return 1;
}


/* ---- TNS (double precision, as the reference decoder: the filters of long windows of the extreme streams are ill conditioned) ---------------------------------------------------- */
static const uint8_t tns_max_bands_long[13]={31,31,34,40,42,51,46,46,42,42,42,39,39};
static const uint8_t tns_max_bands_short[13]={9,9,10,14,14,14,14,14,14,14,14,14,14};
static void tns_lpc(unsigned order,unsigned res,const int8_t *coef,double *a){
    double tmp[33],b[33];
    const double iqfac=((double)(1u<<(res-1u))-0.5)/(PI/2.0),iqfac_m=((double)(1u<<(res-1u))+0.5)/(PI/2.0);
    for(unsigned i=0;i<order;i++)tmp[i+1u]=sin((double)coef[i]/(coef[i]>=0?iqfac:iqfac_m));
    a[0]=1.0;
    for(unsigned m=1;m<=order;m++){
        b[0]=a[0];
        for(unsigned i=1;i<m;i++)b[i]=a[i]+tmp[m]*a[m-i];
        b[m]=tmp[m];
        for(unsigned i=0;i<=m;i++)a[i]=b[i];
    }
}
/* one window of a TNS frame: `nbands` limits the filter ranges, `off` are the band tops of the window, `analysis` selects the encoder side filter (all-zero) instead of the decoder side (all-pole) */
static void tns_window(double *spec,const TnsSub *ts,unsigned longf,unsigned sfi,unsigned nbands,const uint16_t *off,int analysis){
    const unsigned maxo=longf?(sfi>=5u?12u:20u):7u,maxb=longf?tns_max_bands_long[sfi]:tns_max_bands_short[sfi];
    for(unsigned f=0;f<ts->n_filt;f++){
        unsigned order=ts->f[f].order;
        if(analysis&&order>maxo)order=maxo;
        if(!order)continue;
        double lpc[34],state[32];
        tns_lpc(order,ts->coef_res,ts->f[f].coef,lpc);
        unsigned st=ts->f[f].start,sp=ts->f[f].stop;
        if(st>maxb)st=maxb;
        if(st>nbands)st=nbands;
        if(sp>maxb)sp=maxb;
        if(sp>nbands)sp=nbands;
        const int a0=st>0u?(int)off[st]:0,a1=sp>0u?(int)off[sp]:0;
        const int size=a1-a0;
        if(size<=0)continue;
        const int inc=ts->f[f].direction?-1:1;
        double *x=spec+a0;
        if(inc<0)x+=size-1;
        for(unsigned i=0;i<order;i++)state[i]=0.0;
        for(int i=0;i<size;i++){
            double y=*x;
            if(analysis){
                for(unsigned j=0;j<order;j++)y+=lpc[j+1u]*state[j];
                for(unsigned j=order-1u;j>0u;j--)state[j]=state[j-1u];
                state[0]=*x;
            }else{
                for(unsigned j=0;j<order;j++)y-=lpc[j+1u]*state[j];
                for(unsigned j=order-1u;j>0u;j--)state[j]=state[j-1u];
                state[0]=y;
            }
            *x=y;x+=inc;
        }
    }
}

/* ---- long term prediction ---------------------------------------------------------------------------------------------------------------------------------------------- */
/* forward MDCT of 2n windowed samples (the inverse transform carries the DCT-IV) */
static int mdct_fwd(const float *in,unsigned n,float *out){
    float u[MAXBLOCK],y[2*MAXBLOCK];
    const unsigned h=n/2u;
    for(unsigned m=0;m<h;m++)u[m]=-in[3u*h-1u-m]-in[3u*h+m];
    for(unsigned m=h;m<n;m++)u[m]=in[m-h]-in[3u*h-1u-m];
    if(!sauti_aac_imdct(u,n,y))return 0;
    for(unsigned m=0;m<h;m++)out[m]=-(float)h*y[3u*h-1u-m];
    for(unsigned m=h;m<n;m++)out[m]=(float)h*y[m-h];
    return 1;
}
#define LTP_LEN (3u*MAXBLOCK)
static int tns_ok(const Tns *t,unsigned longf){
    (void)longf;
    for(unsigned w=0;w<t->n_sub;w++)for(unsigned f=0;f<t->sub[w].n_filt;f++)if(t->sub[w].f[f].order>31u)return 0;
    return 1;
}
static void ics_of(const SautiAacTwinVq *s,unsigned ws,unsigned shape,unsigned max_sfb,SautiAacIcsInfo *ics){
    (void)s;
    memset(ics,0,sizeof(*ics));
    ics->window_sequence=ws;ics->window_shape=shape;ics->max_sfb=max_sfb;
    if(ws==2u){ics->window_count=8;ics->group_count=8;for(unsigned i=0;i<8;i++)ics->group_length[i]=1;}
    else{ics->window_count=1;ics->group_count=1;ics->group_length[0]=1;}
}
/* adds the prediction to the spectrum of channel c; `td` is the TNS data of the frame (NULL without TNS) */
static int ltp_add(SautiAacTwinVq *s,unsigned c,unsigned ws,unsigned shape,const Ltp *l,const Tns *tns,unsigned idx,double *spec){
    const unsigned block=s->block,pshape=s->prev_shape[c];
    const double *buf=s->ltp[c];
    const uint16_t *off=NULL;unsigned nb=0;
    if(!l->present)return 1;
    if(ws!=2u){
        float w[2*MAXBLOCK],pred[2*MAXBLOCK],freq[MAXBLOCK];
        const int j=(int)LTP_LEN-2*(int)block-(l->lag[0]-1024);
        int num=2*(int)block;
        if((int)LTP_LEN-j<2*(int)block)num=(int)LTP_LEN-j;
        if(!sauti_aac_window_long_weights(block,ws,pshape,shape,w))return 0;
        for(int i=0;i<2*(int)block;i++){
            const int k=i+j;
            pred[i]=(i<num&&k>=0&&k<(int)LTP_LEN)?(float)(l->weight*buf[k])*w[i]:0.0f;
        }
        if(!mdct_fwd(pred,block,freq))return 0;
        if(!sauti_aac_band_offsets(idx,0u,&off,&nb))return 0;
        if(tns&&tns->active&&tns->sub[0].n_filt){
            double full[MAXBLOCK];
            for(unsigned i=0;i<block;i++)full[i]=(double)freq[i];
            tns_window(full,&tns->sub[0],1u,s->rate_index,nb,off,1);
            for(unsigned i=0;i<block;i++)freq[i]=(float)full[i];
        }
        if(!sauti_aac_band_offsets(idx,0u,&off,&nb))return 0;
        unsigned j2=0;
        for(unsigned i=0;i<nb;i++){
            if(l->sfb_used[i+1u]==0u)for(;j2<off[i+1u];j2++)freq[j2]=0.0f;
            else j2=off[i+1u];
        }
        for(unsigned i=0;i<block;i++)spec[i]+=(double)freq[i];
    }else{
        const unsigned ns=block/N_SHORT;
        unsigned last=l->sfb_used[1]<8u?l->sfb_used[1]:8u;
        float wp[2*MAXBLOCK/N_SHORT],wc[2*MAXBLOCK/N_SHORT];
        if(!l->sbk_used[0]||!last)return 1;
        if(!sauti_aac_window_generate(ns,pshape,wp)||!sauti_aac_window_generate(ns,shape,wc))return 0;
        if(!sauti_aac_band_offsets(idx,1u,&off,&nb))return 0;
        if(last>nb)last=nb;
        for(unsigned sb=0;sb<N_SHORT;sb++){
            if(!l->sbk_used[sb+1u])continue;
            float pred[2*MAXBLOCK/N_SHORT],freq[MAXBLOCK/N_SHORT];
            int num=2*(int)ns;
            const int j=(int)LTP_LEN-num-l->lag[sb];
            if((int)LTP_LEN-j<2*(int)ns)num=(int)LTP_LEN-j;
            for(int i=0;i<2*(int)ns;i++){
                const int k=i+j;
                float v=(i<num&&k>=0&&k<(int)LTP_LEN)?(float)(l->weight*buf[k]):0.0f;
                v*=i<(int)ns?wp[i]:wc[i];
                pred[i]=v;
            }
            if(!mdct_fwd(pred,ns,freq))return 0;
            if(tns&&tns->active&&sb<tns->n_sub&&tns->sub[sb].n_filt){
                double full[MAXBLOCK/N_SHORT];
                for(unsigned i=0;i<ns;i++)full[i]=(double)freq[i];
                tns_window(full,&tns->sub[sb],0u,s->rate_index,last,off,1);
                for(unsigned i=0;i<ns;i++)freq[i]=(float)full[i];
            }
            for(unsigned i=0;i<off[last];i++)spec[sb*ns+i]+=(double)freq[i];
        }
    }
    return 1;
}
static void ltp_update(SautiAacTwinVq *s,unsigned c,const float *pcm,const float *overlap){
    double *b=s->ltp[c];
    const unsigned block=s->block;
    for(unsigned i=0;i<LTP_LEN-2u*block;i++)b[i]=b[i+block];
    for(unsigned i=0;i<block;i++){b[LTP_LEN-2u*block+i]=(double)pcm[i];b[LTP_LEN-block+i]=(double)overlap[i];}
}

/* ---- the access unit --------------------------------------------------------------------------------------------------------------------------------------------------------- */
int sauti_aac_twinvq_decode(SautiAacTwinVq *s,const uint8_t *au,size_t length,float *pcm,SautiAacReason *reason){
    SautiAacReason local;
    if(!reason)reason=&local;
    *reason=SAUTI_AAC_REASON_INVALID_SCE;
    (void)pcm;
    if(!s||!au||length<2u)return 0;
    Br br={au,length*8u,0,0};
    Idx *x=(Idx *)calloc(1,sizeof(*x));
    SautiAacTwinVq *t=(SautiAacTwinVq *)malloc(sizeof(*t));
    if(t)memcpy(t,s,sizeof(*t));
    if(!x||!t){free(x);free(t);return 0;}
    static const unsigned wsmap[4]={0,1,2,3};
    const unsigned ws=wsmap[get(&br,2)];
    const unsigned shape=get(&br,1);
    const unsigned ch=s->channels,block=s->block,longf=ws!=2u;
    const unsigned nsf=longf?1u:N_SHORT;
    unsigned dec=3;
    Side *sd=(Side *)calloc(1,sizeof(*sd));
    if(!sd){free(x);free(t);return 0;}
    {   /* band count of the coded range, then stereo mask, long term prediction and TNS of every channel */
        const unsigned idx=SAUTI_AAC_IDX(s->rate_index,block==960u?SAUTI_AAC_LAYOUT_960:SAUTI_AAC_LAYOUT_1024);
        const uint16_t *off=NULL;unsigned nb=0;
        if(!sauti_aac_band_offsets(idx,longf?0u:1u,&off,&nb)){free(x);free(sd);free(t);return 0;}
        float ft=(float)s->band_upper;
        ft*=(float)(longf?block:block/N_SHORT);
        const unsigned max_line=(unsigned)(int)ft;
        unsigned sb_ind=1;
        for(unsigned sb=1;sb<=nb&&off[sb]<max_line;sb++)sb_ind=sb+1u;
        sd->max_sfb=sb_ind>nb?nb:sb_ind;
        if(ch>1u){
            sd->ms_mask=get(&br,2);dec+=2;
            if(sd->ms_mask==1u){
                unsigned ng=1,glen[8]={1};
                if(!longf){
                    sd->group_code=get(&br,7);dec+=7;
                    unsigned rl=1;ng=0;
                    for(unsigned m=0;m<7u;m++){if(((sd->group_code>>(6u-m))&1u)==0u){glen[ng++]=rl;rl=0;}rl++;}
                    glen[ng++]=rl;
                }
                unsigned win=0;
                for(unsigned g=0;g<ng;g++){
                    for(unsigned sfb=0;sfb<sd->max_sfb;sfb++){sd->ms[win][sfb]=(uint8_t)get(&br,1);dec++;}
                    for(unsigned gw=1;gw<glen[g];gw++)for(unsigned sfb=0;sfb<sd->max_sfb;sfb++)sd->ms[win+gw][sfb]=sd->ms[win][sfb];
                    win+=glen[g];
                }
            }else if(sd->ms_mask==2u)for(unsigned w=0;w<N_SHORT;w++)for(unsigned sfb=0;sfb<sd->max_sfb;sfb++)sd->ms[w][sfb]=1;
        }
        for(unsigned c=0;c<ch;c++){
            Ltp *l=&sd->ltp[c];
            l->sbk_used[0]=(uint8_t)get(&br,1);dec++;
            if(l->sbk_used[0]){
                l->lag[0]=(int)get(&br,11);l->weight=ltp_weight[get(&br,3)];dec+=14;
                if(longf){
                    const unsigned last=(sd->max_sfb<40u?sd->max_sfb:40u)+1u;
                    l->sfb_used[0]=l->sbk_used[0];
                    for(unsigned i=1;i<last;i++)l->sfb_used[i]=(uint8_t)get(&br,1);
                    dec+=last-1u;
                }else{
                    const int lag0=l->lag[0];
                    l->sfb_used[0]=l->sbk_used[0];l->sfb_used[1]=(uint8_t)sd->max_sfb;
                    dec+=N_SHORT;
                    for(unsigned i=0;i<N_SHORT;i++){
                        l->lag[i]=0;
                        l->sbk_used[i+1u]=(uint8_t)get(&br,1);
                        if(l->sbk_used[i+1u]){
                            dec++;
                            l->lag[i]=lag0;
                            if(get(&br,1)){dec+=5;const int tmp=(int)get(&br,5)-8;l->lag[i]=tmp+lag0;}
                        }
                    }
                }
            }
            l->present=l->sbk_used[0];
            Tns *tn=&sd->tns[c];
            const unsigned tns_on=get(&br,1);dec++;
            if(tns_on){
                tn->active=1;tn->n_sub=longf?1u:N_SHORT;
                for(unsigned w=0;w<tn->n_sub;w++){
                    TnsSub *ts=&tn->sub[w];
                    const unsigned nbits=longf?2u:1u;
                    ts->n_filt=(uint8_t)get(&br,nbits);dec+=nbits;
                    if(!ts->n_filt)continue;
                    ts->coef_res=(uint8_t)(get(&br,1)+3u);dec++;
                    unsigned top=nb;
                    for(unsigned f=0;f<ts->n_filt;f++){
                        ts->f[f].stop=(uint8_t)top;
                        const unsigned lb=longf?6u:4u,ob=longf?5u:3u;
                        const unsigned len=get(&br,lb);dec+=lb;
                        ts->f[f].len=(uint8_t)len;top=top>len?top-len:0u;ts->f[f].start=(uint8_t)top;
                        ts->f[f].order=(uint8_t)get(&br,ob);dec+=ob;
                        if(ts->f[f].order){
                            ts->f[f].direction=(uint8_t)get(&br,1);const unsigned compress=get(&br,1);ts->f[f].compress=(uint8_t)compress;dec+=2;
                            const unsigned res2=ts->coef_res-compress;
                            if(res2<2u||res2>4u){free(x);free(sd);free(t);return 0;}
                            for(unsigned k=0;k<ts->f[f].order;k++){
                                int v=(int)get(&br,res2);dec+=res2;
                                if(v&(1<<(res2-1u)))v-=1<<res2;
                                ts->f[f].coef[k]=(int8_t)v;
                            }
                        }
                    }
                }
            }
        }
        if(br.bad){free(x);free(sd);free(t);return 0;}
    }
    {   /* BitUnPack */
        const unsigned avail=(unsigned)(length*8u)-dec;
        unsigned side,vq=avail;
        if(longf){
            x->bandlimit=(int)get(&br,1);x->pitch_q=(int)get(&br,1);x->pf=(int)get(&br,1);
            side=(19u+9u+(FW_N_BIT*FW_N_DIV+1u))*ch+3u;
            if(vq<=side){free(x);free(sd);free(t);return 0;}
            vq-=side;
            if(x->pitch_q){const unsigned pb=(PIT_N_BIT+BASF_BIT+PGAIN_BIT)*ch;if(vq<=pb){free(x);free(sd);free(t);return 0;}vq-=pb;}
        }else{
            x->bandlimit=(int)get(&br,1);
            side=(19u+(GAIN_BITS+SUB_GAIN_BITS*N_SHORT))*ch+1u;
            if(vq<=side){free(x);free(sd);free(t);return 0;}
            vq-=side;
        }
        if(x->bandlimit==1){const unsigned bb=BWID_BITS*ch;if(vq<=bb){free(x);free(sd);free(t);return 0;}vq-=bb;}
        x->vq_bits=vq;
        for(unsigned c=0;c<ch;c++)if(x->bandlimit==1){x->blim_h[c]=(int)get(&br,BLIM_BITS_H);x->blim_l[c]=(int)get(&br,BLIM_BITS_L);}
        if(longf&&x->pitch_q){
            unsigned bits[8],len[8];
            const unsigned nd=vec_len_p(ch,bits,len);
            for(unsigned i=0;i<nd;i++){const unsigned b0=(bits[i]+1u)/2u,b1=bits[i]/2u;x->pls[i]=(int)get(&br,b0);x->pls[i+nd]=(int)get(&br,b1);}
            for(unsigned c=0;c<ch;c++){x->pit[c]=(int)get(&br,BASF_BIT);x->pgain[c]=(int)get(&br,PGAIN_BIT);}
        }
        const unsigned ndiv=(vq+MAXBIT*2u-1u)/(MAXBIT*2u);
        if(ndiv*2u>1024u){free(x);free(sd);free(t);return 0;}
        for(unsigned i=0;i<ndiv;i++){
            const unsigned bits=(vq+ndiv-1u-i)/ndiv,b0=(bits+1u)/2u,b1=bits/2u;
            x->wvq[i]=(int)get(&br,b0);x->wvq[i+ndiv]=(int)get(&br,b1);
        }
        if(longf){
            for(unsigned c=0;c<ch;c++)for(unsigned i=0;i<FW_N_DIV;i++)x->fw[i+c*FW_N_DIV]=(int)get(&br,FW_N_BIT);
            for(unsigned c=0;c<ch;c++)x->fw_alf[c]=(int)get(&br,1);
        }
        for(unsigned c=0;c<ch;c++){
            if(nsf==1u)x->pow[c]=(int)get(&br,GAIN_BITS);
            else{const unsigned top=(nsf+1u)*c;x->pow[top]=(int)get(&br,GAIN_BITS);for(unsigned i=0;i<nsf;i++)x->pow[top+i+1u]=(int)get(&br,SUB_GAIN_BITS);}
        }
        for(unsigned c=0;c<ch;c++){x->lsp[c][0]=(int)get(&br,1);x->lsp[c][1]=(int)get(&br,6);for(unsigned i=0;i<LSP_SPLIT;i++)x->lsp[c][i+2u]=(int)get(&br,4);}
        if(br.bad){free(x);free(sd);free(t);return 0;}
    }
    {
        double *tmp=(double *)calloc((size_t)block*ch,sizeof(double));
        if(!tmp||!spectrum(t,x,ws,tmp)){free(tmp);free(x);free(sd);free(t);return 0;}
        if(ch==2u&&sd->ms_mask){        /* mid / side bands */
            const uint16_t *off=NULL;unsigned nb=0;
            const unsigned idx=SAUTI_AAC_IDX(s->rate_index,block==960u?SAUTI_AAC_LAYOUT_960:SAUTI_AAC_LAYOUT_1024);
            if(!sauti_aac_band_offsets(idx,longf?0u:1u,&off,&nb)){free(tmp);free(x);free(sd);free(t);return 0;}
            const unsigned win_n=longf?1u:N_SHORT,per=block/win_n;
            for(unsigned w=0;w<win_n;w++){
                unsigned so=w*per;
                for(unsigned sb=0;sb<sd->max_sfb&&sb<nb;sb++){
                    const unsigned width=off[sb+1u]-off[sb];
                    if(sd->ms[w][sb])for(unsigned i=so;i<so+width;i++){const double d=tmp[i]-tmp[i+block];tmp[i]=tmp[i]+tmp[i+block];tmp[i+block]=d;}
                    so+=width;
                }
            }
        }
        {   /* the gain of the filter bank of the reference decoder */
            const double g=sqrt((double)(longf?block:block/N_SHORT)*2.0);
            for(unsigned c=0;c<ch;c++)for(unsigned i=0;i<block;i++)t->spec[c][i]=tmp[c*block+i]*g;
        }
        free(tmp);
    }
    {
        const unsigned idx=SAUTI_AAC_IDX(s->rate_index,block==960u?SAUTI_AAC_LAYOUT_960:SAUTI_AAC_LAYOUT_1024);
        float *out=(float *)malloc(sizeof(float)*MAXBLOCK*ch);
        if(!out){free(x);free(sd);free(t);return 0;}
        for(unsigned c=0;c<ch;c++){
            SautiAacIcsInfo ics;float sp[MAXBLOCK];
            if(!tns_ok(&sd->tns[c],longf)){free(out);free(x);free(sd);free(t);return 0;}
            if(!ltp_add(t,c,ws,shape,&sd->ltp[c],&sd->tns[c],idx,t->spec[c])){free(out);free(x);free(sd);free(t);return 0;}
            {
                double work[MAXBLOCK];
                const uint16_t *off=NULL;unsigned nb=0;
                memcpy(work,t->spec[c],block*sizeof(double));
                if(sd->tns[c].active){
                    if(!sauti_aac_band_offsets(idx,longf?0u:1u,&off,&nb)){free(out);free(x);free(sd);free(t);return 0;}
                    for(unsigned w=0;w<sd->tns[c].n_sub;w++)tns_window(work+w*(block/(longf?1u:N_SHORT)),&sd->tns[c].sub[w],longf,s->rate_index,sd->max_sfb,off,0);
                }
                for(unsigned i=0;i<block;i++)sp[i]=(float)work[i];
            }
            for(unsigned i=block;i<MAXBLOCK;i++)sp[i]=0.0f;
            ics_of(t,ws,shape,sd->max_sfb,&ics);
            if(!sauti_aac_lc_synthesis_spectral(&t->syn[c],&ics,NULL,idx,sp,out+c*MAXBLOCK)){free(out);free(x);free(sd);free(t);return 0;}
            ltp_update(t,c,out+c*MAXBLOCK,t->syn[c].overlap);
            t->prev_shape[c]=shape;
        }
        for(unsigned c=0;c<ch;c++)for(unsigned i=0;i<block;i++)if(!isfinite(out[c*MAXBLOCK+i])){free(out);free(x);free(sd);free(t);return 0;}
        if(pcm)for(unsigned c=0;c<ch;c++)for(unsigned i=0;i<block;i++)pcm[(size_t)i*ch+c]=out[c*MAXBLOCK+i]*TWINVQ_PCM_SCALE;
        free(out);
    }
    t->last_ws=ws;t->last_shape=shape;
    free(x);free(sd);
    memcpy(s,t,sizeof(*s));
    free(t);
    *reason=SAUTI_AAC_REASON_OK;
    return 1;
}
