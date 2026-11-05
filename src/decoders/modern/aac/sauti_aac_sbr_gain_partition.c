/* SBR-54j: source-verified pre-limiter SBR power allocation.
 * FFmpeg aacsbr.c:sbr_gain_calc() documents the corresponding
 * ISO/IEC MPEG-4 SBR relationships; our function uses equivalent
 * double-precision arithmetic and checks finite outputs.
 * Not yet connected to Sauti's PCM / SBR-53d limiter. */
#include "sauti_aac_sbr_gain_partition.h"
#include <math.h>

int sauti_aac_sbr_gain_partition(
    double e,double ec,double q,
    unsigned harmonic,unsigned delta,
    SautiAacSbrGainPartition *out){
    if(!out || !isfinite(e) || !isfinite(ec) || !isfinite(q) ||
       e<0.0 || ec<0.0 || q<0.0 || harmonic>1u || delta>1u)
        return 0;
    const double denominator=1.0+q;
    const double ec_denom=1.0+ec;
    const double delta_denom=1.0+q*(double)delta;
    if(!isfinite(denominator) || !isfinite(ec_denom) ||
       !isfinite(delta_denom) || denominator<=0.0 ||
       ec_denom<=0.0 || delta_denom<=0.0)return 0;

    const double n2=(e/denominator)*q;
    const double s2=harmonic?(e/denominator):0.0;
    /* Divide before multiplying to avoid overflow when E and Q
     * are individually valid but their product would overflow. */
    const double g2=harmonic?
        ((e/ec_denom)/denominator)*q:
        (e/ec_denom)/delta_denom;
    if(!isfinite(n2)||!isfinite(s2)||!isfinite(g2)||
       n2<0.0||s2<0.0||g2<0.0)return 0;
    const SautiAacSbrGainPartition value={
        sqrt(g2),sqrt(n2),sqrt(s2)
    };
    if(!isfinite(value.hf_gain) ||
       !isfinite(value.noise_amplitude) ||
       !isfinite(value.sine_amplitude))
        return 0;
    *out=value;
    return 1;
}
