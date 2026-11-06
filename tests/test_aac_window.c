#include "sauti_aac_window.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int near(float a,float b){
    return fabsf(a-b)<0.0002f;
}
static void windows(void){
    float w[2048],saved[2048];
    for(unsigned n=128;n<=1024;n*=8){
        for(unsigned shape=0;shape<2;shape++){
            assert(sauti_aac_window_generate(n,shape,w));
            assert(w[0]>0 && w[0]<1 && w[n-1]>0 && w[n-1]<=1);
            for(unsigned i=0;i<n;i++){
                assert(near(w[i],w[2*n-1-i]));
                assert(near(w[i]*w[i]+w[n+i]*w[n+i],1));
                if(i)assert(w[i]>=w[i-1]);
            }
        }
    }
    memcpy(saved,w,sizeof(saved));
    assert(!sauti_aac_window_generate(129,0,w));
    assert(!sauti_aac_window_generate(128,2,w));
    assert(!sauti_aac_window_generate(128,0,NULL));
    assert(memcmp(saved,w,sizeof(w))==0);
}
static void sequences(void){
    float in[2048],pcm[1024],hist[1024],next[1024];
    float lp[2048],lc[2048],sp[256],sc[256];
    assert(sauti_aac_window_generate(1024,0,lp));
    assert(sauti_aac_window_generate(1024,1,lc));
    assert(sauti_aac_window_generate(128,0,sp));
    assert(sauti_aac_window_generate(128,1,sc));
    for(unsigned i=0;i<2048;i++)in[i]=1.0f;
    for(unsigned i=0;i<1024;i++)hist[i]=0.25f;
    for(unsigned seq=0;seq<4;seq++){
        assert(sauti_aac_window_synth(seq,0,1,in,hist,pcm,next));
        for(unsigned i=0;i<1024;i++){
            float left=0,right=0;
            if(seq==0){left=lp[i];right=lc[1024+i];}
            if(seq==1){
                left=lp[i];
                right=i<448?1.0f:i<576?sc[128+i-448]:0.0f;
            }
            if(seq==3){
                left=i<448?0.0f:i<576?sp[i-448]:1.0f;
                right=lc[1024+i];
            }
            if(seq==2){
                /* Eight staggered 256-sample windows start at 448+128*b. */
                for(unsigned b=0;b<8;b++){
                    unsigned start=448+128*b;
                    if(i>=start && i<start+256)
                        left+=(i-start<128 ?
                              (b==0?sp[i-start]:sc[i-start]):
                               sc[i-start]);
                    unsigned t=i+1024;
                    if(t>=start && t<start+256)
                        right+=(t-start<128 ?
                               (b==0?sp[t-start]:sc[t-start]):
                                sc[t-start]);
                }
            }
            assert(near(pcm[i],0.25f+left));
            assert(near(next[i],right));
        }
    }
    float previous[1024],previous_next[1024];
    memcpy(previous,pcm,sizeof(previous));
    memcpy(previous_next,next,sizeof(previous_next));
    in[100]=NAN;
    assert(!sauti_aac_window_synth(2,0,1,in,hist,pcm,next));
    assert(memcmp(pcm,previous,sizeof(previous))==0);
    assert(memcmp(next,previous_next,sizeof(previous_next))==0);
    in[100]=1.0f;hist[0]=INFINITY;
    assert(!sauti_aac_window_synth(0,0,1,in,hist,pcm,next));
    hist[0]=0.25f;
    assert(!sauti_aac_window_synth(4,0,1,in,hist,pcm,next));
    assert(!sauti_aac_window_synth(0,2,1,in,hist,pcm,next));
    assert(!sauti_aac_window_synth(0,0,2,in,hist,pcm,next));
    assert(!sauti_aac_window_synth(0,0,1,NULL,hist,pcm,next));
    assert(memcmp(pcm,previous,sizeof(previous))==0);
    assert(memcmp(next,previous_next,sizeof(previous_next))==0);
}
int main(void){
    windows();sequences();
    puts("PASS AAC window synthesis: SIN/KBD, four sequences, short alignment, NASM overlap");
    return 0;
}
