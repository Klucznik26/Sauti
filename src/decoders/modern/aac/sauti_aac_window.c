/* AAC-LC window synthesis, 1024/128 sine and Kaiser-Bessel derived.
 * Window placement follows MPEG-4 AAC 1024-sample frame geometry.
 * Uses original Sauti NASM overlap-add to merge the two half frames.
 */
#include "sauti_aac_window.h"
#include "sauti_aac_syntax.h"
#include <math.h>
#include <string.h>

static double bessel_i0(double x){
    /* Positive power series; x <= pi*6. */
    double z=x*x*0.25,term=1.0,sum=1.0;
    for(unsigned k=1;k<=100;k++){
        term*=z/((double)k*(double)k);
        sum+=term;
        if(term<sum*1e-16)break;
    }
    return sum;
}
int sauti_aac_window_generate(unsigned n,unsigned shape,float *window){
    if(!window || (n!=128u && n!=1024u) || shape>1u)return 0;
#ifdef SAUTI_AAC_DIAG_FORCE_SINE_WINDOWS
    /* Diagnostic experiment only. Do not enable in production: an AAC
     * KBD window must ordinarily retain the shape signalled by ICS. */
    shape=0u;
#endif
    const double pi=3.14159265358979323846264338327950288;
    if(shape==0u){
        for(unsigned i=0;i<n;i++){
            float value=(float)sin(pi*((double)i+0.5)/(2.0*n));
            window[i]=value;
            window[2u*n-1u-i]=value;
        }
    }else{
        /* KBD uses N+1 Kaiser samples; the extra endpoint sample
         * ensures Princen-Bradley complementarity of opposing halves. */
        const double alpha=n==1024u?4.0:6.0;
        const double factor=2.0*pi*alpha/(double)n;
        double denominator=0.0,acc=0.0;
        for(unsigned i=0;i<=n;i++){
            double z=factor*sqrt((double)i*((double)n-(double)i));
            denominator+=bessel_i0(z);
        }
        for(unsigned i=0;i<n;i++){
            double z=factor*sqrt((double)i*((double)n-(double)i));
            acc+=bessel_i0(z);
            float value=(float)sqrt(acc/denominator);
            window[i]=value;
            window[2u*n-1u-i]=value;
        }
    }
    return 1;
}
int sauti_aac_window_synth(unsigned sequence,unsigned previous_shape,
                           unsigned current_shape,const float *imdct,
                           const float previous_overlap[1024],
                           float pcm[1024],float next_overlap[1024]){
    if(sequence>3u || previous_shape>1u || current_shape>1u ||
       !imdct || !previous_overlap || !pcm || !next_overlap)return 0;
    for(unsigned i=0;i<2048u;i++)
        if(!isfinite(imdct[i]))return 0;
    for(unsigned i=0;i<1024;i++)
        if(!isfinite(previous_overlap[i]))return 0;
    float long_prev[2048],long_curr[2048],short_prev[256],short_curr[256];
    if(!sauti_aac_window_generate(1024,previous_shape,long_prev) ||
       !sauti_aac_window_generate(1024,current_shape,long_curr) ||
       !sauti_aac_window_generate(128,previous_shape,short_prev) ||
       !sauti_aac_window_generate(128,current_shape,short_curr))return 0;
    float frame[2048]={0};
    if(sequence==0u){
        for(unsigned i=0;i<1024;i++){
            frame[i]=imdct[i]*long_prev[i];
            frame[1024+i]=imdct[1024+i]*long_curr[1024+i];
        }
    }else if(sequence==1u){
        for(unsigned i=0;i<1024;i++)frame[i]=imdct[i]*long_prev[i];
        for(unsigned i=1024;i<1472;i++)frame[i]=imdct[i];
        for(unsigned i=1472;i<1600;i++)
            frame[i]=imdct[i]*short_curr[128u+(i-1472u)];
    }else if(sequence==3u){
        for(unsigned i=448;i<576;i++)
            frame[i]=imdct[i]*short_prev[i-448u];
        for(unsigned i=576;i<1024;i++)frame[i]=imdct[i];
        for(unsigned i=1024;i<2048;i++)
            frame[i]=imdct[i]*long_curr[i];
    }else{
        /* First short rising half uses previous shape; all subsequent
         * rising halves and all falling halves use current shape. */
        for(unsigned block=0;block<8;block++){
            unsigned base=448u+128u*block;
            for(unsigned i=0;i<256;i++){
                float w=i<128u ?
#ifdef SAUTI_AAC_DIAG_SHORT_FIRST_CURRENT
                    short_curr[i] :
#else
                    (block==0u?short_prev[i]:short_curr[i]) :
#endif
                    short_curr[i];
                frame[base+i]+=imdct[block*256u+i]*w;
            }
        }
    }
    for(unsigned i=0;i<2048;i++)if(!isfinite(frame[i]))return 0;
    float ones[2048],history[1024],result[1024];
    for(unsigned i=0;i<2048;i++)ones[i]=1.0f;
    memcpy(history,previous_overlap,sizeof(history));
    if(!sauti_aac_window_overlap_asm(frame,ones,history,result,1024))return 0;
    for(unsigned i=0;i<1024;i++)
        if(!isfinite(history[i]) || !isfinite(result[i]))return 0;
    memcpy(pcm,result,sizeof(result));
    memcpy(next_overlap,history,sizeof(history));
    return 1;
}
