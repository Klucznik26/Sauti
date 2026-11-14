/* SBR-07 exhaustive CODEWORD fixtures, not a decoder or a shared
 * lookup into the implementation. Expected codewords were generated
 * independently from MPEG-4 SBR tables and cross-checked with FDK-AAC
 * tree traversal: 604/604 exact code-length and bit-pattern matches.
 * Each fixture packs length in bits 20..24 and codeword in 0..19. */
#include "sauti_aac_sbr_huffman.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { unsigned begin, count, lav; } RefBook;
static const RefBook books[SAUTI_SBR_HUFF_TABLE_COUNT] = {
    { 0u, 121u, 60 },
    { 121u, 121u, 60 },
    { 242u, 49u, 24 },
    { 291u, 49u, 24 },
    { 340u, 63u, 31 },
    { 403u, 63u, 31 },
    { 466u, 25u, 12 },
    { 491u, 25u, 12 },
    { 516u, 63u, 31 },
    { 579u, 25u, 12 },
};
static const uint32_t refwords[] = {
    /* t_huffman_env_1_5dB */
    0x123ffd6u, 0x123ffd7u, 0x123ffd8u, 0x123ffd9u, 0x123ffdau, 0x123ffdbu, 0x137ffb8u, 0x137ffb9u,
    0x137ffbau, 0x137ffbbu, 0x137ffbcu, 0x137ffbdu, 0x137ffbeu, 0x137ffbfu, 0x137ffc0u, 0x137ffc1u,
    0x137ffc2u, 0x137ffc3u, 0x137ffc4u, 0x137ffc5u, 0x137ffc6u, 0x137ffc7u, 0x137ffc8u, 0x137ffc9u,
    0x137ffcau, 0x137ffcbu, 0x137ffccu, 0x137ffcdu, 0x137ffceu, 0x137ffcfu, 0x137ffd0u, 0x137ffd1u,
    0x137ffd2u, 0x137ffd3u, 0x111ffe6u, 0x123ffd4u, 0x100fff0u, 0x111ffe9u, 0x123ffd5u, 0x111ffe7u,
    0x100fff1u, 0x100ffecu, 0x100ffedu, 0x100ffeeu, 0x0f07ff4u, 0x0e03ff9u, 0x0e03ff7u, 0x0d01ffau,
    0x0d01ff9u, 0x0c00ffbu, 0x0b007fcu, 0x0a003fcu, 0x09001fdu, 0x08000fdu, 0x070007du, 0x060003du,
    0x050001du, 0x040000du, 0x0300005u, 0x0200001u, 0x0200000u, 0x0300004u, 0x040000cu, 0x050001cu,
    0x060003cu, 0x070007cu, 0x08000fcu, 0x09001fcu, 0x0a003fdu, 0x0c00ffau, 0x0d01ff8u, 0x0e03ff6u,
    0x0e03ff8u, 0x0f07ff5u, 0x100ffefu, 0x111ffe8u, 0x100fff2u, 0x137ffd4u, 0x137ffd5u, 0x137ffd6u,
    0x137ffd7u, 0x137ffd8u, 0x137ffd9u, 0x137ffdau, 0x137ffdbu, 0x137ffdcu, 0x137ffddu, 0x137ffdeu,
    0x137ffdfu, 0x137ffe0u, 0x137ffe1u, 0x137ffe2u, 0x137ffe3u, 0x137ffe4u, 0x137ffe5u, 0x137ffe6u,
    0x137ffe7u, 0x137ffe8u, 0x137ffe9u, 0x137ffeau, 0x137ffebu, 0x137ffecu, 0x137ffedu, 0x137ffeeu,
    0x137ffefu, 0x137fff0u, 0x137fff1u, 0x137fff2u, 0x137fff3u, 0x137fff4u, 0x137fff5u, 0x137fff6u,
    0x137fff7u, 0x137fff8u, 0x137fff9u, 0x137fffau, 0x137fffbu, 0x137fffcu, 0x137fffdu, 0x137fffeu,
    0x137ffffu,
    /* f_huffman_env_1_5dB */
    0x137ffe7u, 0x137ffe8u, 0x14fffd2u, 0x14fffd3u, 0x14fffd4u, 0x14fffd5u, 0x14fffd6u, 0x14fffd7u,
    0x14fffd8u, 0x137ffdau, 0x14fffd9u, 0x14fffdau, 0x14fffdbu, 0x14fffdcu, 0x137ffdbu, 0x14fffddu,
    0x137ffdcu, 0x137ffddu, 0x14fffdeu, 0x123ffe4u, 0x14fffdfu, 0x14fffe0u, 0x14fffe1u, 0x137ffdeu,
    0x14fffe2u, 0x14fffe3u, 0x14fffe4u, 0x137ffdfu, 0x14fffe5u, 0x137ffe0u, 0x123ffe8u, 0x137ffe1u,
    0x123ffe0u, 0x123ffe9u, 0x111ffefu, 0x123ffe5u, 0x111ffecu, 0x111ffedu, 0x111ffeeu, 0x100fff4u,
    0x100fff3u, 0x100fff0u, 0x0f07ff7u, 0x0f07ff6u, 0x0e03ffau, 0x0d01ffau, 0x0d01ff9u, 0x0c00ffau,
    0x0c00ff8u, 0x0b007f9u, 0x0a003fbu, 0x09001fcu, 0x09001fau, 0x08000fbu, 0x070007cu, 0x060003cu,
    0x050001cu, 0x040000cu, 0x0300005u, 0x0200001u, 0x0200000u, 0x0300004u, 0x040000du, 0x050001du,
    0x060003du, 0x08000fau, 0x08000fcu, 0x09001fbu, 0x0a003fau, 0x0b007f8u, 0x0b007fau, 0x0b007fbu,
    0x0c00ff9u, 0x0c00ffbu, 0x0d01ff8u, 0x0d01ffbu, 0x0e03ff8u, 0x0e03ff9u, 0x100fff1u, 0x100fff2u,
    0x111ffeau, 0x111ffebu, 0x123ffe1u, 0x123ffe2u, 0x123ffeau, 0x123ffe3u, 0x123ffe6u, 0x123ffe7u,
    0x123ffebu, 0x14fffe6u, 0x137ffe2u, 0x14fffe7u, 0x14fffe8u, 0x14fffe9u, 0x14fffeau, 0x14fffebu,
    0x14fffecu, 0x137ffe3u, 0x14fffedu, 0x14fffeeu, 0x14fffefu, 0x14ffff0u, 0x137ffe4u, 0x14ffff1u,
    0x123ffecu, 0x14ffff2u, 0x14ffff3u, 0x137ffe5u, 0x137ffe6u, 0x14ffff4u, 0x14ffff5u, 0x14ffff6u,
    0x14ffff7u, 0x14ffff8u, 0x14ffff9u, 0x14ffffau, 0x14ffffbu, 0x14ffffcu, 0x14ffffdu, 0x14ffffeu,
    0x14fffffu,
    /* t_huffman_env_bal_1_5dB */
    0x100ffe4u, 0x100ffe5u, 0x100ffe6u, 0x100ffe7u, 0x100ffe8u, 0x100ffe9u, 0x100ffeau, 0x100ffebu,
    0x100ffecu, 0x100ffedu, 0x100ffeeu, 0x100ffefu, 0x100fff0u, 0x100fff1u, 0x100fff2u, 0x100fff3u,
    0x100fff4u, 0x100ffe2u, 0x0c00ffcu, 0x0b007fcu, 0x09001feu, 0x070007eu, 0x050001eu, 0x0300006u,
    0x0100000u, 0x0200002u, 0x040000eu, 0x060003eu, 0x08000feu, 0x0b007fdu, 0x0c00ffdu, 0x0f07ff0u,
    0x100ffe3u, 0x100fff5u, 0x100fff6u, 0x100fff7u, 0x100fff8u, 0x100fff9u, 0x100fffau, 0x111fff6u,
    0x111fff7u, 0x111fff8u, 0x111fff9u, 0x111fffau, 0x111fffbu, 0x111fffcu, 0x111fffdu, 0x111fffeu,
    0x111ffffu,
    /* f_huffman_env_bal_1_5dB */
    0x123ffe2u, 0x123ffe3u, 0x123ffe4u, 0x123ffe5u, 0x123ffe6u, 0x123ffe7u, 0x123ffe8u, 0x123ffe9u,
    0x123ffeau, 0x123ffebu, 0x123ffecu, 0x123ffedu, 0x123ffeeu, 0x123ffefu, 0x123fff0u, 0x100fff7u,
    0x111fff0u, 0x0e03ffcu, 0x0b007feu, 0x0b007fcu, 0x08000feu, 0x070007eu, 0x040000eu, 0x0200002u,
    0x0100000u, 0x0300006u, 0x050001eu, 0x060003eu, 0x09001feu, 0x0b007fdu, 0x0c00ffeu, 0x0f07ffau,
    0x100fff6u, 0x123fff1u, 0x123fff2u, 0x123fff3u, 0x123fff4u, 0x123fff5u, 0x123fff6u, 0x123fff7u,
    0x123fff8u, 0x123fff9u, 0x123fffau, 0x123fffbu, 0x123fffcu, 0x123fffdu, 0x123fffeu, 0x137fffeu,
    0x137ffffu,
    /* t_huffman_env_3_0dB */
    0x123ffedu, 0x123ffeeu, 0x137ffdeu, 0x137ffdfu, 0x137ffe0u, 0x137ffe1u, 0x137ffe2u, 0x137ffe3u,
    0x137ffe4u, 0x137ffe5u, 0x137ffe6u, 0x137ffe7u, 0x137ffe8u, 0x137ffe9u, 0x137ffeau, 0x137ffebu,
    0x137ffecu, 0x111fff4u, 0x100fff7u, 0x100fff9u, 0x100fff8u, 0x0e03ffbu, 0x0e03ffau, 0x0e03ff8u,
    0x0d01ffau, 0x0c00ffcu, 0x0b007fcu, 0x08000feu, 0x060003eu, 0x040000eu, 0x0200002u, 0x0100000u,
    0x0300006u, 0x050001eu, 0x070007eu, 0x09001feu, 0x0b007fdu, 0x0d01ffbu, 0x0e03ff9u, 0x0e03ffcu,
    0x0f07ffau, 0x100fff6u, 0x111fff5u, 0x123ffecu, 0x137ffedu, 0x137ffeeu, 0x137ffefu, 0x137fff0u,
    0x137fff1u, 0x137fff2u, 0x137fff3u, 0x137fff4u, 0x137fff5u, 0x137fff6u, 0x137fff7u, 0x137fff8u,
    0x137fff9u, 0x137fffau, 0x137fffbu, 0x137fffcu, 0x137fffdu, 0x137fffeu, 0x137ffffu,
    /* f_huffman_env_3_0dB */
    0x14ffff0u, 0x14ffff1u, 0x14ffff2u, 0x14ffff3u, 0x14ffff4u, 0x14ffff5u, 0x14ffff6u, 0x123fff3u,
    0x137fff5u, 0x137ffeeu, 0x137ffefu, 0x137fff6u, 0x123fff4u, 0x123fff2u, 0x14ffff7u, 0x137fff0u,
    0x111fff5u, 0x123fff0u, 0x111fff4u, 0x100fff7u, 0x100fff6u, 0x0f07ff8u, 0x0e03ffbu, 0x0c00ffdu,
    0x0b007fdu, 0x0a003fdu, 0x09001fdu, 0x08000fdu, 0x060003eu, 0x040000eu, 0x0200002u, 0x0100000u,
    0x0300006u, 0x050001eu, 0x08000fcu, 0x09001fcu, 0x0a003fcu, 0x0b007fcu, 0x0c00ffcu, 0x0d01ffcu,
    0x0e03ffau, 0x0f07ff9u, 0x0f07ffau, 0x100fff8u, 0x100fff9u, 0x111fff6u, 0x111fff7u, 0x123fff5u,
    0x123fff6u, 0x123fff1u, 0x14ffff8u, 0x137fff1u, 0x137fff2u, 0x137fff3u, 0x14ffff9u, 0x137fff7u,
    0x137fff4u, 0x14ffffau, 0x14ffffbu, 0x14ffffcu, 0x14ffffdu, 0x14ffffeu, 0x14fffffu,
    /* t_huffman_env_bal_3_0dB */
    0x0d01ff2u, 0x0d01ff3u, 0x0d01ff4u, 0x0d01ff5u, 0x0d01ff6u, 0x0d01ff7u, 0x0d01ff8u, 0x0c00ff8u,
    0x08000feu, 0x070007eu, 0x040000eu, 0x0300006u, 0x0100000u, 0x0200002u, 0x050001eu, 0x060003eu,
    0x09001feu, 0x0d01ff9u, 0x0d01ffau, 0x0d01ffbu, 0x0d01ffcu, 0x0d01ffdu, 0x0d01ffeu, 0x0e03ffeu,
    0x0e03fffu,
    /* f_huffman_env_bal_3_0dB */
    0x0d01ff7u, 0x0d01ff8u, 0x0d01ff9u, 0x0d01ffau, 0x0d01ffbu, 0x0e03ff8u, 0x0e03ff9u, 0x0b007fcu,
    0x08000feu, 0x070007eu, 0x040000eu, 0x0200002u, 0x0100000u, 0x0300006u, 0x050001eu, 0x060003eu,
    0x09001feu, 0x0c00ffau, 0x0d01ff6u, 0x0e03ffau, 0x0e03ffbu, 0x0e03ffcu, 0x0e03ffdu, 0x0e03ffeu,
    0x0e03fffu,
    /* t_huffman_noise_3_0dB */
    0x0d01fceu, 0x0d01fcfu, 0x0d01fd0u, 0x0d01fd1u, 0x0d01fd2u, 0x0d01fd3u, 0x0d01fd4u, 0x0d01fd5u,
    0x0d01fd6u, 0x0d01fd7u, 0x0d01fd8u, 0x0d01fd9u, 0x0d01fdau, 0x0d01fdbu, 0x0d01fdcu, 0x0d01fddu,
    0x0d01fdeu, 0x0d01fdfu, 0x0d01fe0u, 0x0d01fe1u, 0x0d01fe2u, 0x0d01fe3u, 0x0d01fe4u, 0x0d01fe5u,
    0x0d01fe6u, 0x0d01fe7u, 0x0b007f2u, 0x08000fdu, 0x060003eu, 0x040000eu, 0x0300006u, 0x0100000u,
    0x0200002u, 0x050001eu, 0x08000fcu, 0x0a003f8u, 0x0d01fccu, 0x0d01fe8u, 0x0d01fe9u, 0x0d01feau,
    0x0d01febu, 0x0d01fecu, 0x0d01fcdu, 0x0d01fedu, 0x0d01feeu, 0x0d01fefu, 0x0d01ff0u, 0x0d01ff1u,
    0x0d01ff2u, 0x0d01ff3u, 0x0d01ff4u, 0x0d01ff5u, 0x0d01ff6u, 0x0d01ff7u, 0x0d01ff8u, 0x0d01ff9u,
    0x0d01ffau, 0x0d01ffbu, 0x0d01ffcu, 0x0d01ffdu, 0x0d01ffeu, 0x0e03ffeu, 0x0e03fffu,
    /* t_huffman_noise_bal_3_0dB */
    0x08000ecu, 0x08000edu, 0x08000eeu, 0x08000efu, 0x08000f0u, 0x08000f1u, 0x08000f2u, 0x08000f3u,
    0x08000f4u, 0x08000f5u, 0x050001cu, 0x0200002u, 0x0100000u, 0x0300006u, 0x060003au, 0x08000f6u,
    0x08000f7u, 0x08000f8u, 0x08000f9u, 0x08000fau, 0x08000fbu, 0x08000fcu, 0x08000fdu, 0x08000feu,
    0x08000ffu,
};

typedef struct { uint8_t bytes[128]; size_t bit; } Bits;

static void put(Bits *b, unsigned n, unsigned value) {
    assert(n <= 20u && b->bit + n <= sizeof(b->bytes) * 8u);
    for (unsigned k=n; k>0u; --k) {
        b->bytes[b->bit/8u] |=
            (uint8_t)(((value >> (k-1u)) & 1u) << (7u-b->bit%8u));
        ++b->bit;
    }
}
static size_t used(const Bits *b) { return (b->bit+7u)/8u; }

static void test_every_codeword(void) {
    unsigned checked=0,failures=0;
    for (unsigned id=0; id<SAUTI_SBR_HUFF_TABLE_COUNT; ++id) {
        const RefBook book=books[id];
        for (unsigned index=0; index<book.count; ++index) {
            const uint32_t packed=refwords[book.begin+index];
            const unsigned bits=packed>>20;
            const unsigned code=packed&0xfffffu;
            assert(bits>=1u && bits<=20u && (uint64_t)code<(1ull<<bits));
            assert(index<=120u);
            const int expected=(int)index-(int)book.lav;
            Bits b={0};
            unsigned prefix=1u+(index%7u);
            put(&b,prefix,(1u<<prefix)-1u);
            size_t start=b.bit;
            put(&b,bits,code);
            size_t pos=start;
            int32_t result=INT32_MIN;
            assert(sauti_aac_sbr_huffman_decode(b.bytes,used(&b),&pos,
                      (SautiAacSbrHuffmanBook)id,&result));
            assert(result==expected && pos==b.bit);
            ++checked;
            /* Exact byte-level truncation: place the final code bit
             * at a byte boundary then omit the last input byte. */
            b=(Bits){0};
            prefix=(8u-(bits&7u))&7u;
            if (prefix) put(&b,prefix,(1u<<prefix)-1u);
            start=b.bit;
            put(&b,bits,code);
            assert((b.bit&7u)==0u);
            size_t missing_byte=used(&b)-1u;
            pos=start;
            result=INT32_MIN;
            assert(!sauti_aac_sbr_huffman_decode(b.bytes,missing_byte,&pos,
                     (SautiAacSbrHuffmanBook)id,&result));
            assert(pos==start && result==INT32_MIN);
            ++failures;
        }
    }
    printf("SBR-07 Huffman: %u verified codewords, %u truncated rollback checks\n",
           checked,failures);
    assert(checked==604u && failures==604u);
}

static void test_mixed_sequence(void) {
    Bits b={0};
    put(&b,3u,5u);
    const size_t start=b.bit;
    for (unsigned rep=0; rep<3u; ++rep) {
        for (unsigned id=0; id<SAUTI_SBR_HUFF_TABLE_COUNT; ++id) {
            const RefBook book=books[id];
            const unsigned index=rep==0?book.lav:
                                 rep==1?0u:book.count-1u;
            const uint32_t packed=refwords[book.begin+index];
            put(&b,packed>>20,packed&0xfffffu);
        }
    }
    size_t pos=start;
    for (unsigned rep=0; rep<3u; ++rep)
        for (unsigned id=0; id<SAUTI_SBR_HUFF_TABLE_COUNT; ++id) {
            const RefBook book=books[id];
            const unsigned index=rep==0?book.lav:
                                 rep==1?0u:book.count-1u;
            int32_t result=INT32_MIN;
            assert(sauti_aac_sbr_huffman_decode(b.bytes,used(&b),&pos,
                        (SautiAacSbrHuffmanBook)id,&result));
            assert(result==(int)index-(int)book.lav);
        }
    assert(pos==b.bit);
}
static void test_fail_closed_inputs(void) {
    const uint8_t data[2]={0,0};
    size_t pos=3u;
    int32_t out=0x12345678;
    assert(!sauti_aac_sbr_huffman_decode(data,sizeof(data),&pos,
                      SAUTI_SBR_HUFF_TABLE_COUNT,&out));
    assert(!sauti_aac_sbr_huffman_decode(data,sizeof(data),&pos,
                      (SautiAacSbrHuffmanBook)-1,&out));
    assert(!sauti_aac_sbr_huffman_decode(NULL,sizeof(data),&pos,
                      SAUTI_SBR_HUFF_ENV_3_0DB_F,&out));
    assert(!sauti_aac_sbr_huffman_decode(data,sizeof(data),NULL,
                      SAUTI_SBR_HUFF_ENV_3_0DB_F,&out));
    assert(!sauti_aac_sbr_huffman_decode(data,sizeof(data),&pos,
                      SAUTI_SBR_HUFF_ENV_3_0DB_F,NULL));
    assert(pos==3u && out==0x12345678);
    pos=SIZE_MAX;
    assert(!sauti_aac_sbr_huffman_decode(data,sizeof(data),&pos,
                      SAUTI_SBR_HUFF_ENV_3_0DB_F,&out));
    assert(pos==SIZE_MAX && out==0x12345678);
    pos=0u;
    assert(!sauti_aac_sbr_huffman_decode(data,SIZE_MAX,&pos,
                      SAUTI_SBR_HUFF_ENV_3_0DB_F,&out));
    assert(pos==0u && out==0x12345678);
}
int main(void) {
    test_every_codeword();
    test_mixed_sequence();
    test_fail_closed_inputs();
    puts("PASS HE-AAC SBR-07 native Huffman: 10 codebooks, all words, mixed streams, fail-closed");
    return 0;
}
