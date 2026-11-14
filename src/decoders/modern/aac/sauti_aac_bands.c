/* AAC scalefactor-band offsets for 1024 and 128 spectral lines.
 * Numeric tables validated against FFmpeg libavcodec/aactab.c
 * (LGPL-2.1-or-later); no FFmpeg linkage or runtime dependency.
 * Licensing of bundled numeric data needs review before distribution. */
#include "sauti_aac_bands.h"
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
int sauti_aac_band_offsets(unsigned sample_rate_index,unsigned short_window,
                           const uint16_t **offsets,unsigned *count){
    if(sample_rate_index>=13 || short_window>1 || !offsets || !count)return 0;
    *offsets=short_window?short_offsets[sample_rate_index]:long_offsets[sample_rate_index];
    *count=short_window?short_count[sample_rate_index]:long_count[sample_rate_index];
    return 1;
}
