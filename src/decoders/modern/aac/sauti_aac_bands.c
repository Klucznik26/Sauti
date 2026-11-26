/* AAC scalefactor-band offsets for 1024 and 128 spectral lines.
 * Numeric tables validated against FFmpeg libavcodec/aactab.c
 * (LGPL-2.1-or-later); no FFmpeg linkage or runtime dependency.
 * Licensing of bundled numeric data needs review before distribution. */
#include "sauti_aac_ssr.h"
#include "sauti_aac_bands.h"
#include "sauti_aac_layout.h"
#include <stddef.h>
static const uint16_t swb_offset_1024_96[42] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 
    48, 52, 56, 64, 72, 80, 88, 96, 108, 120, 132, 144, 
    156, 172, 188, 212, 240, 276, 320, 384, 448, 512, 576, 640, 
    704, 768, 832, 896, 960, 1024, 
};
static const uint16_t swb_offset_128_96[13] = {
    0, 4, 8, 12, 16, 20, 24, 32, 40, 48, 64, 92, 
    128, 
};
static const uint16_t swb_offset_1024_64[48] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 
    48, 52, 56, 64, 72, 80, 88, 100, 112, 124, 140, 156, 
    172, 192, 216, 240, 268, 304, 344, 384, 424, 464, 504, 544, 
    584, 624, 664, 704, 744, 784, 824, 864, 904, 944, 984, 1024, 

};
static const uint16_t swb_offset_1024_48[50] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 48, 
    56, 64, 72, 80, 88, 96, 108, 120, 132, 144, 160, 176, 
    196, 216, 240, 264, 292, 320, 352, 384, 416, 448, 480, 512, 
    544, 576, 608, 640, 672, 704, 736, 768, 800, 832, 864, 896, 
    928, 1024, 
};
static const uint16_t swb_offset_128_48[15] = {
    0, 4, 8, 12, 16, 20, 28, 36, 44, 56, 68, 80, 
    96, 112, 128, 
};
static const uint16_t swb_offset_1024_32[52] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 48, 
    56, 64, 72, 80, 88, 96, 108, 120, 132, 144, 160, 176, 
    196, 216, 240, 264, 292, 320, 352, 384, 416, 448, 480, 512, 
    544, 576, 608, 640, 672, 704, 736, 768, 800, 832, 864, 896, 
    928, 960, 992, 1024, 
};
static const uint16_t swb_offset_1024_24[48] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 
    52, 60, 68, 76, 84, 92, 100, 108, 116, 124, 136, 148, 
    160, 172, 188, 204, 220, 240, 260, 284, 308, 336, 364, 396, 
    432, 468, 508, 552, 600, 652, 704, 768, 832, 896, 960, 1024, 

};
static const uint16_t swb_offset_128_24[16] = {
    0, 4, 8, 12, 16, 20, 24, 28, 36, 44, 52, 64, 
    76, 92, 108, 128, 
};
static const uint16_t swb_offset_1024_16[44] = {
    0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 
    100, 112, 124, 136, 148, 160, 172, 184, 196, 212, 228, 244, 
    260, 280, 300, 320, 344, 368, 396, 424, 456, 492, 532, 572, 
    616, 664, 716, 772, 832, 896, 960, 1024, 
};
static const uint16_t swb_offset_128_16[16] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 40, 48, 60, 
    72, 88, 108, 128, 
};
static const uint16_t swb_offset_1024_8[41] = {
    0, 12, 24, 36, 48, 60, 72, 84, 96, 108, 120, 132, 
    144, 156, 172, 188, 204, 220, 236, 252, 268, 288, 308, 328, 
    348, 372, 396, 420, 448, 476, 508, 544, 580, 620, 664, 712, 
    764, 820, 880, 944, 1024, 
};
static const uint16_t swb_offset_128_8[16] = {
    0, 4, 8, 12, 16, 20, 24, 28, 36, 44, 52, 60, 
    72, 88, 108, 128, 
};
static const uint16_t *const long_offsets[13]={
    swb_offset_1024_96,
    swb_offset_1024_96,
    swb_offset_1024_64,
    swb_offset_1024_48,
    swb_offset_1024_48,
    swb_offset_1024_32,
    swb_offset_1024_24,
    swb_offset_1024_24,
    swb_offset_1024_16,
    swb_offset_1024_16,
    swb_offset_1024_16,
    swb_offset_1024_8,
    swb_offset_1024_8,
};
static const uint16_t *const short_offsets[13]={
    swb_offset_128_96,
    swb_offset_128_96,
    swb_offset_128_96,
    swb_offset_128_48,
    swb_offset_128_48,
    swb_offset_128_48,
    swb_offset_128_24,
    swb_offset_128_24,
    swb_offset_128_16,
    swb_offset_128_16,
    swb_offset_128_16,
    swb_offset_128_8,
    swb_offset_128_8,
};
static const uint8_t long_count[13]={41,41,47,49,49,51,47,47,43,43,43,40,40};
static const uint8_t short_count[13]={12,12,12,14,14,14,15,15,15,15,15,15,15};
/* Layout 960 (AAC-LC and the other long-window profiles with frameLengthFlag = 1: 960 lines, eight short windows of 120): scalefactor-band offsets by sampling_frequency_index.
 * Numeric tables validated against FFmpeg libavcodec/aactab.c (LGPL-2.1-or-later), like the tables above; same licensing note applies. */
static const uint16_t swb_offset_960_96[41] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44,
    48, 52, 56, 64, 72, 80, 88, 96, 108, 120, 132, 144,
    156, 172, 188, 212, 240, 276, 320, 384, 448, 512, 576, 640,
    704, 768, 832, 896, 960,
};
static const uint16_t swb_offset_960_64[47] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44,
    48, 52, 56, 64, 72, 80, 88, 100, 112, 124, 140, 156,
    172, 192, 216, 240, 268, 304, 344, 384, 424, 464, 504, 544,
    584, 624, 664, 704, 744, 784, 824, 864, 904, 944, 960,
};
static const uint16_t swb_offset_960_48[50] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 48,
    56, 64, 72, 80, 88, 96, 108, 120, 132, 144, 160, 176,
    196, 216, 240, 264, 292, 320, 352, 384, 416, 448, 480, 512,
    544, 576, 608, 640, 672, 704, 736, 768, 800, 832, 864, 896,
    928, 960,
};
static const uint16_t swb_offset_960_24[47] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44,
    52, 60, 68, 76, 84, 92, 100, 108, 116, 124, 136, 148,
    160, 172, 188, 204, 220, 240, 260, 284, 308, 336, 364, 396,
    432, 468, 508, 552, 600, 652, 704, 768, 832, 896, 960,
};
static const uint16_t swb_offset_960_16[43] = {
    0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88,
    100, 112, 124, 136, 148, 160, 172, 184, 196, 212, 228, 244,
    260, 280, 300, 320, 344, 368, 396, 424, 456, 492, 532, 572,
    616, 664, 716, 772, 832, 896, 960,
};
static const uint16_t swb_offset_960_8[41] = {
    0, 12, 24, 36, 48, 60, 72, 84, 96, 108, 120, 132,
    144, 156, 172, 188, 204, 220, 236, 252, 268, 288, 308, 328,
    348, 372, 396, 420, 448, 476, 508, 544, 580, 620, 664, 712,
    764, 820, 880, 944, 960,
};
static const uint16_t swb_offset_120_96[13] = {
    0, 4, 8, 12, 16, 20, 24, 32, 40, 48, 64, 92,
    120,
};
static const uint16_t swb_offset_120_48[15] = {
    0, 4, 8, 12, 16, 20, 28, 36, 44, 56, 68, 80,
    96, 112, 120,
};
static const uint16_t swb_offset_120_24[16] = {
    0, 4, 8, 12, 16, 20, 24, 28, 36, 44, 52, 64,
    76, 92, 108, 120,
};
static const uint16_t swb_offset_120_16[16] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 40, 48, 60,
    72, 88, 108, 120,
};
static const uint16_t swb_offset_120_8[16] = {
    0, 4, 8, 12, 16, 20, 24, 28, 36, 44, 52, 60,
    72, 88, 108, 120,
};
static const uint16_t *const long960_offsets[13]={swb_offset_960_96,swb_offset_960_96,swb_offset_960_64,swb_offset_960_48,swb_offset_960_48,swb_offset_960_48,swb_offset_960_24,swb_offset_960_24,swb_offset_960_16,swb_offset_960_16,swb_offset_960_16,swb_offset_960_8,swb_offset_960_8};
static const unsigned char long960_count[13]={40,40,46,49,49,49,46,46,42,42,42,40,40};
static const uint16_t *const short120_offsets[13]={swb_offset_120_96,swb_offset_120_96,swb_offset_120_96,swb_offset_120_48,swb_offset_120_48,swb_offset_120_48,swb_offset_120_24,swb_offset_120_24,swb_offset_120_16,swb_offset_120_16,swb_offset_120_16,swb_offset_120_8,swb_offset_120_8};
static const unsigned char short120_count[13]={12,12,12,14,14,14,15,15,15,15,15,15,15};

/* Low-delay layouts (AAC-LD / AAC-ELD): 512 and 480 lines, long window only, sampling_frequency_index 3..12 (48, 44.1, 32 kHz tables; 24 kHz table for 24 kHz and below, as FDK).
 * Numeric tables validated against FFmpeg libavcodec/aactab.c (LGPL-2.1-or-later), like the tables above; same licensing note applies. */
static const uint16_t swb_offset_512_48[37] = {
      0,   4,   8,  12,  16,  20,  24,  28,  32,  36,  40,  44,  48,  52,  56,  60,
     68,  76,  84,  92, 100, 112, 124, 136, 148, 164, 184, 208, 236, 268, 300, 332,
    364, 396, 428, 460, 512
};
static const uint16_t swb_offset_480_48[36] = {
      0,   4,   8,  12,  16,  20,  24,  28,  32,  36,  40,  44,  48,  52,  56,  64,
     72,  80,  88,  96, 108, 120, 132, 144, 156, 172, 188, 212, 240, 272, 304, 336,
    368, 400, 432, 480
};
static const uint16_t swb_offset_512_32[38] = {
      0,   4,   8,  12,  16,  20,  24,  28,  32,  36,  40,  44,  48,  52,  56,  64,
     72,  80,  88,  96, 108, 120, 132, 144, 160, 176, 192, 212, 236, 260, 288, 320,
    352, 384, 416, 448, 480, 512
};
static const uint16_t swb_offset_480_32[38] = {
      0,   4,   8,  12,  16,  20,  24,  28,  32,  36,  40,  44,  48,  52,  56,  60,
     64,  72,  80,  88,  96, 104, 112, 124, 136, 148, 164, 180, 200, 224, 256, 288,
    320, 352, 384, 416, 448, 480
};
static const uint16_t swb_offset_512_24[32] = {
      0,   4,   8,  12,  16,  20,  24,  28,  32,  36,  40,  44,  52,  60,  68,  80,
     92, 104, 120, 140, 164, 192, 224, 256, 288, 320, 352, 384, 416, 448, 480, 512
};
static const uint16_t swb_offset_480_24[31] = {
      0,   4,   8,  12,  16,  20,  24,  28,  32,  36,  40,  44,  52,  60,  68,  80,
     92, 104, 120, 140, 164, 192, 224, 256, 288, 320, 352, 384, 416, 448, 480
};
static const uint16_t *const ld512_offsets[13]={NULL,NULL,NULL,swb_offset_512_48,swb_offset_512_48,swb_offset_512_32,swb_offset_512_24,swb_offset_512_24,swb_offset_512_24,swb_offset_512_24,swb_offset_512_24,swb_offset_512_24,swb_offset_512_24};
static const unsigned char ld512_count[13]={0,0,0,36,36,37,31,31,31,31,31,31,31};
static const uint16_t *const ld480_offsets[13]={NULL,NULL,NULL,swb_offset_480_48,swb_offset_480_48,swb_offset_480_32,swb_offset_480_24,swb_offset_480_24,swb_offset_480_24,swb_offset_480_24,swb_offset_480_24,swb_offset_480_24,swb_offset_480_24};
static const unsigned char ld480_count[13]={0,0,0,35,35,37,30,30,30,30,30,30,30};
/* tns_max_bands of the low-delay layouts (ISO/IEC 14496-3, same source as above) */
static const unsigned char tns_max_512[13]={0,0,0,31,32,37,31,31,31,31,31,31,31};
static const unsigned char tns_max_480[13]={0,0,0,31,32,37,30,30,30,30,30,30,30};

int sauti_aac_band_offsets(unsigned sample_rate_index,unsigned short_window,
                           const uint16_t **offsets,unsigned *count){
    const unsigned layout=SAUTI_AAC_IDX_LAYOUT(sample_rate_index),rate=SAUTI_AAC_IDX_RATE(sample_rate_index);
    if(rate>=13 || short_window>1 || !offsets || !count)return 0;
    if(layout==SAUTI_AAC_LAYOUT_1024){
        *offsets=short_window?short_offsets[rate]:long_offsets[rate];
        *count=short_window?short_count[rate]:long_count[rate];
        return 1;
    }
    if(layout==SAUTI_AAC_LAYOUT_960){
        *offsets=short_window?short120_offsets[rate]:long960_offsets[rate];
        *count=short_window?short120_count[rate]:long960_count[rate];
        return 1;
    }
    if(short_window || layout>SAUTI_AAC_LAYOUT_480)return 0;                 /* low-delay layouts have no short windows */
    *offsets=layout==SAUTI_AAC_LAYOUT_512?ld512_offsets[rate]:ld480_offsets[rate];
    *count=layout==SAUTI_AAC_LAYOUT_512?ld512_count[rate]:ld480_count[rate];
    return *offsets!=NULL;
}
unsigned sauti_aac_tns_max_bands(unsigned sample_rate_index,unsigned short_window){
    static const unsigned char max_long[13]={31,31,34,40,42,51,46,46,42,42,42,39,39};
    static const unsigned char max_short[13]={9,9,10,14,14,14,14,14,14,14,14,14,14};
    const unsigned layout=SAUTI_AAC_IDX_LAYOUT(sample_rate_index),rate=SAUTI_AAC_IDX_RATE(sample_rate_index);
    if(rate>=13 || short_window>1)return 0;
    if(SAUTI_AAC_IDX_IS_SSR(sample_rate_index))return rate<12u?sauti_aac_ssr_tns_max_bands[rate][short_window]:0u;       /* AAC SSR: its own TNS_MAX_BANDS columns */
    if(layout==SAUTI_AAC_LAYOUT_1024 || layout==SAUTI_AAC_LAYOUT_960)return short_window?max_short[rate]:max_long[rate];   /* 960 uses the 1024 / 128 limits (as FFmpeg) */
    if(short_window)return 0;
    return layout==SAUTI_AAC_LAYOUT_512?tns_max_512[rate]:layout==SAUTI_AAC_LAYOUT_480?tns_max_480[rate]:0u;
}
