/* Unwindowed AAC IMDCT: a clear baseline implementation.
 * C prepares trigonometric rows; independent NASM SSE2 kernel performs
 * every spectral dot product. Optimized FFT-based IMDCT may replace the
 * O(N^2) cosine construction after reference correctness is established. */
#include "sauti_aac_imdct.h"
#include <math.h>
#include <string.h>

int sauti_aac_imdct(const float *spectral,size_t n,float *time_samples){
    if(!spectral || !time_samples || (n!=128u && n!=1024u))return 0;
    for(size_t k=0;k<n;k++)if(!isfinite(spectral[k]))return 0;
    const double pi=3.14159265358979323846264338327950288;
    const double normalized=2.0/(double)n;
    float basis[1024];
    float next[2048];
    for(size_t t=0;t<2u*n;t++){
        const double phase=(pi/(double)n) *
                           ((double)t+0.5+(double)n/2.0);
        /* Cosine recurrence: two libm calls per row, instead of n.
         * Keep double phase accumulation; the NASM SSE2 dot product and
         * explicit 2/n scaling remain unchanged. This is still O(n^2). */
        const double twice_cos=2.0*cos(phase);
        double previous=cos(0.5*phase);
        double current=previous;
        for(size_t k=0;k<n;k++){
            basis[k]=(float)current;
            double following=twice_cos*current-previous;
            previous=current;
            current=following;
        }
        const double sum=sauti_aac_imdct_dot_asm(spectral,basis,n);
        const double value=normalized*sum;
        if(!isfinite(value) || fabs(value) > 3.402823466e38)return 0;
        next[t]=(float)value;
    }
    memcpy(time_samples,next,2u*n*sizeof(float));
    return 1;
}
