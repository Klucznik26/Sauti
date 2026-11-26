/* MaktabaSauti, modul maktabasauti_mpc: Musepack SV7 ("MP+") i SV8 ("MPCK"), napisany od zera.
 * Stale liczbowe (Huffmana, requant, okno syntezy, RNG, kombinacje SV8) sa opisem formatu
 * z libmpcdec r475 (Musepack Development Team). SV7 po magii czyta slowa little-endian od MSB.
 * SV8 czyta bajty od MSB i pakiety MPCK. SV4-SV6 i ReplayGain zostaja poza tym modulem.
 * Intensity stereo SV7 jest w naglowku i referencja je tylko odczytuje: bit jest pomijany.
 * Kanalow innych niz 1 i 2 SV8 nie mapuje. */
#include "maktabasauti/maktabasauti_codec.h"
#include <string.h>

enum {
    MPC_BANDS = 32,
    MPC_SUB = 36,
    MPC_FRAME = 36 * 32,
    MPC_DELAY = 481,
    MPC_VMEM = 2304,
    MPC_VTAIL = 960,
    MPC_VSIZE = MPC_VMEM + MPC_VTAIL,
    MPC_CH = 2
};

typedef struct { uint16_t code; uint8_t len; int8_t value; } Huff;

#include "maktabasauti_mpc_sv8.inc"

static const Huff HUFF_HDR[] = {
    {0x8000, 1, 0}, {0x6000, 3, 1}, {0x5e00, 7, -4}, {0x5d80, 9, 3}, {0x5d00, 9, 4},
    {0x5c00, 8, -5}, {0x5800, 6, 2}, {0x5000, 5, -3}, {0x4000, 4, -2}, {0x0000, 2, -1}
};
static const Huff HUFF_SCFI[] = {
    {0x8000, 1, 1}, {0x6000, 3, 2}, {0x4000, 3, 0}, {0x0000, 2, 3}
};
static const Huff HUFF_DSCF[] = {
    {0xf800, 5, 5}, {0xf000, 5, -4}, {0xe000, 4, 3}, {0xd000, 4, -3}, {0xc000, 4, 8},
    {0xa000, 3, 1}, {0x9000, 4, 0}, {0x8800, 5, -5}, {0x8400, 6, 7}, {0x8000, 6, -7},
    {0x6000, 3, -1}, {0x4000, 3, 2}, {0x3000, 4, 4}, {0x2800, 5, 6}, {0x2000, 5, -6}, {0x0000, 3, -2}
};
static const Huff HUFF_Q1[2][27] = {{
    {0xe000, 3, 13}, {0xdc00, 6, 26}, {0xd800, 6, 0}, {0xd400, 6, 20}, {0xd000, 6, 6}, {0xc000, 4, 14},
    {0xb000, 4, 12}, {0xa000, 4, 4}, {0x9000, 4, 22}, {0x8c00, 6, 8}, {0x8800, 6, 18}, {0x8400, 6, 24},
    {0x8000, 6, 2}, {0x7000, 4, 16}, {0x6000, 4, 10}, {0x5800, 5, 17}, {0x5000, 5, 9}, {0x4800, 5, 1},
    {0x4000, 5, 25}, {0x3800, 5, 5}, {0x3000, 5, 21}, {0x2800, 5, 3}, {0x2000, 5, 11}, {0x1800, 5, 15},
    {0x1000, 5, 23}, {0x0800, 5, 19}, {0x0000, 5, 7}
}, {
    {0x8000, 1, 13}, {0x7e00, 7, 15}, {0x7c00, 7, 1}, {0x7a00, 7, 11}, {0x7800, 7, 7}, {0x7600, 7, 17},
    {0x7400, 7, 25}, {0x7200, 7, 19}, {0x7180, 9, 8}, {0x7100, 9, 18}, {0x7080, 9, 2}, {0x7000, 9, 24},
    {0x6e00, 7, 3}, {0x6c00, 7, 23}, {0x6a00, 7, 21}, {0x6800, 7, 5}, {0x6700, 8, 0}, {0x6600, 8, 26},
    {0x6500, 8, 6}, {0x6400, 8, 20}, {0x6000, 6, 9}, {0x5000, 4, 14}, {0x4000, 4, 12}, {0x3000, 4, 4},
    {0x2000, 4, 22}, {0x1000, 4, 16}, {0x0000, 4, 10}
}};
static const Huff HUFF_Q2[2][25] = {{
    {0xf000, 4, 13}, {0xe000, 4, 17}, {0xd000, 4, 7}, {0xc000, 4, 11}, {0xbc00, 6, 1}, {0xb800, 6, 23},
    {0xb600, 7, 4}, {0xb400, 7, 20}, {0xb200, 7, 0}, {0xb000, 7, 24}, {0xa800, 5, 22}, {0xa000, 5, 10},
    {0x8000, 3, 12}, {0x7800, 5, 2}, {0x7000, 5, 14}, {0x6000, 4, 6}, {0x5000, 4, 18}, {0x4000, 4, 8},
    {0x3000, 4, 16}, {0x2800, 5, 9}, {0x2000, 5, 5}, {0x1800, 5, 15}, {0x1000, 5, 21}, {0x0800, 5, 19}, {0x0000, 5, 3}
}, {
    {0xf800, 5, 18}, {0xf000, 5, 6}, {0xe800, 5, 8}, {0xe700, 8, 3}, {0xe6c0, 10, 24}, {0xe680, 10, 4},
    {0xe640, 10, 0}, {0xe600, 10, 20}, {0xe400, 7, 23}, {0xe200, 7, 1}, {0xe000, 7, 19}, {0xd800, 5, 16},
    {0xd600, 7, 15}, {0xd400, 7, 21}, {0xd200, 7, 9}, {0xd000, 7, 5}, {0xcc00, 6, 2}, {0xc800, 6, 10},
    {0xc400, 6, 14}, {0xc000, 6, 22}, {0x8000, 2, 12}, {0x6000, 3, 13}, {0x4000, 3, 17}, {0x2000, 3, 11}, {0x0000, 3, 7}
}};
static const Huff HUFF_Q3[2][7] = {{
    {0xe000, 3, 1}, {0xd000, 4, 3}, {0xc000, 4, -3}, {0xa000, 3, 2}, {0x8000, 3, -2}, {0x4000, 2, 0}, {0x0000, 2, -1}
}, {
    {0xc000, 2, 0}, {0x8000, 2, -1}, {0x4000, 2, 1}, {0x3000, 4, -2}, {0x2800, 5, 3}, {0x2000, 5, -3}, {0x0000, 3, 2}
}};
static const Huff HUFF_Q4[2][9] = {{
    {0xe000, 3, 0}, {0xc000, 3, -1}, {0xa000, 3, 1}, {0x8000, 3, -2}, {0x6000, 3, 2}, {0x5000, 4, -4},
    {0x4000, 4, 4}, {0x2000, 3, 3}, {0x0000, 3, -3}
}, {
    {0xe000, 3, 1}, {0xd000, 4, 2}, {0xc000, 4, -3}, {0x8000, 2, 0}, {0x6000, 3, -2}, {0x5000, 4, 3},
    {0x4800, 5, -4}, {0x4000, 5, 4}, {0x0000, 2, -1}
}};
static const Huff HUFF_Q5[2][15] = {{
    {0xf000, 4, 2}, {0xe800, 5, 5}, {0xe400, 6, -7}, {0xe000, 6, 7}, {0xd000, 4, -3}, {0xc000, 4, 3},
    {0xb800, 5, -6}, {0xb000, 5, 6}, {0xa000, 4, -4}, {0x9000, 4, 4}, {0x8000, 4, -5}, {0x6000, 3, 0},
    {0x4000, 3, -1}, {0x2000, 3, 1}, {0x0000, 3, -2}
}, {
    {0xf000, 4, 3}, {0xe800, 5, 4}, {0xe600, 7, 6}, {0xe500, 8, -7}, {0xe400, 8, 7}, {0xe000, 6, -6},
    {0xc000, 3, 0}, {0xa000, 3, -1}, {0x8000, 3, 1}, {0x6000, 3, -2}, {0x4000, 3, 2}, {0x3800, 5, -5},
    {0x3000, 5, 5}, {0x2000, 4, -4}, {0x0000, 3, -3}
}};
static const Huff HUFF_Q6[2][31] = {{
    {0xf800, 5, 3}, {0xf000, 5, -4}, {0xec00, 6, -11}, {0xe800, 6, 12}, {0xe000, 5, 4}, {0xd800, 5, 6},
    {0xd000, 5, -5}, {0xc800, 5, 5}, {0xc000, 5, 7}, {0xb800, 5, -7}, {0xb400, 6, -12}, {0xb000, 6, -13},
    {0xa800, 5, -6}, {0xa000, 5, 8}, {0x9800, 5, -8}, {0x9000, 5, 9}, {0x8800, 5, -9}, {0x8400, 6, 13},
    {0x8200, 7, -15}, {0x8000, 7, 15}, {0x7000, 4, 0}, {0x6800, 5, -10}, {0x6000, 5, 10}, {0x5000, 4, -1},
    {0x4000, 4, 2}, {0x3000, 4, 1}, {0x2000, 4, -2}, {0x1c00, 6, 14}, {0x1800, 6, -14}, {0x1000, 5, 11}, {0x0000, 4, -3}
}, {
    {0xf800, 5, -6}, {0xf000, 5, 6}, {0xe000, 4, 1}, {0xd000, 4, -1}, {0xce00, 7, 10}, {0xcc00, 7, -10},
    {0xcb00, 8, -11}, {0xca80, 9, -12}, {0xca60, 11, 13}, {0xca58, 13, 15}, {0xca50, 13, -14}, {0xca48, 13, 14},
    {0xca40, 13, -15}, {0xca00, 10, -13}, {0xc900, 8, 11}, {0xc800, 8, 12}, {0xc400, 6, -9}, {0xc000, 6, 9},
    {0xb000, 4, -2}, {0xa000, 4, 2}, {0x9000, 4, 3}, {0x8000, 4, -3}, {0x7800, 5, -7}, {0x7000, 5, 7},
    {0x6000, 4, -4}, {0x5000, 4, 4}, {0x4800, 5, -8}, {0x4000, 5, 8}, {0x3000, 4, 5}, {0x2000, 4, -5}, {0x0000, 3, 0}
}};
static const Huff HUFF_Q7[2][63] = {{
    {0xfc00, 6, 7}, {0xf800, 6, 8}, {0xf400, 6, 9}, {0xf000, 6, -8}, {0xec00, 6, 11}, {0xea00, 7, 21},
    {0xe900, 8, -28}, {0xe800, 8, 28}, {0xe400, 6, -9}, {0xe200, 7, -22}, {0xe000, 7, -21}, {0xdc00, 6, -10},
    {0xd800, 6, -11}, {0xd400, 6, 10}, {0xd000, 6, 12}, {0xcc00, 6, -13}, {0xca00, 7, 22}, {0xc800, 7, 23},
    {0xc400, 6, -12}, {0xc000, 6, 13}, {0xbc00, 6, 14}, {0xb800, 6, -14}, {0xb600, 7, -23}, {0xb500, 8, -29},
    {0xb400, 8, 29}, {0xb000, 6, -15}, {0xac00, 6, 15}, {0xa800, 6, 16}, {0xa400, 6, -16}, {0xa200, 7, -24},
    {0xa000, 7, 24}, {0x9c00, 6, 17}, {0x9a00, 7, -25}, {0x9900, 8, -30}, {0x9800, 8, 30}, {0x9400, 6, -17},
    {0x9000, 6, 18}, {0x8c00, 6, -18}, {0x8a00, 7, 25}, {0x8800, 7, 26}, {0x8400, 6, 19}, {0x8200, 7, -26},
    {0x8000, 7, -27}, {0x7800, 5, 2}, {0x7400, 6, -19}, {0x7000, 6, 20}, {0x6800, 5, -1}, {0x6700, 8, -31},
    {0x6600, 8, 31}, {0x6400, 7, 27}, {0x6000, 6, -20}, {0x5800, 5, 1}, {0x5000, 5, -5}, {0x4800, 5, -3},
    {0x4000, 5, 3}, {0x3800, 5, 0}, {0x3000, 5, -2}, {0x2800, 5, -4}, {0x2000, 5, 4}, {0x1800, 5, 5},
    {0x1000, 5, -6}, {0x0800, 5, 6}, {0x0000, 5, -7}
}, {
    {0xf800, 5, -1}, {0xf000, 5, 2}, {0xe800, 5, -2}, {0xe000, 5, 3}, {0xdf00, 8, -20}, {0xdec0, 10, 24},
    {0xdebc, 14, 28}, {0xdeb8, 14, -28}, {0xdeb4, 14, -30}, {0xdeb0, 14, 30}, {0xdea0, 12, -27}, {0xde9c, 14, 29},
    {0xde98, 14, -29}, {0xde94, 14, 31}, {0xde90, 14, -31}, {0xde80, 12, 27}, {0xde00, 9, -22}, {0xdc00, 7, -17},
    {0xd800, 6, -11}, {0xd000, 5, -3}, {0xc800, 5, 4}, {0xc000, 5, -4}, {0xbe00, 7, 17}, {0xbd00, 8, 20},
    {0xbc80, 9, 22}, {0xbc40, 10, -25}, {0xbc00, 10, -26}, {0xb800, 6, 12}, {0xb000, 5, 5}, {0xa800, 5, -5},
    {0xa000, 5, 6}, {0x9800, 5, -6}, {0x9400, 6, -12}, {0x9200, 7, -18}, {0x9000, 7, 18}, {0x8c00, 6, 13},
    {0x8800, 6, -13}, {0x8000, 5, -7}, {0x7c00, 6, 14}, {0x7b00, 8, 21}, {0x7a00, 8, -21}, {0x7800, 7, -19},
    {0x7000, 5, 7}, {0x6800, 5, 8}, {0x6400, 6, -14}, {0x6000, 6, -15}, {0x5800, 5, -8}, {0x5400, 6, 15},
    {0x5200, 7, 19}, {0x51c0, 10, 25}, {0x5180, 10, 26}, {0x5100, 9, -23}, {0x5080, 9, 23}, {0x5000, 9, -24},
    {0x4800, 5, -9}, {0x4000, 5, 9}, {0x3c00, 6, 16}, {0x3800, 6, -16}, {0x3000, 5, 10}, {0x2000, 4, 0},
    {0x1800, 5, -10}, {0x1000, 5, 11}, {0x0000, 4, 1}
}};

static const int8_t IDX30[27] = {-1, 0, 1,-1, 0, 1,-1, 0, 1,-1, 0, 1,-1, 0, 1,-1, 0, 1,-1, 0, 1,-1, 0, 1,-1, 0, 1};
static const int8_t IDX31[27] = {-1,-1,-1, 0, 0, 0, 1, 1, 1,-1,-1,-1, 0, 0, 0, 1, 1, 1,-1,-1,-1, 0, 0, 0, 1, 1, 1};
static const int8_t IDX32[27] = {-1,-1,-1,-1,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static const int8_t IDX50[25] = {-2,-1, 0, 1, 2,-2,-1, 0, 1, 2,-2,-1, 0, 1, 2,-2,-1, 0, 1, 2,-2,-1, 0, 1, 2};
static const int8_t IDX51[25] = {-2,-2,-2,-2,-2,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2};

static const uint8_t RES_BITS[18] = {0, 0, 0, 0, 0, 0, 0, 0, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
static const int DC[18] = {0, 1, 2, 3, 4, 7, 15, 31, 63, 127, 255, 511, 1023, 2047, 4095, 8191, 16383, 32767};
static const float CC_NOISE = 111.285962475327f;
static const float CC[18] = {
    65536.000000000000f, 21845.333333333332f, 13107.200000000001f, 9362.285714285713f, 7281.777777777777f,
    4369.066666666666f, 2114.064516129032f, 1040.253968253968f, 516.031496062992f, 257.003921568627f,
    128.250489236790f, 64.062561094819f, 32.015632633121f, 16.003907203907f, 8.000976681723f,
    4.000244155527f, 2.000061037018f, 1.000015259021f
};

/* Okno syntezy SV7, wartosci calkowite dzielone przez 65536. Bin 16 transformaty nie jest zapisywany. */
static const int32_t DI_RAW[32][16] = {
    {0, -29, 213, -459, 2037, -5153, 6574, -37489, 75038, 37489, 6574, 5153, 2037, 459, 213, 29},
    {-1, -31, 218, -519, 2000, -5517, 5959, -39336, 74992, 35640, 7134, 4788, 2063, 401, 208, 26},
    {-1, -35, 222, -581, 1952, -5879, 5288, -41176, 74856, 33791, 7640, 4425, 2080, 347, 202, 24},
    {-1, -38, 225, -645, 1893, -6237, 4561, -43006, 74630, 31947, 8092, 4063, 2087, 294, 196, 21},
    {-1, -41, 227, -711, 1822, -6589, 3776, -44821, 74313, 30112, 8492, 3705, 2085, 244, 190, 19},
    {-1, -45, 228, -779, 1739, -6935, 2935, -46617, 73908, 28289, 8840, 3351, 2075, 197, 183, 17},
    {-1, -49, 228, -848, 1644, -7271, 2037, -48390, 73415, 26482, 9139, 3004, 2057, 153, 176, 16},
    {-2, -53, 227, -919, 1535, -7597, 1082, -50137, 72835, 24694, 9389, 2663, 2032, 111, 169, 14},
    {-2, -58, 224, -991, 1414, -7910, 70, -51853, 72169, 22929, 9592, 2330, 2001, 72, 161, 13},
    {-2, -63, 221, -1064, 1280, -8209, -998, -53534, 71420, 21189, 9750, 2006, 1962, 36, 154, 11},
    {-2, -68, 215, -1137, 1131, -8491, -2122, -55178, 70590, 19478, 9863, 1692, 1919, 2, 147, 10},
    {-3, -73, 208, -1210, 970, -8755, -3300, -56778, 69679, 17799, 9935, 1388, 1870, -29, 139, 9},
    {-3, -79, 200, -1283, 794, -8998, -4533, -58333, 68692, 16155, 9966, 1095, 1817, -57, 132, 8},
    {-4, -85, 189, -1356, 605, -9219, -5818, -59838, 67629, 14548, 9959, 814, 1759, -83, 125, 7},
    {-4, -91, 177, -1428, 402, -9416, -7154, -61289, 66494, 12980, 9916, 545, 1698, -106, 117, 7},
    {-5, -97, 163, -1498, 185, -9585, -8540, -62684, 65290, 11455, 9838, 288, 1634, -127, 111, 6},
    {-5, -104, 146, -1567, -45, -9727, -9975, -64019, 64019, 9975, 9727, 45, 1567, -146, 104, 5},
    {-6, -111, 127, -1634, -288, -9838, -11455, -65290, 62684, 8540, 9585, -185, 1498, -163, 97, 5},
    {-7, -117, 106, -1698, -545, -9916, -12980, -66494, 61289, 7154, 9416, -402, 1428, -177, 91, 4},
    {-7, -125, 83, -1759, -814, -9959, -14548, -67629, 59838, 5818, 9219, -605, 1356, -189, 85, 4},
    {-8, -132, 57, -1817, -1095, -9966, -16155, -68692, 58333, 4533, 8998, -794, 1283, -200, 79, 3},
    {-9, -139, 29, -1870, -1388, -9935, -17799, -69679, 56778, 3300, 8755, -970, 1210, -208, 73, 3},
    {-10, -147, -2, -1919, -1692, -9863, -19478, -70590, 55178, 2122, 8491, -1131, 1137, -215, 68, 2},
    {-11, -154, -36, -1962, -2006, -9750, -21189, -71420, 53534, 998, 8209, -1280, 1064, -221, 63, 2},
    {-13, -161, -72, -2001, -2330, -9592, -22929, -72169, 51853, -70, 7910, -1414, 991, -224, 58, 2},
    {-14, -169, -111, -2032, -2663, -9389, -24694, -72835, 50137, -1082, 7597, -1535, 919, -227, 53, 2},
    {-16, -176, -153, -2057, -3004, -9139, -26482, -73415, 48390, -2037, 7271, -1644, 848, -228, 49, 1},
    {-17, -183, -197, -2075, -3351, -8840, -28289, -73908, 46617, -2935, 6935, -1739, 779, -228, 45, 1},
    {-19, -190, -244, -2085, -3705, -8492, -30112, -74313, 44821, -3776, 6589, -1822, 711, -227, 41, 1},
    {-21, -196, -294, -2087, -4063, -8092, -31947, -74630, 43006, -4561, 6237, -1893, 645, -225, 38, 1},
    {-24, -202, -347, -2080, -4425, -7640, -33791, -74856, 41176, -5288, 5879, -1952, 581, -222, 35, 1},
    {-26, -208, -401, -2063, -4788, -7134, -35640, -74992, 39336, -5959, 5517, -2000, 519, -218, 31, 1}
};

static const uint32_t RATES[4] = {44100u, 48000u, 37800u, 32000u};

typedef struct {
    const maktabasauti_host_t *host;
    uint8_t *file;
    size_t file_n;
    size_t bs0;
    uint64_t bitpos;
    uint64_t audio_bit;
    uint32_t rate;
    int ver;
    int channels;
    int ms;
    int max_band;
    int gapless;
    int block_pwr;
    int last_max_band;
    int block_frames;
    int64_t block_bits;
    uint64_t beg_silence;
    uint8_t dscf_l[32], dscf_r[32];
    uint32_t frames_hdr;
    uint64_t audible;
    uint64_t samples;
    uint64_t samples0;
    uint64_t decoded;
    uint32_t skip;
    uint32_t r1, r2;
    int32_t scf_l[32][3], scf_r[32][3];
    int res_l[32], res_r[32];
    int scfi_l[32], scfi_r[32];
    int ms_flag[32];
    int16_t q_l[32][36], q_r[32][36];
    float y_l[36][32], y_r[36][32];
    float v_l[MPC_VSIZE], v_r[MPC_VSIZE];
    float scf_tab[256];
    float pcm[MPC_FRAME * MPC_CH];
    size_t pcm_n, pcm_i;
    uint64_t out_pos;
    uint32_t frames_done;
    maktabasauti_result_t pend;
    const char *err;
} Mpc;

static int parity32(uint32_t v) {
    v ^= v >> 16; v ^= v >> 8; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
    return (int)(v & 1u);
}

static uint32_t mpc_random(Mpc *s) {
    uint32_t t1 = s->r1, t2 = s->r2;
    uint32_t p1 = (uint32_t)parity32(t1 & 0xF5u);
    uint32_t p2 = (uint32_t)parity32((t2 >> 25) & 0x63u);
    s->r1 = (t1 >> 1) | (p1 << 31);
    s->r2 = (t2 << 1) | p2;
    return s->r1 ^ s->r2;
}

static uint32_t le32_pad(const Mpc *s, size_t off) {
    uint32_t v = 0;
    int i;
    for (i = 0; i < 4; i++) if (off + (size_t)i < s->file_n) v |= (uint32_t)s->file[off + (size_t)i] << (8 * i);
    return v;
}

static uint32_t bit_peek(const Mpc *s, unsigned n) {
    size_t byte;
    unsigned sh;
    uint32_t w0, w1;
    uint64_t window;
    if (n == 0 || n > 31) return 0;
    byte = s->bs0 + (size_t)(s->bitpos / 32u) * 4u;
    sh = (unsigned)(s->bitpos % 32u);
    w0 = le32_pad(s, byte);
    w1 = le32_pad(s, byte + 4u);
    window = (((uint64_t)w0 << 32) | w1) << sh;
    return (uint32_t)((window >> (64u - n)) & ((1u << n) - 1u));
}

static uint32_t bit_read(Mpc *s, unsigned n) {
    uint32_t v = bit_peek(s, n);
    s->bitpos += n;
    return v;
}

static int huff_dec(Mpc *s, const Huff *tab) {
    uint32_t code = bit_peek(s, 16);
    const Huff *t = tab;
    int guard = 0;
    while (code < t->code) { t++; if (++guard > 80) break; }
    bit_read(s, t->len);
    return t->value;
}

static const Huff *q_table(int res, int sel) {
    switch (res) {
    case 1: return HUFF_Q1[sel];
    case 2: return HUFF_Q2[sel];
    case 3: return HUFF_Q3[sel];
    case 4: return HUFF_Q4[sel];
    case 5: return HUFF_Q5[sel];
    case 6: return HUFF_Q6[sel];
    default: return HUFF_Q7[sel];
    }
}

static float cc_of(int res) { return res < 0 ? CC_NOISE : CC[res]; }

static void build_scf(Mpc *s) {
    const double ratio = 0.83298066476582673961;
    double factor = 1.0 / 32768.0;
    double f1, f2;
    int n;
    memset(s->scf_tab, 0, sizeof s->scf_tab);
    s->scf_tab[1] = (float)factor;
    f1 = factor * ratio;
    f2 = factor / ratio;
    for (n = 1; n <= 128; n++) {
        s->scf_tab[(uint8_t)(1 + n)] = (float)f1;
        s->scf_tab[(uint8_t)(1 - n)] = (float)f2;
        f1 *= ratio;
        f2 /= ratio;
    }
}

static void scale_band(const Mpc *s, float *y, const int16_t *q, int res, const int32_t scf[3]) {
    int g, n;
    for (g = 0; g < 3; g++) {
        float fac = cc_of(res) * s->scf_tab[scf[g] & 0xFF];
        for (n = 0; n < 12; n++, y += 32) *y = fac * (float)q[g * 12 + n];
    }
}

static void requant(Mpc *s) {
    int band, n;
    memset(s->y_l, 0, sizeof s->y_l);
    memset(s->y_r, 0, sizeof s->y_r);
    for (band = 0; band <= s->max_band; band++) {
        float *yl = &s->y_l[0][band];
        float *yr = &s->y_r[0][band];
        int rl = s->res_l[band], rr = s->res_r[band];
        if (s->ms_flag[band]) {
            if (rl && rr) {
                float facl, facr, a, b;
                int g;
                for (g = 0; g < 3; g++) {
                    facl = cc_of(rl) * s->scf_tab[s->scf_l[band][g] & 0xFF];
                    facr = cc_of(rr) * s->scf_tab[s->scf_r[band][g] & 0xFF];
                    for (n = 0; n < 12; n++, yl += 32, yr += 32) {
                        a = facl * (float)s->q_l[band][g * 12 + n];
                        b = facr * (float)s->q_r[band][g * 12 + n];
                        *yl = a + b;
                        *yr = a - b;
                    }
                }
            } else if (rl) {
                scale_band(s, yl, s->q_l[band], rl, s->scf_l[band]);
                for (n = 0; n < 36; n++) s->y_r[n][band] = s->y_l[n][band];
            } else if (rr) {
                scale_band(s, yl, s->q_r[band], rr, s->scf_r[band]);
                for (n = 0; n < 36; n++) { s->y_r[n][band] = -s->y_l[n][band]; }
            }
        } else {
            if (rl) scale_band(s, yl, s->q_l[band], rl, s->scf_l[band]);
            if (rr) scale_band(s, yr, s->q_r[band], rr, s->scf_r[band]);
        }
    }
}

static void new_v(const float *in, float *p) {
    float A00, A01, A02, A03, A04, A05, A06, A07, A08, A09, A10, A11, A12, A13, A14, A15;
    float B00, B01, B02, B03, B04, B05, B06, B07, B08, B09, B10, B11, B12, B13, B14, B15;
    float tmp;
    A00 = in[0] + in[31]; A01 = in[1] + in[30]; A02 = in[2] + in[29]; A03 = in[3] + in[28];
    A04 = in[4] + in[27]; A05 = in[5] + in[26]; A06 = in[6] + in[25]; A07 = in[7] + in[24];
    A08 = in[8] + in[23]; A09 = in[9] + in[22]; A10 = in[10] + in[21]; A11 = in[11] + in[20];
    A12 = in[12] + in[19]; A13 = in[13] + in[18]; A14 = in[14] + in[17]; A15 = in[15] + in[16];
    B00 = A00 + A15; B01 = A01 + A14; B02 = A02 + A13; B03 = A03 + A12;
    B04 = A04 + A11; B05 = A05 + A10; B06 = A06 + A09; B07 = A07 + A08;
    B08 = (A00 - A15) * 0.5024192929f; B09 = (A01 - A14) * 0.5224986076f;
    B10 = (A02 - A13) * 0.5669440627f; B11 = (A03 - A12) * 0.6468217969f;
    B12 = (A04 - A11) * 0.7881546021f; B13 = (A05 - A10) * 1.0606776476f;
    B14 = (A06 - A09) * 1.7224471569f; B15 = (A07 - A08) * 5.1011486053f;
    A00 = B00 + B07; A01 = B01 + B06; A02 = B02 + B05; A03 = B03 + B04;
    A04 = (B00 - B07) * 0.5097956061f; A05 = (B01 - B06) * 0.6013448834f;
    A06 = (B02 - B05) * 0.8999761939f; A07 = (B03 - B04) * 2.5629155636f;
    A08 = B08 + B15; A09 = B09 + B14; A10 = B10 + B13; A11 = B11 + B12;
    A12 = (B08 - B15) * 0.5097956061f; A13 = (B09 - B14) * 0.6013448834f;
    A14 = (B10 - B13) * 0.8999761939f; A15 = (B11 - B12) * 2.5629155636f;
    B00 = A00 + A03; B01 = A01 + A02;
    B02 = (A00 - A03) * 0.5411961079f; B03 = (A01 - A02) * 1.3065630198f;
    B04 = A04 + A07; B05 = A05 + A06;
    B06 = (A04 - A07) * 0.5411961079f; B07 = (A05 - A06) * 1.3065630198f;
    B08 = A08 + A11; B09 = A09 + A10;
    B10 = (A08 - A11) * 0.5411961079f; B11 = (A09 - A10) * 1.3065630198f;
    B12 = A12 + A15; B13 = A13 + A14;
    B14 = (A12 - A15) * 0.5411961079f; B15 = (A13 - A14) * 1.3065630198f;
    A00 = B00 + B01; A01 = (B00 - B01) * 0.7071067691f;
    A02 = B02 + B03; A03 = (B02 - B03) * 0.7071067691f;
    A04 = B04 + B05; A05 = (B04 - B05) * 0.7071067691f;
    A06 = B06 + B07; A07 = (B06 - B07) * 0.7071067691f;
    A08 = B08 + B09; A09 = (B08 - B09) * 0.7071067691f;
    A10 = B10 + B11; A11 = (B10 - B11) * 0.7071067691f;
    A12 = B12 + B13; A13 = (B12 - B13) * 0.7071067691f;
    A14 = B14 + B15; A15 = (B14 - B15) * 0.7071067691f;
    p[48] = -A00; p[0] = A01; p[8] = A03; p[40] = -A02 - p[8];
    p[12] = A07; p[4] = A05 + p[12]; p[36] = -(p[4] + A06);
    p[44] = -A04 - A06 - A07;
    p[14] = A15; p[10] = A11 + p[14]; p[6] = p[10] + A13;
    p[2] = A09 + A13 + A15; p[34] = -p[2] - A14; p[38] = p[34] + A09 - A10 - A11;
    tmp = -(A12 + A14 + A15); p[46] = tmp - A08; p[42] = tmp - A10 - A11;
    A00 = (in[0] - in[31]) * 0.5006030202f; A01 = (in[1] - in[30]) * 0.5054709315f;
    A02 = (in[2] - in[29]) * 0.5154473186f; A03 = (in[3] - in[28]) * 0.5310425758f;
    A04 = (in[4] - in[27]) * 0.5531039238f; A05 = (in[5] - in[26]) * 0.5829349756f;
    A06 = (in[6] - in[25]) * 0.6225041151f; A07 = (in[7] - in[24]) * 0.6748083234f;
    A08 = (in[8] - in[23]) * 0.7445362806f; A09 = (in[9] - in[22]) * 0.8393496275f;
    A10 = (in[10] - in[21]) * 0.9725682139f; A11 = (in[11] - in[20]) * 1.1694399118f;
    A12 = (in[12] - in[19]) * 1.4841645956f; A13 = (in[13] - in[18]) * 2.0577809811f;
    A14 = (in[14] - in[17]) * 3.4076085091f; A15 = (in[15] - in[16]) * 10.1900081635f;
    B00 = A00 + A15; B01 = A01 + A14; B02 = A02 + A13; B03 = A03 + A12;
    B04 = A04 + A11; B05 = A05 + A10; B06 = A06 + A09; B07 = A07 + A08;
    B08 = (A00 - A15) * 0.5024192929f; B09 = (A01 - A14) * 0.5224986076f;
    B10 = (A02 - A13) * 0.5669440627f; B11 = (A03 - A12) * 0.6468217969f;
    B12 = (A04 - A11) * 0.7881546021f; B13 = (A05 - A10) * 1.0606776476f;
    B14 = (A06 - A09) * 1.7224471569f; B15 = (A07 - A08) * 5.1011486053f;
    A00 = B00 + B07; A01 = B01 + B06; A02 = B02 + B05; A03 = B03 + B04;
    A04 = (B00 - B07) * 0.5097956061f; A05 = (B01 - B06) * 0.6013448834f;
    A06 = (B02 - B05) * 0.8999761939f; A07 = (B03 - B04) * 2.5629155636f;
    A08 = B08 + B15; A09 = B09 + B14; A10 = B10 + B13; A11 = B11 + B12;
    A12 = (B08 - B15) * 0.5097956061f; A13 = (B09 - B14) * 0.6013448834f;
    A14 = (B10 - B13) * 0.8999761939f; A15 = (B11 - B12) * 2.5629155636f;
    B00 = A00 + A03; B01 = A01 + A02;
    B02 = (A00 - A03) * 0.5411961079f; B03 = (A01 - A02) * 1.3065630198f;
    B04 = A04 + A07; B05 = A05 + A06;
    B06 = (A04 - A07) * 0.5411961079f; B07 = (A05 - A06) * 1.3065630198f;
    B08 = A08 + A11; B09 = A09 + A10;
    B10 = (A08 - A11) * 0.5411961079f; B11 = (A09 - A10) * 1.3065630198f;
    B12 = A12 + A15; B13 = A13 + A14;
    B14 = (A12 - A15) * 0.5411961079f; B15 = (A13 - A14) * 1.3065630198f;
    A00 = B00 + B01; A01 = (B00 - B01) * 0.7071067691f;
    A02 = B02 + B03; A03 = (B02 - B03) * 0.7071067691f;
    A04 = B04 + B05; A05 = (B04 - B05) * 0.7071067691f;
    A06 = B06 + B07; A07 = (B06 - B07) * 0.7071067691f;
    A08 = B08 + B09; A09 = (B08 - B09) * 0.7071067691f;
    A10 = B10 + B11; A11 = (B10 - B11) * 0.7071067691f;
    A12 = B12 + B13; A13 = (B12 - B13) * 0.7071067691f;
    A14 = B14 + B15; A15 = (B14 - B15) * 0.7071067691f;
    p[15] = A15; p[13] = A07 + p[15]; p[11] = p[13] + A11; p[5] = p[11] + A05 + A13;
    p[9] = A03 + A11 + A15; p[7] = p[9] + A13;
    p[1] = A01 + A09 + A13 + A15; p[33] = -p[1] - A14;
    p[3] = A05 + A07 + A09 + A13 + A15; p[35] = -p[3] - A06 - A14;
    tmp = -(A10 + A11 + A13 + A14 + A15); p[37] = tmp - A05 - A06 - A07; p[39] = tmp - A02 - A03;
    tmp += A13 - A12; p[41] = tmp - A02 - A03; p[43] = tmp - A04 - A06 - A07;
    tmp = -(A08 + A12 + A14 + A15); p[47] = tmp - A00; p[45] = tmp - A04 - A06 - A07;
    p[32] = -p[0]; p[31] = -p[1]; p[30] = -p[2]; p[29] = -p[3];
    p[28] = -p[4]; p[27] = -p[5]; p[26] = -p[6]; p[25] = -p[7];
    p[24] = -p[8]; p[23] = -p[9]; p[22] = -p[10]; p[21] = -p[11];
    p[20] = -p[12]; p[19] = -p[13]; p[18] = -p[14]; p[17] = -p[15];
    p[63] = p[33]; p[62] = p[34]; p[61] = p[35]; p[60] = p[36];
    p[59] = p[37]; p[58] = p[38]; p[57] = p[39]; p[56] = p[40];
    p[55] = p[41]; p[54] = p[42]; p[53] = p[43]; p[52] = p[44];
    p[51] = p[45]; p[50] = p[46]; p[49] = p[47];
}

static float di_at(int k, int j) {
    return (float)((double)DI_RAW[k][j] / 65536.0);
}

static void synth_channel(float *out, float *v, const float y[36][32], int channels) {
    int n, k;
    memmove(v + MPC_VMEM, v, MPC_VTAIL * sizeof *v);
    v += MPC_VMEM;
    for (n = 0; n < 36; n++) {
        float *base;
        v -= 64;
        new_v(y[n], v);
        base = v;
        for (k = 0; k < 32; k++) {
            float *p = base + k;
            float sum = p[0] * di_at(k, 0) + p[96] * di_at(k, 1) + p[128] * di_at(k, 2) + p[224] * di_at(k, 3)
                + p[256] * di_at(k, 4) + p[352] * di_at(k, 5) + p[384] * di_at(k, 6) + p[480] * di_at(k, 7)
                + p[512] * di_at(k, 8) + p[608] * di_at(k, 9) + p[640] * di_at(k, 10) + p[736] * di_at(k, 11)
                + p[768] * di_at(k, 12) + p[864] * di_at(k, 13) + p[896] * di_at(k, 14) + p[992] * di_at(k, 15);
            out[(size_t)n * 32u * (size_t)channels + (size_t)k * (size_t)channels] = sum;
        }
    }
}

static void synth(Mpc *s, float *out) {
    synth_channel(out, s->v_l, s->y_l, s->channels);
    if (s->channels > 1) synth_channel(out + 1, s->v_r, s->y_r, s->channels);
}

static void read_scf(Mpc *s, int32_t scf[3], int scfi) {
    int idx;
    switch (scfi) {
    case 1:
        idx = huff_dec(s, HUFF_DSCF); scf[0] = idx != 8 ? scf[2] + idx : (int32_t)bit_read(s, 6);
        idx = huff_dec(s, HUFF_DSCF); scf[1] = idx != 8 ? scf[0] + idx : (int32_t)bit_read(s, 6);
        scf[2] = scf[1];
        break;
    case 3:
        idx = huff_dec(s, HUFF_DSCF); scf[0] = idx != 8 ? scf[2] + idx : (int32_t)bit_read(s, 6);
        scf[1] = scf[0]; scf[2] = scf[1];
        break;
    case 2:
        idx = huff_dec(s, HUFF_DSCF); scf[0] = idx != 8 ? scf[2] + idx : (int32_t)bit_read(s, 6);
        scf[1] = scf[0];
        idx = huff_dec(s, HUFF_DSCF); scf[2] = idx != 8 ? scf[1] + idx : (int32_t)bit_read(s, 6);
        break;
    case 0:
        idx = huff_dec(s, HUFF_DSCF); scf[0] = idx != 8 ? scf[2] + idx : (int32_t)bit_read(s, 6);
        idx = huff_dec(s, HUFF_DSCF); scf[1] = idx != 8 ? scf[0] + idx : (int32_t)bit_read(s, 6);
        idx = huff_dec(s, HUFF_DSCF); scf[2] = idx != 8 ? scf[1] + idx : (int32_t)bit_read(s, 6);
        break;
    default:
        break;
    }
    if (scf[0] > 1024) scf[0] = 0x8080;
    if (scf[1] > 1024) scf[1] = 0x8080;
    if (scf[2] > 1024) scf[2] = 0x8080;
}

static maktabasauti_result_t read_samples(Mpc *s, int16_t q[36], int res) {
    int k, idx, sel;
    if (res < -1 || res > 17) return MAKTABASAUTI_E_CORRUPT_DATA;
    if (res == 0 || res <= -2) return MAKTABASAUTI_OK;
    if (res == -1) {
        for (k = 0; k < 36; k++) {
            uint32_t t = mpc_random(s);
            q[k] = (int16_t)(((t >> 24) & 0xFF) + ((t >> 16) & 0xFF) + ((t >> 8) & 0xFF) + (t & 0xFF) - 510);
        }
        return MAKTABASAUTI_OK;
    }
    if (res >= 8) {
        for (k = 0; k < 36; k++) q[k] = (int16_t)((int)bit_read(s, RES_BITS[res]) - DC[res]);
        return MAKTABASAUTI_OK;
    }
    sel = (int)bit_read(s, 1);
    if (res == 1) {
        for (k = 0; k < 36; k += 3) {
            idx = huff_dec(s, q_table(1, sel));
            q[k] = IDX30[idx]; q[k + 1] = IDX31[idx]; q[k + 2] = IDX32[idx];
        }
    } else if (res == 2) {
        for (k = 0; k < 36; k += 2) {
            idx = huff_dec(s, q_table(2, sel));
            q[k] = IDX50[idx]; q[k + 1] = IDX51[idx];
        }
    } else {
        const Huff *tab = q_table(res, sel);
        for (k = 0; k < 36; k++) q[k] = (int16_t)huff_dec(s, tab);
    }
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t read_body(Mpc *s) {
    int n, idx, used = 0;
    maktabasauti_result_t r;
    s->res_l[0] = (int)bit_read(s, 4);
    s->res_r[0] = (int)bit_read(s, 4);
    if (s->res_l[0] || s->res_r[0]) {
        if (s->ms) s->ms_flag[0] = (int)bit_read(s, 1);
        used = 1;
    }
    for (n = 1; n <= s->max_band; n++) {
        idx = huff_dec(s, HUFF_HDR);
        s->res_l[n] = idx != 4 ? s->res_l[n - 1] + idx : (int)bit_read(s, 4);
        idx = huff_dec(s, HUFF_HDR);
        s->res_r[n] = idx != 4 ? s->res_r[n - 1] + idx : (int)bit_read(s, 4);
        if (s->res_l[n] || s->res_r[n]) {
            if (s->ms) s->ms_flag[n] = (int)bit_read(s, 1);
            used = n + 1;
        }
    }
    for (n = 0; n < used; n++) {
        if (s->res_l[n]) s->scfi_l[n] = huff_dec(s, HUFF_SCFI);
        if (s->res_r[n]) s->scfi_r[n] = huff_dec(s, HUFF_SCFI);
    }
    for (n = 0; n < used; n++) {
        if (s->res_l[n]) read_scf(s, s->scf_l[n], s->scfi_l[n]);
        if (s->res_r[n]) read_scf(s, s->scf_r[n], s->scfi_r[n]);
    }
    for (n = 0; n < used; n++) {
        r = read_samples(s, s->q_l[n], s->res_l[n]);
        if (r != MAKTABASAUTI_OK) return r;
        r = read_samples(s, s->q_r[n], s->res_r[n]);
        if (r != MAKTABASAUTI_OK) return r;
    }
    return MAKTABASAUTI_OK;
}

static void rewind_audio(Mpc *s) {
    memset(s->scf_l, 0, sizeof s->scf_l);
    memset(s->scf_r, 0, sizeof s->scf_r);
    memset(s->q_l, 0, sizeof s->q_l);
    memset(s->q_r, 0, sizeof s->q_r);
    memset(s->v_l, 0, sizeof s->v_l);
    memset(s->v_r, 0, sizeof s->v_r);
    memset(s->ms_flag, 0, sizeof s->ms_flag);
    memset(s->res_l, 0, sizeof s->res_l);
    memset(s->res_r, 0, sizeof s->res_r);
    s->r1 = 1; s->r2 = 1;
    s->bitpos = s->audio_bit;
    s->samples = s->samples0;
    s->decoded = 0;
    s->skip = MPC_DELAY + (uint32_t)s->beg_silence;
    s->block_frames = 0;
    s->block_bits = 0;
    s->last_max_band = 0;
    memset(s->dscf_l, 0, sizeof s->dscf_l);
    memset(s->dscf_r, 0, sizeof s->dscf_r);
    s->pcm_n = s->pcm_i = 0;
    s->out_pos = 0;
    s->frames_done = 0;
    s->pend = MAKTABASAUTI_OK;
    s->err = NULL;
}

static const uint8_t SV8_LOG2[32] = {
    1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6
};
static const uint8_t SV8_LOG2_LOST[32] = {
    0, 1, 0, 3, 2, 1, 0, 7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 31
};
static const unsigned SV8_THRES[9] = {0, 0, 3, 0, 0, 1, 3, 4, 8};

static uint32_t b8_peek(const Mpc *s, unsigned n) {
    uint32_t v = 0;
    uint64_t pos = s->bitpos;
    unsigned i;
    for (i = 0; i < n && i < 32u; i++) {
        size_t off = s->bs0 + (size_t)(pos >> 3);
        unsigned bit = 0;
        if (off < s->file_n) bit = (s->file[off] >> (7u - (unsigned)(pos & 7u))) & 1u;
        v = (v << 1) | bit;
        pos++;
    }
    return v;
}

static uint32_t b8_read(Mpc *s, unsigned n) {
    uint32_t v = b8_peek(s, n);
    s->bitpos += n;
    return v;
}

static void b8_align(Mpc *s) {
    unsigned rem = (unsigned)(s->bitpos & 7u);
    if (rem) s->bitpos += 8u - rem;
}

static uint32_t png_crc(const uint8_t *p, size_t n) {
    uint32_t c = 0xffffffffu;
    size_t i;
    int k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++) c = (c & 1u) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
    }
    return c ^ 0xffffffffu;
}

static int can_dec(Mpc *s, const Huff *tab, const int8_t *sym, maktabasauti_result_t *err) {
    uint32_t code = b8_peek(s, 16);
    const Huff *t = tab;
    int guard = 0;
    int idx;
    while (code < t->code) {
        t++;
        if (++guard > 80) {
            s->err = "zly huffman SV8";
            *err = MAKTABASAUTI_E_CORRUPT_DATA;
            return 0;
        }
    }
    if (t->len > 16) {
        s->err = "zly huffman SV8";
        *err = MAKTABASAUTI_E_CORRUPT_DATA;
        return 0;
    }
    if (t->len) b8_read(s, t->len);
    idx = ((int)t->value - (int)(code >> (16u - t->len))) & 0xFF;
    return (int)sym[idx];
}

static uint32_t log_dec(Mpc *s, unsigned max) {
    uint32_t value = 0;
    if (max == 0 || max > 32) return 0;
    if (SV8_LOG2[max - 1] > 1) value = b8_read(s, SV8_LOG2[max - 1] - 1u);
    if (value >= SV8_LOG2_LOST[max - 1])
        value = ((value << 1) | b8_read(s, 1)) - SV8_LOG2_LOST[max - 1];
    return value;
}

static uint32_t enum_dec(Mpc *s, unsigned k, unsigned n, maktabasauti_result_t *err) {
    uint32_t bits = 0, code;
    unsigned row, len;
    if (k == 0 || k > 16 || n == 0 || n > 32) {
        s->err = "zla kombinacja SV8";
        *err = MAKTABASAUTI_E_CORRUPT_DATA;
        return 0;
    }
    row = k - 1;
    len = SV8_CNK_LEN[row * 32u + (n - 1u)];
    if (len == 0) {
        s->err = "zla kombinacja SV8";
        *err = MAKTABASAUTI_E_CORRUPT_DATA;
        return 0;
    }
    code = b8_read(s, len - 1u);
    if (code >= SV8_CNK_LOST[row * 32u + (n - 1u)])
        code = ((code << 1) | b8_read(s, 1)) - SV8_CNK_LOST[row * 32u + (n - 1u)];
    while (k > 0) {
        if (n == 0) {
            s->err = "zla kombinacja SV8";
            *err = MAKTABASAUTI_E_CORRUPT_DATA;
            return 0;
        }
        n--;
        if (code >= SV8_CNK[row * 32u + n]) {
            bits |= 1u << n;
            code -= SV8_CNK[row * 32u + n];
            if (k > 1) row--;
            k--;
        }
    }
    return bits;
}

static maktabasauti_result_t b8_size(Mpc *s, uint64_t end, uint64_t *out, unsigned *nbytes) {
    uint64_t size = 0;
    unsigned n = 0;
    do {
        uint32_t tmp;
        if (n >= 9 || s->bitpos + 8u > end) {
            s->err = "uciety rozmiar pakietu";
            return MAKTABASAUTI_E_TRUNCATED;
        }
        tmp = b8_read(s, 8);
        size = (size << 7) | (uint64_t)(tmp & 0x7fu);
        n++;
        if ((tmp & 0x80u) == 0) break;
    } while (1);
    *out = size;
    if (nbytes) *nbytes = n;
    return MAKTABASAUTI_OK;
}

static int key_ok(unsigned a, unsigned b) {
    return a >= 'A' && a <= 'Z' && b >= 'A' && b <= 'Z';
}

/* Czyta jeden pakiet. Przy AP zostawia bitpos na poczatku ladunku. */
static maktabasauti_result_t sv8_packet(Mpc *s, int *is_ap, uint64_t *header_bit, uint64_t *payload) {
    unsigned k0, k1, nb = 0;
    uint64_t raw = 0, hdr, end;
    maktabasauti_result_t r;
    *is_ap = 0;
    *header_bit = s->bitpos;
    end = (uint64_t)(s->file_n - s->bs0) * 8u;
    if (s->bs0 > s->file_n || s->bitpos + 16u > end) return MAKTABASAUTI_E_TRUNCATED;
    k0 = b8_read(s, 8);
    k1 = b8_read(s, 8);
    if (!key_ok(k0, k1)) {
        s->err = "zly klucz pakietu";
        return MAKTABASAUTI_E_CORRUPT_DATA;
    }
    r = b8_size(s, end, &raw, &nb);
    if (r != MAKTABASAUTI_OK) return r;
    hdr = 2u + (uint64_t)nb;
    if (raw < hdr) {
        s->err = "pakiet krotszy niz naglowek";
        return MAKTABASAUTI_E_CORRUPT_DATA;
    }
    *payload = raw - hdr;
    if (k0 == 'S' && k1 == 'E') return MAKTABASAUTI_EOF;
    if (s->bitpos / 8u + *payload > (uint64_t)(s->file_n - s->bs0)) return MAKTABASAUTI_E_TRUNCATED;
    if (k0 == 'A' && k1 == 'P') *is_ap = 1;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t parse_sh(Mpc *s, uint64_t payload) {
    uint64_t end, samples, beg;
    size_t byte0;
    uint32_t got, rate_i;
    int mb, ch;
    maktabasauti_result_t r;
    if (payload < 4) {
        s->err = "SH bez CRC";
        return MAKTABASAUTI_E_CORRUPT_DATA;
    }
    byte0 = s->bs0 + (size_t)(s->bitpos >> 3);
    if (((s->bitpos & 7u) != 0) || byte0 + (size_t)payload > s->file_n) return MAKTABASAUTI_E_TRUNCATED;
    got = (b8_read(s, 16) << 16) | b8_read(s, 16);
    if (png_crc(s->file + byte0 + 4, (size_t)payload - 4u) != got) {
        s->err = "zle CRC SH";
        return MAKTABASAUTI_E_CORRUPT_DATA;
    }
    end = (s->bitpos - 32u) + payload * 8u;
    if (s->bitpos + 8u > end) return MAKTABASAUTI_E_TRUNCATED;
    if (b8_read(s, 8) != 8) {
        s->err = "zla wersja SV8";
        return MAKTABASAUTI_E_CORRUPT_DATA;
    }
    r = b8_size(s, end, &samples, NULL);
    if (r != MAKTABASAUTI_OK) return r;
    r = b8_size(s, end, &beg, NULL);
    if (r != MAKTABASAUTI_OK) return r;
    if (s->bitpos + 16u > end) return MAKTABASAUTI_E_TRUNCATED;
    rate_i = b8_read(s, 3);
    mb = (int)b8_read(s, 5) + 1;
    ch = (int)b8_read(s, 4) + 1;
    s->ms = (int)b8_read(s, 1);
    s->block_pwr = (int)b8_read(s, 3) * 2;
    if (rate_i > 3 || mb <= 0 || mb >= 32) {
        s->err = "zly naglowek SV8";
        return MAKTABASAUTI_E_CORRUPT_DATA;
    }
    if (ch != 1 && ch != 2) {
        s->err = "SV8 tylko mono i stereo";
        return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    }
    if (samples == 0 || beg > samples || samples > 10000000ull * (uint64_t)MPC_FRAME) {
        s->err = "zla dlugosc SV8";
        return MAKTABASAUTI_E_CORRUPT_DATA;
    }
    if (samples - beg == 0) return MAKTABASAUTI_E_CORRUPT_DATA;
    if (beg > (uint64_t)UINT32_MAX - (uint64_t)MPC_DELAY) return MAKTABASAUTI_E_CORRUPT_DATA;
    s->channels = ch;
    s->max_band = mb;
    s->rate = RATES[rate_i];
    s->beg_silence = beg;
    s->samples0 = samples;
    s->audible = samples - beg;
    s->frames_hdr = (uint32_t)((samples + (uint64_t)MPC_FRAME - 1u) / (uint64_t)MPC_FRAME);
    s->gapless = 1;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t scf_sv8(Mpc *s, int32_t scf[3], int scfi, uint8_t *flag) {
    maktabasauti_result_t e = MAKTABASAUTI_OK;
    int m, tmp;
    if (*flag) {
        scf[0] = (int32_t)b8_read(s, 7) - 6;
        *flag = 0;
    } else {
        tmp = can_dec(s, SV8H_DSCF_2, SV8S_DSCF_2, &e);
        if (e != MAKTABASAUTI_OK) return e;
        if (tmp == 64) tmp += (int)b8_read(s, 6);
        scf[0] = (int32_t)(((uint32_t)(scf[2] - 25 + tmp) & 127u)) - 6;
    }
    for (m = 0; m < 2; m++) {
        if (((scfi << m) & 2) == 0) {
            tmp = can_dec(s, SV8H_DSCF_1, SV8S_DSCF_1, &e);
            if (e != MAKTABASAUTI_OK) return e;
            if (tmp == 31) tmp = 64 + (int)b8_read(s, 6);
            scf[m + 1] = (int32_t)(((uint32_t)(scf[m] - 25 + tmp) & 127u)) - 6;
        } else scf[m + 1] = scf[m];
    }
    return MAKTABASAUTI_OK;
}

static void split_nib(int sym, int16_t *a, int16_t *b) {
    uint8_t u = (uint8_t)sym;
    *a = (int16_t)((int8_t)(u << 4) >> 4);
    *b = (int16_t)((int8_t)u >> 4);
}

static void qtab_sv8(int res, int side, const Huff **h, const int8_t **sym) {
    switch (res) {
    case 5: *h = side ? SV8H_Q5_2 : SV8H_Q5_1; *sym = side ? SV8S_Q5_2 : SV8S_Q5_1; break;
    case 6: *h = side ? SV8H_Q6_2 : SV8H_Q6_1; *sym = side ? SV8S_Q6_2 : SV8S_Q6_1; break;
    case 7: *h = side ? SV8H_Q7_2 : SV8H_Q7_1; *sym = side ? SV8S_Q7_2 : SV8S_Q7_1; break;
    default: *h = side ? SV8H_Q8_2 : SV8H_Q8_1; *sym = side ? SV8S_Q8_2 : SV8S_Q8_1; break;
    }
}

static maktabasauti_result_t samples_sv8(Mpc *s, int16_t q[36], int res) {
    maktabasauti_result_t e = MAKTABASAUTI_OK;
    int k;
    if (res == 0) return MAKTABASAUTI_OK;
    if (res < -1 || res > 15) return MAKTABASAUTI_E_CORRUPT_DATA;
    if (res == -1) {
        for (k = 0; k < 36; k++) {
            uint32_t t = mpc_random(s);
            q[k] = (int16_t)(((t >> 24) & 0xFF) + ((t >> 16) & 0xFF) + ((t >> 8) & 0xFF) + (t & 0xFF) - 510);
        }
        return MAKTABASAUTI_OK;
    }
    if (res == 1) {
        for (k = 0; k < 36; ) {
            int cnt = can_dec(s, SV8H_Q1, SV8S_Q1, &e);
            uint32_t idx = 0;
            int j, kmax;
            if (e != MAKTABASAUTI_OK) return e;
            if (cnt < 0 || cnt > 18) return MAKTABASAUTI_E_CORRUPT_DATA;
            if (cnt > 0 && cnt < 18) {
                unsigned kk = (unsigned)(cnt <= 9 ? cnt : 18 - cnt);
                idx = enum_dec(s, kk, 18, &e);
                if (e != MAKTABASAUTI_OK) return e;
            }
            if (cnt > 9) idx = ~idx;
            kmax = k + 18;
            for (j = k; j < kmax; j++) {
                q[j] = 0;
                if (idx & (1u << 17)) q[j] = (int16_t)((b8_read(s, 1) << 1) - 1);
                idx <<= 1;
            }
            k = kmax;
        }
        return MAKTABASAUTI_OK;
    }
    if (res == 2) {
        int idx = (int)(2u * SV8_THRES[2]);
        for (k = 0; k < 36; k += 3) {
            int tmp = can_dec(s, idx > (int)SV8_THRES[2] ? SV8H_Q2_2 : SV8H_Q2_1,
                              idx > (int)SV8_THRES[2] ? SV8S_Q2_2 : SV8S_Q2_1, &e);
            if (e != MAKTABASAUTI_OK) return e;
            if (tmp < 0 || tmp >= 125) return MAKTABASAUTI_E_CORRUPT_DATA;
            q[k] = SV8_IDX50[tmp];
            q[k + 1] = SV8_IDX51[tmp];
            q[k + 2] = SV8_IDX52[tmp];
            idx = (idx >> 1) + (int)SV8_HUFFQ2_VAR[tmp];
        }
        return MAKTABASAUTI_OK;
    }
    if (res <= 4) {
        const Huff *h = res == 3 ? SV8H_Q3 : SV8H_Q4;
        const int8_t *sy = res == 3 ? SV8S_Q3 : SV8S_Q4;
        for (k = 0; k < 36; k += 2) {
            int sym = can_dec(s, h, sy, &e);
            if (e != MAKTABASAUTI_OK) return e;
            split_nib(sym, &q[k], &q[k + 1]);
        }
        return MAKTABASAUTI_OK;
    }
    if (res <= 8) {
        int idx = (int)(2u * SV8_THRES[res]);
        for (k = 0; k < 36; k++) {
            const Huff *h;
            const int8_t *sy;
            int sample;
            qtab_sv8(res, idx > (int)SV8_THRES[res], &h, &sy);
            sample = can_dec(s, h, sy, &e);
            if (e != MAKTABASAUTI_OK) return e;
            q[k] = (int16_t)sample;
            idx = (idx >> 1) + (sample < 0 ? -sample : sample);
        }
        return MAKTABASAUTI_OK;
    }
    for (k = 0; k < 36; k++) {
        int sym = can_dec(s, SV8H_Q9up, SV8S_Q9up, &e);
        int qv;
        if (e != MAKTABASAUTI_OK) return e;
        qv = (unsigned char)sym;
        if (res != 9) qv = (qv << (res - 9)) | (int)b8_read(s, (unsigned)(res - 9));
        q[k] = (int16_t)(qv - DC[res]);
    }
    return MAKTABASAUTI_OK;
}

static int adj_res(int v) { return v > 15 ? v - 17 : v; }

static maktabasauti_result_t read_body_sv8(Mpc *s, int key) {
    maktabasauti_result_t e = MAKTABASAUTI_OK;
    int used, n;
    if (key) used = (int)log_dec(s, (unsigned)s->max_band + 1u);
    else {
        int sym = can_dec(s, SV8H_Bands, SV8S_Bands, &e);
        if (e != MAKTABASAUTI_OK) return e;
        used = s->last_max_band + sym;
        if (used > 32) used -= 33;
    }
    if (used < 0 || used > 32) {
        s->err = "zly zakres pasm SV8";
        return MAKTABASAUTI_E_CORRUPT_DATA;
    }
    s->last_max_band = used;
    if (used) {
        int top = used - 1;
        s->res_l[top] = adj_res(can_dec(s, SV8H_Res_1, SV8S_Res_1, &e));
        if (e != MAKTABASAUTI_OK) return e;
        s->res_r[top] = adj_res(can_dec(s, SV8H_Res_1, SV8S_Res_1, &e));
        if (e != MAKTABASAUTI_OK) return e;
        for (n = top - 1; n >= 0; n--) {
            const Huff *hl = s->res_l[n + 1] > 2 ? SV8H_Res_2 : SV8H_Res_1;
            const int8_t *sl = s->res_l[n + 1] > 2 ? SV8S_Res_2 : SV8S_Res_1;
            const Huff *hr = s->res_r[n + 1] > 2 ? SV8H_Res_2 : SV8H_Res_1;
            const int8_t *sr = s->res_r[n + 1] > 2 ? SV8S_Res_2 : SV8S_Res_1;
            s->res_l[n] = adj_res(can_dec(s, hl, sl, &e) + s->res_l[n + 1]);
            if (e != MAKTABASAUTI_OK) return e;
            s->res_r[n] = adj_res(can_dec(s, hr, sr, &e) + s->res_r[n + 1]);
            if (e != MAKTABASAUTI_OK) return e;
        }
        for (n = 0; n < used; n++) {
            if (s->res_l[n] < -1 || s->res_l[n] > 15 || s->res_r[n] < -1 || s->res_r[n] > 15)
                return MAKTABASAUTI_E_CORRUPT_DATA;
        }
        if (s->ms) {
            unsigned tot = 0, cnt;
            uint32_t tmp = 0;
            for (n = 0; n < used; n++) if (s->res_l[n] || s->res_r[n]) tot++;
            cnt = log_dec(s, tot);
            if (cnt != 0 && cnt != tot) {
                unsigned kk = cnt < tot - cnt ? cnt : tot - cnt;
                tmp = enum_dec(s, kk, tot, &e);
                if (e != MAKTABASAUTI_OK) return e;
            }
            if (cnt * 2u > tot) tmp = ~tmp;
            for (n = used - 1; n >= 0; n--) {
                if (s->res_l[n] || s->res_r[n]) {
                    s->ms_flag[n] = (int)(tmp & 1u);
                    tmp >>= 1;
                }
            }
        }
    }
    for (n = used; n <= s->max_band; n++) s->res_l[n] = s->res_r[n] = 0;
    if (key) {
        for (n = 0; n < 32; n++) s->dscf_l[n] = s->dscf_r[n] = 1;
    }
    for (n = 0; n < used; n++) {
        int tmp = 0, cnt = -1;
        if (s->res_l[n]) cnt++;
        if (s->res_r[n]) cnt++;
        if (cnt >= 0) {
            tmp = can_dec(s, cnt ? SV8H_SCFI_2 : SV8H_SCFI_1, cnt ? SV8S_SCFI_2 : SV8S_SCFI_1, &e);
            if (e != MAKTABASAUTI_OK) return e;
            if (s->res_l[n]) s->scfi_l[n] = tmp >> (2 * cnt);
            if (s->res_r[n]) s->scfi_r[n] = tmp & 3;
        }
    }
    for (n = 0; n < used; n++) {
        if (s->res_l[n]) {
            e = scf_sv8(s, s->scf_l[n], s->scfi_l[n], &s->dscf_l[n]);
            if (e != MAKTABASAUTI_OK) return e;
        }
        if (s->res_r[n]) {
            e = scf_sv8(s, s->scf_r[n], s->scfi_r[n], &s->dscf_r[n]);
            if (e != MAKTABASAUTI_OK) return e;
        }
    }
    for (n = 0; n < used; n++) {
        e = samples_sv8(s, s->q_l[n], s->res_l[n]);
        if (e != MAKTABASAUTI_OK) return e;
        e = samples_sv8(s, s->q_r[n], s->res_r[n]);
        if (e != MAKTABASAUTI_OK) return e;
    }
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t sv8_open_frame(Mpc *s, int *key) {
    if (s->block_frames > 0) {
        *key = 0;
        return MAKTABASAUTI_OK;
    }
    b8_align(s);
    for (;;) {
        int is_ap = 0;
        uint64_t header_bit = 0, payload = 0;
        maktabasauti_result_t r = sv8_packet(s, &is_ap, &header_bit, &payload);
        (void)header_bit;
        if (r != MAKTABASAUTI_OK) return r;
        if (is_ap) {
            if (payload > (uint64_t)INT64_MAX / 8u) return MAKTABASAUTI_E_CORRUPT_DATA;
            s->block_bits = (int64_t)payload * 8;
            s->block_frames = 1 << s->block_pwr;
            *key = 1;
            return MAKTABASAUTI_OK;
        }
        s->bitpos += payload * 8u;
    }
}

static maktabasauti_result_t pull_sv8(Mpc *s) {
    int64_t samples_left;
    int key = 0, nout, i, ch;
    uint64_t bit0;
    maktabasauti_result_t r;
    float frame[MPC_FRAME * MPC_CH];
    if (s->out_pos >= s->audible) return MAKTABASAUTI_EOF;
    samples_left = (int64_t)s->samples - (int64_t)s->decoded + (int64_t)MPC_DELAY;
    if (samples_left <= 0) return MAKTABASAUTI_EOF;
    r = sv8_open_frame(s, &key);
    if (r != MAKTABASAUTI_OK) return r;
    bit0 = s->bitpos;
    r = read_body_sv8(s, key);
    if (r != MAKTABASAUTI_OK) return r;
    ch = s->channels > 0 ? s->channels : 1;
    memset(frame, 0, sizeof frame);
    if (s->skip < (uint32_t)MPC_FRAME + (uint32_t)MPC_DELAY) {
        requant(s);
        synth(s, frame);
    }
    s->decoded += (uint64_t)MPC_FRAME;
    s->block_bits -= (int64_t)(s->bitpos - bit0);
    s->block_frames--;
    if (s->block_bits < 0 || (s->block_frames == 0 && s->block_bits > 7)) {
        s->err = "pakiet AP nie zgadza sie z bitami";
        return MAKTABASAUTI_E_CORRUPT_DATA;
    }
    nout = samples_left > MPC_FRAME ? MPC_FRAME : samples_left < 0 ? 0 : (int)samples_left;
    if (s->skip) {
        if (nout <= (int)s->skip) {
            s->skip -= (uint32_t)nout;
            nout = 0;
        } else {
            memmove(frame, frame + (size_t)s->skip * (size_t)ch,
                    (size_t)(nout - (int)s->skip) * (size_t)ch * sizeof(float));
            nout -= (int)s->skip;
            s->skip = 0;
        }
    }
    if ((uint64_t)nout > s->audible - s->out_pos) nout = (int)(s->audible - s->out_pos);
    for (i = 0; i < nout * ch; i++) s->pcm[i] = frame[i];
    s->pcm_n = (size_t)nout;
    s->pcm_i = 0;
    s->out_pos += (uint64_t)nout;
    s->frames_done++;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t pull_frame(Mpc *s) {
    int64_t samples_left;
    if (s->ver == 8) return pull_sv8(s);
    int extra11;
    uint32_t stored;
    uint64_t bit0, consumed;
    maktabasauti_result_t r;
    int nout, i;
    float frame[MPC_FRAME * MPC_CH];
    if (s->out_pos >= s->audible) return MAKTABASAUTI_EOF;
    samples_left = (int64_t)s->samples - (int64_t)s->decoded + (int64_t)MPC_DELAY;
    if (samples_left <= 0) return MAKTABASAUTI_EOF;
    extra11 = 1152 > (s->samples - s->decoded - 1u);
    stored = bit_read(s, 20);
    if (stored > 34816u) { s->err = "zla dlugosc ramki"; return MAKTABASAUTI_E_CORRUPT_DATA; }
    bit0 = s->bitpos;
    r = read_body(s);
    if (r != MAKTABASAUTI_OK) return r;
    requant(s);
    synth(s, frame);
    s->decoded += (uint64_t)MPC_FRAME;
    if (s->decoded - s->samples < (uint64_t)MPC_FRAME) {
        int last = (int)bit_read(s, 11);
        if (s->decoded == s->samples) {
            if (last == 0) last = MPC_FRAME;
            if (last > MPC_FRAME) { s->err = "zla dlugosc ostatniej ramki"; return MAKTABASAUTI_E_CORRUPT_DATA; }
            s->samples = (uint64_t)((int64_t)s->samples + last - MPC_FRAME);
            samples_left += last - MPC_FRAME;
        }
    }
    consumed = s->bitpos - bit0;
    if (consumed != (uint64_t)stored + (extra11 ? 11u : 0u)) {
        s->err = "ramka nie zgadza sie z dlugoscia";
        return MAKTABASAUTI_E_CORRUPT_DATA;
    }
    nout = samples_left > MPC_FRAME ? MPC_FRAME : samples_left < 0 ? 0 : (int)samples_left;
    if (s->skip) {
        if (nout <= (int)s->skip) { s->skip -= (uint32_t)nout; nout = 0; }
        else {
            memmove(frame, frame + (size_t)s->skip * MPC_CH, (size_t)(nout - (int)s->skip) * MPC_CH * sizeof(float));
            nout -= (int)s->skip;
            s->skip = 0;
        }
    }
    if ((uint64_t)nout > s->audible - s->out_pos) nout = (int)(s->audible - s->out_pos);
    for (i = 0; i < nout * MPC_CH; i++) s->pcm[i] = frame[i];
    s->pcm_n = (size_t)nout;
    s->pcm_i = 0;
    s->out_pos += (uint64_t)nout;
    s->frames_done++;
    return MAKTABASAUTI_OK;
}

static size_t id3_skip(const uint8_t *p, size_t n) {
    uint32_t size;
    if (n < 10 || memcmp(p, "ID3", 3) != 0) return 0;
    if (p[5] & 0x0F) return (size_t)-1;
    if ((p[6] | p[7] | p[8] | p[9]) & 0x80) return (size_t)-1;
    size = ((uint32_t)p[6] << 21) | ((uint32_t)p[7] << 14) | ((uint32_t)p[8] << 7) | p[9];
    size += 10u;
    if (p[5] & 0x10) size += 10u;
    return size;
}

static maktabasauti_result_t parse_header(Mpc *s) {
    size_t skip;
    uint32_t frames, last, rate_i;
    int max_band;
    if (s->file_n < 8) return MAKTABASAUTI_E_TRUNCATED;
    skip = id3_skip(s->file, s->file_n);
    if (skip == (size_t)-1) { s->err = "zly ID3v2"; return MAKTABASAUTI_E_CORRUPT_DATA; }
    if (skip > s->file_n || s->file_n - skip < 4) return MAKTABASAUTI_E_TRUNCATED;
    if (memcmp(s->file + skip, "MPCK", 4) == 0) {
        int got_sh = 0;
        s->ver = 8;
        s->bs0 = skip;
        s->bitpos = 32;
        for (;;) {
            int is_ap = 0;
            uint64_t header_bit = 0, payload = 0, at;
            size_t off;
            maktabasauti_result_t pr = sv8_packet(s, &is_ap, &header_bit, &payload);
            if (pr == MAKTABASAUTI_EOF) {
                s->err = "strumien bez audio";
                return MAKTABASAUTI_E_CORRUPT_DATA;
            }
            if (pr != MAKTABASAUTI_OK) return pr;
            at = s->bitpos;
            off = s->bs0 + (size_t)(header_bit >> 3);
            if (!is_ap && off + 2 <= s->file_n && s->file[off] == 'S' && s->file[off + 1] == 'H') {
                pr = parse_sh(s, payload);
                if (pr != MAKTABASAUTI_OK) return pr;
                got_sh = 1;
            }
            if (is_ap) {
                if (!got_sh) {
                    s->err = "brak SH";
                    return MAKTABASAUTI_E_CORRUPT_DATA;
                }
                s->audio_bit = header_bit;
                break;
            }
            s->bitpos = at + payload * 8u;
        }
        build_scf(s);
        rewind_audio(s);
        return MAKTABASAUTI_OK;
    }
    if (memcmp(s->file + skip, "MP+", 3) != 0 || (s->file[skip + 3] & 15) != 7)
        return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    s->ver = 7;
    s->channels = MPC_CH;
    s->bs0 = skip + 4;
    s->bitpos = 0;
    frames = (bit_read(s, 16) << 16) | bit_read(s, 16);
    (void)bit_read(s, 1); /* intensity stereo: referencja nie uzywa tego bitu */
    s->ms = (int)bit_read(s, 1);
    max_band = (int)bit_read(s, 6);
    (void)bit_read(s, 4); /* profil */
    (void)bit_read(s, 2); /* link */
    rate_i = bit_read(s, 2);
    (void)bit_read(s, 16); /* stary peak */
    (void)bit_read(s, 16); (void)bit_read(s, 16); (void)bit_read(s, 16); (void)bit_read(s, 16); /* ReplayGain, nie stosowany */
    s->gapless = (int)bit_read(s, 1);
    last = bit_read(s, 11);
    (void)bit_read(s, 1); /* fast seek */
    (void)bit_read(s, 19);
    (void)bit_read(s, 8); /* wersja kodera */
    if (s->bitpos != 168) return MAKTABASAUTI_E_CORRUPT_DATA;
    if (max_band <= 0 || max_band >= 32 || frames == 0 || frames > 10000000u) {
        s->err = "zly naglowek SV7";
        return MAKTABASAUTI_E_CORRUPT_DATA;
    }
    if (last == 0) last = MPC_FRAME;
    if (last > MPC_FRAME) return MAKTABASAUTI_E_CORRUPT_DATA;
    s->max_band = max_band;
    s->rate = RATES[rate_i];
    s->frames_hdr = frames;
    s->audible = (uint64_t)frames * (uint64_t)MPC_FRAME;
    if (s->gapless) s->audible -= (uint64_t)MPC_FRAME - last;
    else {
        if (s->audible < MPC_DELAY) return MAKTABASAUTI_E_CORRUPT_DATA;
        s->audible -= MPC_DELAY;
    }
    if (s->audible == 0) return MAKTABASAUTI_E_CORRUPT_DATA;
    if (s->gapless) s->samples0 = ((s->audible + MPC_FRAME - 1) / MPC_FRAME) * MPC_FRAME;
    else s->samples0 = s->audible;
    s->audio_bit = s->bitpos;
    build_scf(s);
    rewind_audio(s);
    return MAKTABASAUTI_OK;
}

static void mpc_state_free(Mpc *s) {
    const maktabasauti_host_t *h;
    if (!s) return;
    h = s->host;
    if (s->file) h->free(h->user, s->file);
    h->free(h->user, s);
}

static maktabasauti_result_t load_file(const maktabasauti_host_t *host, maktabasauti_reader_t *rd, Mpc *s) {
    size_t cap = 4096, len = 0;
    uint8_t *buf = (uint8_t *)host->alloc(host->user, cap);
    if (!buf) return MAKTABASAUTI_E_NO_MEMORY;
    for (;;) {
        int64_t n;
        if (len == cap) {
            size_t ncap = cap * 2u;
            uint8_t *grown;
            if (ncap < cap) { host->free(host->user, buf); return MAKTABASAUTI_E_NO_MEMORY; }
            grown = (uint8_t *)host->realloc(host->user, buf, ncap);
            if (!grown) { host->free(host->user, buf); return MAKTABASAUTI_E_NO_MEMORY; }
            buf = grown; cap = ncap;
        }
        n = host->read(host->user, rd, buf + len, cap - len);
        if (n < 0) { host->free(host->user, buf); return (maktabasauti_result_t)n; }
        if (n == 0) break;
        len += (size_t)n;
    }
    if (len == 0) { host->free(host->user, buf); return MAKTABASAUTI_E_TRUNCATED; }
    s->file = buf; s->file_n = len;
    return MAKTABASAUTI_OK;
}

static void fill_info(const Mpc *s, maktabasauti_stream_info_t *info) {
    uint64_t bits;
    memset(info, 0, sizeof *info);
    info->struct_size = sizeof *info;
    info->sample_rate = s->rate;
    info->channels = (uint32_t)s->channels;
    info->flags = MAKTABASAUTI_STREAM_SEEKABLE | MAKTABASAUTI_STREAM_VBR | MAKTABASAUTI_STREAM_GAPLESS_TRIMMED;
    info->channel_mask = s->channels == 1 ? MAKTABASAUTI_LAYOUT_MONO : MAKTABASAUTI_LAYOUT_STEREO;
    info->native_format = MAKTABASAUTI_FORMAT_F32;
    info->bits_per_sample = 0;
    info->total_frames = s->audible;
    info->priming_frames = MPC_DELAY + (uint32_t)s->beg_silence;
    info->end_padding_frames = 0;
    bits = (uint64_t)s->file_n * 8u * s->rate;
    info->bitrate = s->audible ? (uint32_t)(bits / s->audible) : 0;
    memcpy(info->codec_name, "musepack", 9);
    memcpy(info->codec_profile, s->ver == 8 ? "SV8" : "SV7", 4);
    memcpy(info->container_name, "mpc", 4);
}

static int32_t MAKTABASAUTI_CALL mpc_probe(const maktabasauti_codec_t *self, const uint8_t *prefix, size_t n, const char *name_hint) {
    size_t skip;
    (void)self; (void)name_hint;
    if (!prefix || n < 4) return 0;
    skip = id3_skip(prefix, n);
    if (skip == (size_t)-1 || skip > n || n - skip < 4) return 0;
    if (memcmp(prefix + skip, "MP+", 3) == 0 && (prefix[skip + 3] & 15) == 7) return 90;
    if (memcmp(prefix + skip, "MPCK", 4) == 0) return 90;
    return 0;
}

static maktabasauti_result_t MAKTABASAUTI_CALL mpc_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *host,
                                                       maktabasauti_reader_t *reader, maktabasauti_stream_info_t *info, void **state) {
    Mpc *s;
    maktabasauti_result_t r;
    (void)self;
    *state = NULL;
    if (!host || !host->alloc || !host->free || !host->read || !host->realloc || !info) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    s = (Mpc *)host->alloc(host->user, sizeof *s);
    if (!s) return MAKTABASAUTI_E_NO_MEMORY;
    memset(s, 0, sizeof *s);
    s->host = host;
    r = load_file(host, reader, s);
    if (r != MAKTABASAUTI_OK) { host->free(host->user, s); return r; }
    r = parse_header(s);
    if (r != MAKTABASAUTI_OK) { mpc_state_free(s); return r; }
    fill_info(s, info);
    *state = s;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL mpc_read(void *state, void *dst, size_t dst_frames, size_t *frames) {
    Mpc *s = (Mpc *)state;
    float *o = (float *)dst;
    size_t done = 0;
    *frames = 0;
    if (s->pend != MAKTABASAUTI_OK) return s->pend;
    while (done < dst_frames) {
        size_t have, take, i;
        if (s->pcm_i >= s->pcm_n) {
            maktabasauti_result_t r;
            if (s->frames_done > s->frames_hdr + 8u) { s->err = "za duzo ramek"; s->pend = MAKTABASAUTI_E_CORRUPT_DATA; break; }
            r = pull_frame(s);
            if (r != MAKTABASAUTI_OK) {
                if (r != MAKTABASAUTI_EOF) s->pend = r;
                break;
            }
            continue;
        }
        have = s->pcm_n - s->pcm_i;
        take = dst_frames - done;
        if (take > have) take = have;
        for (i = 0; i < take * (size_t)s->channels; i++) o[done * (size_t)s->channels + i] = s->pcm[s->pcm_i * (size_t)s->channels + i];
        s->pcm_i += take;
        done += take;
    }
    *frames = done;
    if (done) return MAKTABASAUTI_OK;
    if (s->pend != MAKTABASAUTI_OK) return s->pend;
    return MAKTABASAUTI_EOF;
}

static maktabasauti_result_t MAKTABASAUTI_CALL mpc_seek(void *state, uint64_t frame, uint32_t flags, uint64_t *actual) {
    Mpc *s = (Mpc *)state;
    (void)flags;
    if (frame > s->audible) return MAKTABASAUTI_E_OUT_OF_RANGE;
    rewind_audio(s);
    while (s->out_pos < frame) {
        maktabasauti_result_t r = pull_frame(s);
        if (r != MAKTABASAUTI_OK) return r;
        if (s->out_pos >= frame) {
            size_t back = (size_t)(s->out_pos - frame);
            if (back > s->pcm_n) return MAKTABASAUTI_E_CORRUPT_DATA;
            s->pcm_i = s->pcm_n - back;
            s->out_pos = frame;
            break;
        }
    }
    *actual = frame;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL mpc_reset(void *state) {
    rewind_audio((Mpc *)state);
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL mpc_flush(void *state) {
    ((Mpc *)state)->pend = MAKTABASAUTI_OK;
    return MAKTABASAUTI_OK;
}

static const char *MAKTABASAUTI_CALL mpc_last_error(const void *state) { return ((const Mpc *)state)->err; }

static void MAKTABASAUTI_CALL mpc_close(void *state) { if (state) mpc_state_free((Mpc *)state); }

const maktabasauti_codec_t maktabasauti_mpc_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "musepack", "Musepack SV7/SV8", "maktabasauti_mpc",
    MAKTABASAUTI_CODEC_FLAG_STREAMING | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0, NULL,
    mpc_probe, mpc_open, mpc_read, mpc_seek, mpc_reset, mpc_flush, NULL, mpc_last_error, NULL, mpc_close, { NULL }, { NULL }
};
