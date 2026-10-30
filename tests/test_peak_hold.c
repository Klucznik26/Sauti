#include "sauti.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    SautiPeakHold peak = {0};
    float left[SAUTI_LOG_BANDS] = {0}, right[SAUTI_LOG_BANDS] = {0};
    left[0] = 1.0f; right[21] = 0.5f;
    sauti_peak_hold_update(&peak,left,right,1000);
    assert(peak.level[0] == 20);
    assert(peak.level[13] == 10);
    assert(peak.level[12] == 0);
    left[0] = 0.0f; right[21] = 0.0f;
    sauti_peak_hold_update(&peak,left,right,1299);
    assert(peak.level[0] == 20 && peak.level[13] == 10);
    sauti_peak_hold_update(&peak,left,right,1380);
    assert(peak.level[0] == 19 && peak.level[13] == 9);
    sauti_peak_hold_update(&peak,left,right,1540);
    assert(peak.level[0] == 17 && peak.level[13] == 7);
    sauti_peak_hold_update(&peak,left,right,10000);
    assert(peak.level[0] == 0 && peak.level[13] == 0);
    left[0] = 0.55f;
    sauti_peak_hold_update(&peak,left,right,10100);
    assert(peak.level[0] == 11);
    uint32_t *pixels = calloc(480u*240u+16u,sizeof(uint32_t));
    assert(pixels);
    sauti_render_stereo_bars_argb32(pixels,480,240,left,right,100);
    for(unsigned i=480u*240u;i<480u*240u+16u;++i) pixels[i]=0xabcdef01u;
    sauti_draw_peak_hold_argb32(pixels,480,240,&peak);
    for(unsigned i=480u*240u;i<480u*240u+16u;++i) assert(pixels[i]==0xabcdef01u);
    /* Regression: peak marker x-coordinate must remain in its own channel.
     * Previously r9 was overwritten by vertical geometry, producing an
     * unrelated bright column at the far left of the window. */
    for(unsigned i=0;i<480u*240u;++i) pixels[i]=0;
    peak.level[0]=11;
    peak.level[13]=10;
    sauti_draw_peak_hold_argb32(pixels,480,240,&peak);
    unsigned left_markers=0,right_markers=0;
    for(unsigned y=0;y<240;++y) for(unsigned x=0;x<480;++x){
        uint32_t color=pixels[(size_t)y*480+x];
        if(!color)continue;
        assert(x>=110 && x<400);
        if(x<240) ++left_markers;
        else ++right_markers;
    }
    assert(left_markers>0 && right_markers>0);
    free(pixels);
    puts("sauti peak hold tests passed");
    return 0;
}
