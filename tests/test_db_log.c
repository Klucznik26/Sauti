#include "sauti.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void ck(int yes, const char *where) {
    if (!yes) { fprintf(stderr,"FAIL: %s\n",where);exit(1); }
}
static int near(float x,float y,float eps) {return fabsf(x-y)<eps;}
int main(void) {
    float src[] = {1.0f, 0.5f, 0.1f, 0.01f, 0.001f,
        0.0001f, 0.00003162277f, 0, -1, NAN, INFINITY, 2.0f};
    float out[14]; out[0]=-55;out[13]=-66;
    ck(sauti_db_normalize_f32(src,12,out+1)==0,"db accepted");
    ck(out[0]==-55&&out[13]==-66,"db guards");
    ck(near(out[1],1,0.001),"0 dB");
    ck(near(out[2],1-6.0206f/90,0.002),"-6dB");
    ck(near(out[3],1-20.0f/90,0.002),"-20dB");
    ck(near(out[4],1-40.0f/90,0.002),"-40dB");
    ck(near(out[6],1-80.0f/90,0.002),"-80dB");
    ck(out[7]>=0&&out[7]<0.002,"-90dB floor");
    ck(out[8]==0 && out[9]==0 && out[10]==0,"zero negative NaN");
    ck(out[11]==1 && out[12]==1,"positive Inf and >1");
    ck(sauti_db_normalize_f32(NULL,5,out)==-1,"null db");
    ck(sauti_db_normalize_f32(src,0,out)==-1,"zero db count");

    enum { BIN_COUNT=2049, BAR_COUNT=256 };
    float *bins=calloc(BIN_COUNT,sizeof(float));
    float *bars=calloc(BAR_COUNT+2,sizeof(float));
    ck(bins&&bars,"alloc bands");
    bins[1]=0.15f; bins[16]=0.75f; bins[1024]=0.8f; bins[2048]=0.6f;
    bins[0]=1.0f; /* loud DC must not appear anywhere */
    bars[0]=-11;bars[BAR_COUNT+1]=-22;
    ck(sauti_log_bands_f32(bins,BIN_COUNT,bars+1,BAR_COUNT)==0,"log bins");
    ck(bars[0]==-11 && bars[BAR_COUNT+1]==-22,"log guards");
    ck(near(bars[1],0.15f,0.001),"1 Hz bin mapped to first");
    ck(near(bars[256],0.6f,0.001),"highest FFT bin mapped to last");
    ck(near(bars[232],0.8f,0.001),"1024 bin at 10/11 logarithmic extent");
    ck(sauti_log_bands_f32(bins,1,bars,BAR_COUNT)==-1,"bad bin count");
    ck(sauti_log_bands_f32(bins,2,bars,BAR_COUNT)==-1,"n=2 invalid log denominator");
    ck(sauti_log_bands_f32(bins,BIN_COUNT,bars,1)==-1,"bad band count");
    ck(sauti_log_bands_f32(NULL,BIN_COUNT,bars,BAR_COUNT)==-1,"null source");

    float history[BAR_COUNT]={0},target[BAR_COUNT]={0};
    target[0]=1.0f;
    ck(sauti_smooth_bands_f32(target,history,BAR_COUNT,0.75f,0.10f)==0,"rise");
    ck(near(history[0],0.75f,0.001),"75% attack");
    ck(sauti_smooth_bands_f32(target,history,BAR_COUNT,0.75f,0.10f)==0,"more rise");
    ck(near(history[0],0.9375f,0.001),"attack second step");
    target[0]=0.0f;
    ck(sauti_smooth_bands_f32(target,history,BAR_COUNT,0.75f,0.10f)==0,"release");
    ck(near(history[0],0.84375f,0.001),"10% release");
    ck(sauti_smooth_bands_f32(target,history,BAR_COUNT,NAN,0.1f)==-1,"reject NaN rate");
    ck(sauti_smooth_bands_f32(target,history,BAR_COUNT,0.3f,1.5f)==-1,"reject >1 rate");
    ck(sauti_smooth_bands_f32(target,NULL,BAR_COUNT,0.3f,0.1f)==-1,"null history");
    free(bins); free(bars);
    puts("PASS: dBFS / logarithmic bins / attack-release / invalid inputs / canaries");
    return 0;
}
