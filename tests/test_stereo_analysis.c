#include "sauti.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static float max_band(const SautiFrameScratch *s) {
    float m = 0.0f;
    for (unsigned i=0;i<SAUTI_LOG_BANDS;i++)
        if (s->smooth_bands[i] > m) m=s->smooth_bands[i];
    return m;
}
int main(void) {
    float *l=calloc(SAUTI_FFT_SAMPLES,sizeof(float));
    float *r=calloc(SAUTI_FFT_SAMPLES,sizeof(float));
    SautiFrameScratch *ls=calloc(1,sizeof(*ls)), *rs=calloc(1,sizeof(*rs));
    assert(l && r && ls && rs);
    for (unsigned i=0;i<SAUTI_FFT_SAMPLES;i++)
        l[i]=0.7f*sinf(6.283185307179586f*32.0f*(float)i/4096.0f);
    assert(sauti_analyze_stereo_bands_f32(l,r,ls,rs)==0);
    assert(max_band(ls)>0.05f);
    assert(max_band(rs)==0.0f);
    for (unsigned i=0;i<SAUTI_FFT_SAMPLES;i++) r[i]=-l[i];
    assert(sauti_analyze_stereo_bands_f32(l,r,ls,rs)==0);
    assert(max_band(ls)>0.05f && max_band(rs)>0.05f);
    assert(sauti_analyze_stereo_bands_f32(NULL,r,ls,rs)==-1);
    free(l);free(r);free(ls);free(rs);
    puts("sauti true-stereo FFT tests passed");
    return 0;
}
