/* AAC-18: intentionally separate performance check (not PASS/FAIL).
 * Benchmarks the optimized native FFT IMDCT against a real-time
 * frame budget. It is a diagnostic, not a machine-independent gate. */
#define _POSIX_C_SOURCE 200809L
#include "sauti_aac_imdct.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

static double now(void){
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC,&ts)!=0)return -1.0;
    return (double)ts.tv_sec+1.0e-9*(double)ts.tv_nsec;
}
int main(void){
    float spectral[1024],out[2048];
    const unsigned sizes[]={128,1024};
    for(unsigned j=0;j<2;j++){
        unsigned n=sizes[j],iterations=n==128?40u:5u;
        for(unsigned k=0;k<n;k++)spectral[k]=(float)((int)(k%17)-8)/32.0f;
        double start=now();
        if(start<0)return 2;
        volatile float checksum=0;
        for(unsigned it=0;it<iterations;it++){
            if(!sauti_aac_imdct(spectral,n,out))return 1;
            checksum+=out[it%(2*n)];
        }
        double elapsed=now()-start;
        double per_block=1000.0*elapsed/(double)iterations;
        double realtime=1000.0*(double)n/44100.0;
        printf("AAC-18 IMDCT N=%u: %.3f ms/block (budget %.3f ms at 44.1 kHz), cost/budget=%.2fx checksum=%f\n",
               n,per_block,realtime,per_block/realtime,(double)checksum);
    }
    return 0;
}
