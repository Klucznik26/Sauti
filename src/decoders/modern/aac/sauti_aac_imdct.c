/* Native AAC IMDCT as a DCT-IV through one complex FFT of N/2 points (O(N log N), no allocation).
 * out[t] = 2/N * sum_k X[k] cos(pi/N * (t+1/2+N/2) * (k+1/2)),  t = 0..2N-1.
 * With y[m] = sum_k X[k] cos(pi/N (m+1/2)(k+1/2)) (the DCT-IV) the kernel's symmetries give
 *   t+N/2 in [N/2,N): y[t+N/2];  in [N,2N): -y[2N-1-(t+N/2)];  in [2N,5N/2): -y[t+N/2-2N].
 * DCT-IV by an N/2-point FFT: z[n] = (X[2n] + i X[N-1-2n]) e^{-i pi (4n+1)/(4N)}, Z = FFT(z), w[k] = Z[k] e^{-i pi k/N},
 * y[2k] = Re w[k], y[N-1-2k] = -Im w[k].  All in double precision; the twiddles are evaluated directly (never by recurrence) once per size.
 * No external FFT dependency and no modification of input on failure. */
#include "sauti_aac_imdct.h"
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { double re,im; } AacComplex;
typedef struct {
    size_t n;
    double pre_c[512],pre_s[512];       /* cos and sin of pi (4n+1)/(4N) */
    double post_c[512],post_s[512];     /* cos and sin of pi k/N */
    double fft_c[256],fft_s[256];       /* e^{-2 pi i j/(N/2)}: cos and sin of 2 pi j/(N/2), j < N/4 */
    uint16_t reverse[512];
} ImdctPlan;
static ImdctPlan plans[3];              /* 128 (short), 512 (AAC-LD / AAC-ELD), 1024 (long) */
static atomic_int plan_state[3];        /* 0 = not built, 1 = being built, 2 = ready */

static void plan_build(ImdctPlan *p,size_t n){
    const double pi=3.14159265358979323846264338327950288;
    const size_t half=n/2u;
    p->n=n;
    for(size_t i=0;i<half;i++){
        const double a=pi*(4.0*(double)i+1.0)/(4.0*(double)n);
        p->pre_c[i]=cos(a);p->pre_s[i]=sin(a);
        const double b=pi*(double)i/(double)n;
        p->post_c[i]=cos(b);p->post_s[i]=sin(b);
    }
    for(size_t j=0;j<half/2u;j++){
        const double a=2.0*pi*(double)j/(double)half;
        p->fft_c[j]=cos(a);p->fft_s[j]=sin(a);
    }
    for(size_t i=1,j=0;i<half;i++){
        size_t bit=half>>1u;
        while(j&bit){j^=bit;bit>>=1u;}
        j^=bit;
        p->reverse[i]=(uint16_t)j;
    }
    p->reverse[0]=0;
}
static const ImdctPlan *plan_get(size_t n){
    const unsigned which=n==1024u?2u:n==512u?1u:0u;
    ImdctPlan *p=&plans[which];
    int expected=0;
    if(atomic_compare_exchange_strong(&plan_state[which],&expected,1)){
        plan_build(p,n);
        atomic_store(&plan_state[which],2);
    }else while(atomic_load(&plan_state[which])!=2){}
    return p;
}
/* DCT-IV y[m] = sum_k X[k] cos(pi/(4N) (2m+1)(2k+1)) for a length that is not a power of two (AAC-LD / ELD frame length 480): direct O(N^2) sum over a table of
 * cos(pi j/(4N)), j < 8N.  Slower than the FFT path but exact (double) and allocation free for the one length that needs it. */
static void dct4_direct(const float *x,size_t n,double *y){
    static double table[3840];                 /* 8 * 480 */
    static atomic_int state;                   /* 0 = not built, 1 = being built, 2 = ready */
    int expected=0;
    const double pi=3.14159265358979323846264338327950288;
    if(atomic_compare_exchange_strong(&state,&expected,1)){
        for(size_t j=0;j<8u*480u;j++)table[j]=cos(pi*(double)j/(4.0*480.0));
        atomic_store(&state,2);
    }else while(atomic_load(&state)!=2){}
    for(size_t m=0;m<n;m++){
        double acc=0.0;
        for(size_t k=0;k<n;k++)acc+=(double)x[k]*table[((2u*m+1u)*(2u*k+1u))%(8u*n)];
        y[m]=acc;
    }
}
/* In-place radix-2 FFT of `half` points, e^{-2 pi i nk/half}. */
static void fft_half(AacComplex *a,const ImdctPlan *p){
    const size_t half=p->n/2u;
    for(size_t i=1;i<half;i++){
        const size_t j=p->reverse[i];
        if(i<j){const AacComplex t=a[i];a[i]=a[j];a[j]=t;}
    }
    for(size_t span=2;span<=half;span<<=1u){
        const size_t step=half/span;
        for(size_t start=0;start<half;start+=span){
            for(size_t j=0;j<span/2u;j++){
                const double wr=p->fft_c[j*step],wi=-p->fft_s[j*step];
                AacComplex *u=&a[start+j],*v=&a[start+j+span/2u];
                const double vr=v->re*wr-v->im*wi,vi=v->re*wi+v->im*wr;
                const double ur=u->re,ui=u->im;
                u->re=ur+vr;u->im=ui+vi;
                v->re=ur-vr;v->im=ui-vi;
            }
        }
    }
}
int sauti_aac_imdct(const float *spectral,size_t n,float *time_samples){
    if(!spectral || !time_samples || (n!=128u && n!=512u && n!=480u && n!=1024u))return 0;
    for(size_t k=0;k<n;k++)if(!isfinite(spectral[k]))return 0;
#ifdef SAUTI_AAC_DIAG_SHORT_DIRECT_IMDCT
    /* Diagnostic only: direct textbook IMDCT for the 128-bin short
     * windows. This bypasses the FFT but not dequant, stereo, overlap
     * or the production decoder. Cost O(N^2), never use in player. */
    if(n==128u){
        const double pi=3.14159265358979323846264338327950288;
        float direct[256];
        for(size_t t=0;t<256;t++){
            double acc=0.0;
            for(size_t k=0;k<128;k++)
                acc+=(double)spectral[k]*cos((pi/128.0)*
                            ((double)t+0.5+64.0)*((double)k+0.5));
            const double value=acc*(2.0/128.0);
            if(!isfinite(value)||fabs(value)>3.402823466e38)
                return 0;
            direct[t]=(float)value;
        }
        memcpy(time_samples,direct,sizeof(direct));
        return 1;
    }
#endif
    const size_t half=n/2u;
    double y[1024];
    if(n==480u){
        dct4_direct(spectral,n,y);
    }else{
        const ImdctPlan *plan=plan_get(n);
        AacComplex z[512];
        for(size_t i=0;i<half;i++){
            const double a=(double)spectral[2u*i],b=(double)spectral[n-1u-2u*i];
            const double c=plan->pre_c[i],s2=plan->pre_s[i];       /* multiply by e^{-i angle} = c - i s */
            z[i].re=a*c+b*s2;z[i].im=b*c-a*s2;
        }
        fft_half(z,plan);
        for(size_t k=0;k<half;k++){
            const double c=plan->post_c[k],s2=plan->post_s[k];
            const double wr=z[k].re*c+z[k].im*s2,wi=z[k].im*c-z[k].re*s2;
            y[2u*k]=wr;y[n-1u-2u*k]=-wi;
        }
    }
    float next[2048];                                              /* 2n samples: up to 2048 for n = 1024 */
    const double normalized=2.0/(double)n;
    for(size_t t=0;t<2u*n;t++){
        const size_t m=t+half;
        const double v=m<n?y[m]:m<2u*n?-y[2u*n-1u-m]:-y[m-2u*n];
        const double value=v*normalized;
        if(!isfinite(value) || fabs(value)>3.402823466e38)return 0;
        next[t]=(float)value;
    }
    memcpy(time_samples,next,2u*n*sizeof(float));
    return 1;
}
