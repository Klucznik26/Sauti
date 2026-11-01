/* Native AAC IMDCT through a sparse real-input radix-2 FFT.
 * The legacy reference used 2*N dot-products of N terms: O(N^2).
 * With L=8*N, x[2*k+1]=spectral[k], all other x=0, then
 * Re(FFT(x)[2*t+N+1]) = sum_k spectral[k] *
 *   cos(pi/N * (t+1/2+N/2) * (k+1/2)).
 * This is exactly the IMDCT expression, and costs O(N log N).
 * No external FFT dependency and no modification of input on failure. */
#include "sauti_aac_imdct.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { double re,im; } AacComplex;
static void fft_forward(AacComplex *a,size_t length){
    /* Iterative in-place Cooley-Tukey, natural frequency order. */
    for(size_t i=1,j=0;i<length;i++){
        size_t bit=length>>1u;
        while(j&bit){j^=bit;bit>>=1u;}
        j^=bit;
        if(i<j){
            const AacComplex tmp=a[i];a[i]=a[j];a[j]=tmp;
        }
    }
    const double pi=3.14159265358979323846264338327950288;
    for(size_t span=2;span<=length;span<<=1u){
        const double angle=-2.0*pi/(double)span;
        const double wr_step=cos(angle),wi_step=sin(angle);
        for(size_t start=0;start<length;start+=span){
            double wr=1.0,wi=0.0;
            for(size_t j=0;j<span/2u;j++){
                AacComplex *u=&a[start+j];
                AacComplex *v=&a[start+j+span/2u];
                const double vr=v->re*wr-v->im*wi;
                const double vi=v->re*wi+v->im*wr;
                const double ur=u->re,ui=u->im;
                u->re=ur+vr;u->im=ui+vi;
                v->re=ur-vr;v->im=ui-vi;
                const double next_wr=wr*wr_step-wi*wi_step;
                wi=wr*wi_step+wi*wr_step;
                wr=next_wr;
            }
        }
    }
}
int sauti_aac_imdct(const float *spectral,size_t n,float *time_samples){
    if(!spectral || !time_samples || (n!=128u && n!=1024u))return 0;
    for(size_t k=0;k<n;k++)if(!isfinite(spectral[k]))return 0;
    const size_t length=8u*n;
    AacComplex *fft=calloc(length,sizeof(*fft));
    if(!fft)return 0;
    for(size_t k=0;k<n;k++)fft[2u*k+1u].re=spectral[k];
    fft_forward(fft,length);
    float next[2048];
    const double normalized=2.0/(double)n;
    for(size_t t=0;t<2u*n;t++){
        const size_t bin=2u*t+n+1u;
        const double value=fft[bin].re*normalized;
        if(!isfinite(value) || fabs(value)>3.402823466e38){
            free(fft);return 0;
        }
        next[t]=(float)value;
    }
    free(fft);
    memcpy(time_samples,next,2u*n*sizeof(float));
    return 1;
}
