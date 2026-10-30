#include "sauti.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(offsetof(SautiAudioRing, write_index)==0, "ASM ring write");
_Static_assert(offsetof(SautiAudioRing, read_index)==4, "ASM ring read");
_Static_assert(offsetof(SautiAudioRing, samples)==8, "ASM ring samples");
_Static_assert(sizeof(SautiAudioRing)==8+SAUTI_RING_CAPACITY*sizeof(float), "ASM ring bytes");
static void ck(int valid,const char *name) {
    if(!valid){fprintf(stderr,"FAIL: %s\n",name);exit(1);}
}
static int eq(float x, float y) {return fabsf(x-y)<0.0001f;}
int main(void) {
    SautiAudioRing *ring = calloc(1,sizeof(*ring));
    float *stereo = calloc(SAUTI_RING_CAPACITY*2+2,sizeof(float));
    float *window = calloc(SAUTI_FFT_SAMPLES+2,sizeof(float));
    ck(ring && stereo && window,"alloc");
    window[0]=-99.f;
    window[SAUTI_FFT_SAMPLES+1]=99.f;
    float *samples = window+1;
    ck(sauti_ring_drain_window_f32(ring,samples,SAUTI_FFT_SAMPLES)==0,"empty ring");
    ck(sauti_ring_push_stereo_f32(ring,NULL,5)==0,"null stereo");
    ck(sauti_ring_push_stereo_f32(NULL,stereo,5)==0,"null ring");
    ck(sauti_ring_drain_window_f32(ring,NULL,5)==0,"null window");

    for(unsigned i=0;i<512;i++){stereo[i*2]=0.5f;stereo[i*2+1]=0.1f;}
    ck(sauti_ring_push_stereo_f32(ring,stereo,512)==512,"push 512");
    ck(sauti_ring_drain_window_f32(ring,samples,SAUTI_FFT_SAMPLES)==512,"drain 512");
    ck(eq(samples[0],0),"preserve zero history");
    ck(eq(samples[SAUTI_FFT_SAMPLES-513],0),"first buffer still zero");
    ck(eq(samples[SAUTI_FFT_SAMPLES-512],0.3f),"first averaged frame");
    ck(eq(samples[SAUTI_FFT_SAMPLES-1],0.3f),"last averaged frame");
    ck(sauti_ring_drain_window_f32(ring,samples,SAUTI_FFT_SAMPLES)==0,"no duplicate");
    ck(eq(samples[SAUTI_FFT_SAMPLES-1],0.3f),"history unchanged when empty");

    for(unsigned i=0;i<SAUTI_RING_CAPACITY;i++) {
        stereo[i*2]=(float)(i%1000)/1000.0f;
        stereo[i*2+1]=(float)(i%1000)/1000.0f;
    }
    ck(sauti_ring_push_stereo_f32(ring,stereo,SAUTI_RING_CAPACITY)==SAUTI_RING_CAPACITY,
       "full enqueue");
    ck(sauti_ring_push_stereo_f32(ring,stereo,1)==0,"full drops newest");
    ck(sauti_ring_drain_window_f32(ring,samples,SAUTI_FFT_SAMPLES)==SAUTI_FFT_SAMPLES,
       "drain newest subset");
    ck(eq(samples[0], (float)((SAUTI_RING_CAPACITY-SAUTI_FFT_SAMPLES)%1000)/1000.0f),
       "skips stale backlog");
    ck(eq(samples[SAUTI_FFT_SAMPLES-1], (float)((SAUTI_RING_CAPACITY-1)%1000)/1000.0f),
       "retains latest accepted frame");
    ck(ring->write_index==ring->read_index,"consumed backlog");

    /* Force modulo rollover by choosing empty aligned monotonic indices. */
    ring->write_index = UINT32_MAX-127u;
    ring->read_index = UINT32_MAX-127u;
    for(unsigned i=0;i<256;i++){stereo[i*2]=stereo[i*2+1]=0.5f;}
    ck(sauti_ring_push_stereo_f32(ring,stereo,256)==256,"push counter rollover");
    ck(sauti_ring_drain_window_f32(ring,samples,256)==256,"drain counter rollover");
    ck(ring->write_index==ring->read_index,"indices modulo32 correct");
    ck(eq(samples[0],0.5f)&&eq(samples[255],0.5f),"wrap content correct");

    stereo[0]=NAN;stereo[1]=0;
    stereo[2]=INFINITY;stereo[3]=INFINITY;
    stereo[4]=-100;stereo[5]=-100;
    ck(sauti_ring_push_stereo_f32(ring,stereo,3)==3,"bad sample enqueue");
    ck(sauti_ring_drain_window_f32(ring,samples,3)==3,"bad sample dequeue");
    ck(samples[0]==0 && samples[1]==1 && samples[2]==-1,"clamp/NaN policy");

    ck(window[0]==-99.f && window[SAUTI_FFT_SAMPLES+1]==99.f,"window guards");
    /* Live mode must not generate new tones; it reads caller's sample window. */
    enum {W=320,H=240};
    uint32_t *pixels = calloc(W*H+2,sizeof(uint32_t));
    SautiFrameScratch *frame = calloc(1,sizeof(*frame));
    ck(pixels&&frame,"frame allocation");
    pixels[0]=0xcafebabe;pixels[W*H+1]=0xdeadbeef;
    for(unsigned i=0;i<SAUTI_FFT_SAMPLES;i++)
        frame->samples[i] = sinf((float)(6.283185307179586*64.0*i/SAUTI_FFT_SAMPLES));
    float first=frame->samples[5];
    ck(sauti_render_audio_frame(pixels+1,W,H,frame)==0,"external audio FFT frame");
    ck(eq(frame->samples[5],first),"audio frame preserves source samples");
    ck(frame->phase_a==0 && frame->phase_b==0,"live mode never calls tone generator");
    ck(pixels[0]==0xcafebabe&&pixels[W*H+1]==0xdeadbeef,"renderer guards");
    unsigned nonzero=0;
    for(unsigned i=1;i<=W*H;i++)
        nonzero+=pixels[i]==0xff31d7b8u || pixels[i]==0xffd66b75u;
    ck(nonzero>0,"live spectrum has bars");

    free(frame);free(pixels);free(window);free(stereo);free(ring);
    puts("PASS: SPSC ring capacity, wrap, newest-frame policy, mono conversion, bad samples, live ASM renderer");
    return 0;
}
