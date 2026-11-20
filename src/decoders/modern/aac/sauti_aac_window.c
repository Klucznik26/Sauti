/* AAC-LC window synthesis, 1024/128 sine and Kaiser-Bessel derived.
 * Window placement follows MPEG-4 AAC 1024-sample frame geometry.
 * Uses original Sauti NASM overlap-add to merge the two half frames.
 * The geometry is parameterised by the long length n (1024, or 960 for frameLengthFlag = 1) with n/8 short windows (128 / 120).
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
    if(!window || (n!=128u && n!=1024u && n!=120u && n!=960u) || shape>1u)return 0;
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
        const double alpha=(n==1024u||n==960u)?4.0:6.0;
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
int sauti_aac_window_synth_n(unsigned n,unsigned sequence,unsigned previous_shape,
                             unsigned current_shape,const float *imdct,
                             const float *previous_overlap,
                             float *pcm,float *next_overlap){
    if((n!=1024u && n!=960u) || sequence>3u || previous_shape>1u || current_shape>1u ||
       !imdct || !previous_overlap || !pcm || !next_overlap)return 0;
    const unsigned ns=n/8u,lead=(n-ns)/2u;           /* short window length, offset of the first short block in the 2n frame (448 / 420) */
    for(unsigned i=0;i<2u*n;i++)
        if(!isfinite(imdct[i]))return 0;
    for(unsigned i=0;i<n;i++)
        if(!isfinite(previous_overlap[i]))return 0;
    float long_prev[2048],long_curr[2048],short_prev[256],short_curr[256];
    if(!sauti_aac_window_generate(n,previous_shape,long_prev) ||
       !sauti_aac_window_generate(n,current_shape,long_curr) ||
       !sauti_aac_window_generate(ns,previous_shape,short_prev) ||
       !sauti_aac_window_generate(ns,current_shape,short_curr))return 0;
    float frame[2048]={0};
    if(sequence==0u){
        for(unsigned i=0;i<n;i++){
            frame[i]=imdct[i]*long_prev[i];
            frame[n+i]=imdct[n+i]*long_curr[n+i];
        }
    }else if(sequence==1u){
        for(unsigned i=0;i<n;i++)frame[i]=imdct[i]*long_prev[i];
        for(unsigned i=n;i<n+lead;i++)frame[i]=imdct[i];
        for(unsigned i=n+lead;i<n+lead+ns;i++)
            frame[i]=imdct[i]*short_curr[ns+(i-(n+lead))];
    }else if(sequence==3u){
        for(unsigned i=lead;i<lead+ns;i++)
            frame[i]=imdct[i]*short_prev[i-lead];
        for(unsigned i=lead+ns;i<n;i++)frame[i]=imdct[i];
        for(unsigned i=n;i<2u*n;i++)
            frame[i]=imdct[i]*long_curr[i];
    }else{
        /* First short rising half uses previous shape; all subsequent
         * rising halves and all falling halves use current shape. */
        for(unsigned block=0;block<8;block++){
            unsigned base=lead+ns*block;
            for(unsigned i=0;i<2u*ns;i++){
                float w=i<ns ?
#ifdef SAUTI_AAC_DIAG_SHORT_FIRST_CURRENT
                    short_curr[i] :
#else
                    (block==0u?short_prev[i]:short_curr[i]) :
#endif
                    short_curr[i];
                frame[base+i]+=imdct[block*2u*ns+i]*w;
            }
        }
    }
    for(unsigned i=0;i<2u*n;i++)if(!isfinite(frame[i]))return 0;
    float ones[2048],history[1024],result[1024];
    for(unsigned i=0;i<2u*n;i++)ones[i]=1.0f;
    memcpy(history,previous_overlap,n*sizeof(float));
    if(!sauti_aac_window_overlap_asm(frame,ones,history,result,n))return 0;
    for(unsigned i=0;i<n;i++)
        if(!isfinite(history[i]) || !isfinite(result[i]))return 0;
    memcpy(pcm,result,n*sizeof(float));
    memcpy(next_overlap,history,n*sizeof(float));
    return 1;
}
int sauti_aac_window_synth(unsigned sequence,unsigned previous_shape,
                           unsigned current_shape,const float *imdct,
                           const float previous_overlap[1024],
                           float pcm[1024],float next_overlap[1024]){
    return sauti_aac_window_synth_n(1024u,sequence,previous_shape,current_shape,imdct,previous_overlap,pcm,next_overlap);
}
