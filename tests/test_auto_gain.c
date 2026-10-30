#include "sauti.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void ck(int yes,const char *name) {
    if(!yes) { fprintf(stderr,"FAIL: %s\n",name);exit(1); }
}
static unsigned waveform_span(const uint32_t *argb,unsigned w,unsigned h) {
    unsigned lo=h,hi=0;
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)
        if(argb[(size_t)y*w+x]==0xff32e6c2u) {
            if(y<lo)lo=y;
            if(y>hi)hi=y;
        }
    return lo<=hi?hi-lo:0;
}
int main(void) {
    enum {W=256,H=128,N=4096};
    float *signal=calloc(N+2,sizeof(float));
    uint32_t *plain=calloc(W*H+2,sizeof(uint32_t));
    uint32_t *scaled=calloc(W*H+2,sizeof(uint32_t));
    SautiFrameScratch *frame=calloc(1,sizeof(*frame));
    ck(signal&&plain&&scaled&&frame,"alloc");
    for(unsigned i=0;i<N;i++) signal[i+1]=((i/32u)&1u)?0.025f:-0.025f;
    signal[0]=-123.0f; signal[N+1]=456.0f;
    plain[0]=0x12345678u; plain[W*H+1]=0x87654321u;
    scaled[0]=0x11111111u; scaled[W*H+1]=0x22222222u;
    sauti_render_wave_argb32(plain+1,W,H,signal+1,N);
    sauti_render_wave_scaled_argb32(scaled+1,W,H,signal+1,N,64.0f);
    unsigned a=waveform_span(plain+1,W,H);
    unsigned b=waveform_span(scaled+1,W,H);
    ck(a<=8,"fixed gain line is appropriately flat for weak signal");
    ck(b>=90,"scaled weak signal is visible across the panel");
    ck(signal[0]==-123.0f&&signal[N+1]==456.0f,"samples unchanged by magnification");
    ck(plain[0]==0x12345678u&&plain[W*H+1]==0x87654321u,"plain guards");
    ck(scaled[0]==0x11111111u&&scaled[W*H+1]==0x22222222u,"scaled guards");

    /* The actual live frame auto-gains without writing the source signal. */
    for(unsigned i=0;i<N;i++) frame->samples[i]=signal[i+1];
    float before=frame->samples[9];
    uint32_t *full=calloc(W*(2*H)+2,sizeof(uint32_t));
    ck(full!=NULL,"full frame allocation");
    full[0]=0xabbababa; full[W*(2*H)+1]=0xbabacafe;
    ck(sauti_render_audio_frame(full+1,W,H*2,frame)==0,"live low-level audio");
    ck(frame->samples[9]==before,"auto-gain never modifies live audio");
    ck(waveform_span(full+1,W,H)>=80,"live oscilloscope auto gain");
    ck(full[1+10*W+44]==0xffd66b75u,"peak P meter ruby");
    ck(full[1+27*W+44]==0xff31d7b8u,"RMS R meter emerald");
    ck(full[1+10*W+219]==0xff253544u,"peak meter remaining background");
    ck(full[0]==0xabbababa&&full[W*(2*H)+1]==0xbabacafe,"live frame guards");

    float db[2]={0.5f,0.25f};
    uint32_t old=full[1+10*W+44];
    sauti_draw_levels_argb32(NULL,W,H,db);
    sauti_draw_levels_argb32(full+1,100,H,db);
    ck(full[1+10*W+44]==old,"invalid dimensions untouched");
    free(full);free(frame);free(scaled);free(plain);free(signal);
    puts("PASS: low-volume auto-gain, untouched samples, Peak/RMS overlay, borders");
    return 0;
}
