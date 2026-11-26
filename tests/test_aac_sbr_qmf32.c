#include "sauti_aac_sbr_qmf_analysis.h"
#include "sauti_aac_sbr_qmf_synth_mod.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* 32-band synthesis for SBR at the core rate: the pair (analysis with the phase offset 1, sauti_aac_sbr_qmf_synth_32) reconstructs the signal (59 dB for a noise-like input); with the offset 1/2 of the dual-rate bank it does not (17 dB) */
static double chain_32_snr(int downsampled,double *gain,int *delay){
    enum { SL=32, BLOCKS=8, TOT=SL*32*BLOCKS };
    static float in[TOT],out[TOT];static SautiAacSbrComplex low[SL*32],grid[SL*64];
    SautiAacSbrQmfAnalysisState as;SautiAacSbrQmfSynthWindowState ss;
    memset(&as,0,sizeof as);memset(&ss,0,sizeof ss);
    unsigned seed=12345u;
    for(unsigned i=0;i<TOT;i++){seed=seed*1664525u+1013904223u;in[i]=(float)(0.3*sin(0.05*i)+0.2*sin(0.9*i+1.0)+0.1*sin(2.0*i)+0.05*((double)(seed>>8)/16777216.0-0.5));}
    for(unsigned b=0;b<BLOCKS;b++){
        memset(grid,0,sizeof grid);
        const int ok=(downsampled?sauti_aac_sbr_qmf_analyze_32_downsampled:sauti_aac_sbr_qmf_analyze_32)(in+b*SL*32,SL*32,SL,32u,low,SL*32,&as);
        assert(ok);
        for(unsigned t=0;t<SL;t++)memcpy(grid+t*64,low+t*32,32u*sizeof(SautiAacSbrComplex));
        assert(sauti_aac_sbr_qmf_synth_32(grid,64u,SL,out+b*SL*32,&ss));
    }
    double best=-1e9;
    for(int d=200;d<400;d++){
        double xy=0,xx=0,yy=0;
        for(int i=3000;i<6000;i++){const double x=in[i-d],y=out[i];xy+=x*y;xx+=x*x;yy+=y*y;}
        const double g=xy/xx,e=yy-2*g*xy+g*g*xx,snr=10.0*log10(yy/(e+1e-30));
        if(snr>best){best=snr;*gain=g;*delay=d;}
    }
    return best;
}
static void test_synth_32_downsampled(void){
    double g=0,g0=0;int d=0,d0=0;
    const double ds=chain_32_snr(1,&g,&d),std=chain_32_snr(0,&g0,&d0);
    assert(ds>55.0 && fabs(g-1.0)<0.01 && d==289);
    assert(std<30.0);
    printf("SBR-26: 32-band synthesis, downsampled pair %.1f dB (delay %d, gain %.5f), dual-rate analysis %.1f dB PASS\n",ds,d,g,std);
}
int main(void){
    test_synth_32_downsampled();
    puts("PASS HE-AAC SBR-26 32-band QMF synthesis for SBR at the core rate");
    return 0;
}
