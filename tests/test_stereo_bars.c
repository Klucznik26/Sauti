#include "sauti.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    const uint32_t w = 480, h = 240;
    uint32_t *pixels = calloc((size_t)w * h + 16, sizeof(uint32_t));
    assert(pixels);
    for (int i=0;i<16;i++) pixels[(size_t)w*h+i] = 0xABCDEF01u;
    float l[SAUTI_LOG_BANDS]={0}, r[SAUTI_LOG_BANDS]={0};
    sauti_render_stereo_bars_argb32(pixels,w,h,l,r,0);
    assert(pixels[0] == 0xFF101016u);
    /* Central 12px gutter should remain background. */
    for (unsigned y=0;y<h;y++)
        for (unsigned x=w/2-6;x<w/2+6;x++)
            assert(pixels[(size_t)y*w+x] == 0xFF101016u);
    for (unsigned i=0;i<16;i++) assert(pixels[(size_t)w*h+i] == 0xABCDEF01u);
    l[10]=1.0f;
    sauti_render_stereo_bars_argb32(pixels,w,h,l,r,100);
    int lit_l=0,lit_r=0;
    for(unsigned y=0;y<h;y++)
        for(unsigned x=0;x<w;x++) {
            if (pixels[(size_t)y*w+x] == 0xFFFFBF40u ||
                pixels[(size_t)y*w+x] == 0xFF20C997u ||
                pixels[(size_t)y*w+x] == 0xFF327DF4u ||
                pixels[(size_t)y*w+x] == 0xFFE73D65u) {
                if (x<w/2) lit_l++;
                else lit_r++;
            }
        }
    assert(lit_l>0 && lit_r==0);
    /* Half-size layout should leave broad blank side margins. */
    for (unsigned y=50;y<h-40;y++) {
        for (unsigned x=0;x<25;x++)
            assert(pixels[(size_t)y*w+x] == 0xFF101016u);
        for (unsigned x=w-25;x<w;x++)
            assert(pixels[(size_t)y*w+x] == 0xFF101016u);
    }
    /* A halo must contain blended colors, not only opaque core colors.
     * Check the LEFT half, while the RIGHT channel is silent. */
    int blended=0;
    for (unsigned y=0;y<h;y++)
        for (unsigned x=0;x<w/2-6;x++) {
            uint32_t c=pixels[(size_t)y*w+x];
            if (c!=0xFF101016u && c!=0xFF272331u &&
                c!=0xFFFFBF40u && c!=0xFF20C997u &&
                c!=0xFF327DF4u && c!=0xFFE73D65u &&
                c!=0xFFFFCF70u && c!=0xFF7CFFC0u) blended++;
        }
    assert(blended>0);
    /* The core is stable but the neighboring neon halo changes with time. */
    uint32_t *first = malloc((size_t)w*h*sizeof(uint32_t));
    assert(first);
    memcpy(first,pixels,(size_t)w*h*sizeof(uint32_t));
    sauti_render_stereo_bars_argb32(pixels,w,h,l,r,256);
    int pulse_changes=0;
    for (unsigned y=0;y<h;y++)
        for (unsigned x=0;x<w/2;x++)
            if (first[(size_t)y*w+x] != pixels[(size_t)y*w+x])
                pulse_changes++;
    assert(pulse_changes>0);
    for (unsigned y=0;y<h;y++)
        for (unsigned x=w/2-6;x<w/2+6;x++)
            assert(pixels[(size_t)y*w+x] == 0xFF101016u);
    /* Every bin in a 21-bin source slice must contribute to its column. */
    memset(l,0,sizeof(l));
    l[20]=1.0f;       /* edge of first logarithmic slice */
    sauti_render_stereo_bars_argb32(pixels,w,h,l,r,0);
    int visible=0;
    for (unsigned y=0;y<h;y++)
        for (unsigned x=0;x<w/2;x++)
            {
                uint32_t color=pixels[(size_t)y*w+x];
                unsigned red=(color>>16)&255u;
                unsigned green=(color>>8)&255u;
                unsigned blue=color&255u;
                /* Neon halo is alpha-blended over previously painted LEDs.
                 * Exact 0xFFFFBF40 is no longer guaranteed after compositing.
                 * Check that the first slice still produces amber light. */
                if (red>=155u && green>=85u && blue<135u &&
                    red>green && green>blue)
                    ++visible;
            }
    assert(visible>0);
    free(first);
    free(pixels);
    puts("sauti stereo bars tests passed");
    return 0;
}
