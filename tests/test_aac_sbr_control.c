#include "sauti_aac_sbr_control.h"
#include "sauti_aac_sbr_master.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t bytes[64]; size_t bits; } Bits;
static void put(Bits *b, unsigned width, unsigned value) {
    assert(width <= 32u && b->bits + width <= sizeof(b->bytes) * 8u);
    for (unsigned i = width; i > 0u; --i) {
        b->bytes[b->bits / 8u] |=
            (uint8_t)(((value >> (i - 1u)) & 1u) << (7u - b->bits % 8u));
        ++b->bits;
    }
}
static size_t bytes_used(const Bits *b) { return (b->bits + 7u) / 8u; }

static void layout(SautiAacSbrGrid *grid, SautiAacSbrTables *tables,
                   unsigned env, unsigned freq_noise) {
    memset(grid, 0, sizeof(*grid));
    memset(tables, 0, sizeof(*tables));
    assert(env >= 1 && env <= SAUTI_AAC_SBR_MAX_ENV);
    assert(freq_noise >= 1 && freq_noise <= SAUTI_AAC_SBR_MAX_NOISE_BANDS);
    grid->env_count = env;
    grid->noise_count = env == 1u ? 1u : 2u;
    for (unsigned i = 0; i < env; ++i) grid->freq_res[i] = (uint8_t)(i & 1u);
    tables->kx = 16;
    tables->m = 32;
    tables->n_high = 16;
    tables->n_low = 8;
    tables->n_noise = freq_noise;
}

static void test_nonbyte_and_values(void) {
    SautiAacSbrGrid grid;
    SautiAacSbrTables tables;
    layout(&grid, &tables, 3, 4);
    Bits b = {0};
    put(&b, 3, 5); /* deliberately non-byte-aligned start */
    const size_t start = b.bits;
    put(&b, 1, 1); put(&b, 1, 0); put(&b, 1, 1);
    put(&b, 1, 0); put(&b, 1, 1);
    put(&b, 2, 0); put(&b, 2, 1); put(&b, 2, 2); put(&b, 2, 3);
    SautiAacSbrControl out;
    memset(&out, 0xa5, sizeof(out));
    size_t pos = start;
    assert(sauti_aac_sbr_control_parse(b.bytes, bytes_used(&b), &pos,
                                        &grid, &tables, &out));
    assert(pos == b.bits && out.env_count == 3u &&
           out.noise_count == 2u && out.noise_band_count == 4u);
    assert(out.df_env[0] == 1u && out.df_env[1] == 0u &&
           out.df_env[2] == 1u);
    assert(out.df_noise[0] == 0u && out.df_noise[1] == 1u);
    for (unsigned i = 0; i < 4; ++i) assert(out.invf_mode[i] == i);
    for (unsigned i = 3; i < SAUTI_AAC_SBR_MAX_ENV; ++i)
        assert(out.df_env[i] == 0u);
    assert(out.invf_mode[4] == 0u);
}

static void test_real_grid_and_tables(void) {
    /* Generate the grid from its actual bitstream parser and derive
     * the frequency layout from actual SBR master/low/noise functions. */
    Bits b = {0};
    put(&b, 2, 0); put(&b, 2, 2); put(&b, 1, 1); /* FIXFIX / 4 envelopes */
    SautiAacSbrGrid grid;
    size_t pos = 0;
    assert(sauti_aac_sbr_grid_parse(b.bytes, bytes_used(&b), &pos,
                                    16u, &grid));
    assert(pos == 5u && grid.env_count == 4u && grid.noise_count == 2u);
    SautiAacSbrMaster master;
    assert(sauti_aac_sbr_master_linear(16, 48, 1, 0, &master));
    SautiAacSbrTables tables;
    assert(sauti_aac_sbr_tables_build(&master, 3, &tables));
    assert(tables.n_noise == 5u);

    /* The following bits occur immediately after sbr_grid:
     * bs_df_env[4], bs_df_noise[2], bs_invf_mode[5]. */
    const size_t control_start = b.bits;
    for (unsigned i = 0; i < 4; ++i) put(&b, 1, i & 1u);
    put(&b, 1, 1); put(&b, 1, 0);
    for (unsigned i = 0; i < 5; ++i) put(&b, 2, i & 3u);
    SautiAacSbrControl out;
    pos = control_start;
    assert(sauti_aac_sbr_control_parse(b.bytes, bytes_used(&b), &pos,
                                        &grid, &tables, &out));
    assert(pos == b.bits && out.env_count == 4u &&
           out.noise_count == 2u && out.noise_band_count == 5u);
    for (unsigned i = 0; i < 4; ++i) assert(out.df_env[i] == (i & 1u));
    assert(out.df_noise[0] == 1u && out.df_noise[1] == 0u);
    for (unsigned i = 0; i < 5; ++i) assert(out.invf_mode[i] == (i & 3u));
}

static void test_truncation_and_fail_closed(void) {
    SautiAacSbrGrid grid;
    SautiAacSbrTables tables;
    layout(&grid, &tables, 5u, 5u);
    Bits b = {0};
    put(&b, 6, 0x25);
    const size_t start = b.bits;
    for (unsigned i = 0; i < 5; ++i) put(&b, 1, i & 1u);
    put(&b, 1, 1u); put(&b, 1, 0u);
    for (unsigned i = 0; i < 5; ++i) put(&b, 2, i & 3u);
    SautiAacSbrControl out, original;
    memset(&out, 0xa5, sizeof(out));
    original = out;
    for (size_t bytes = 0; bytes < bytes_used(&b); ++bytes) {
        size_t pos = start;
        assert(!sauti_aac_sbr_control_parse(b.bytes, bytes, &pos,
                                             &grid, &tables, &out));
        assert(pos == start && memcmp(&out, &original, sizeof(out)) == 0);
    }
    size_t pos = start;
    grid.env_count = 0u;
    assert(!sauti_aac_sbr_control_parse(b.bytes, bytes_used(&b), &pos,
                                        &grid, &tables, &out));
    grid.env_count = 6u;
    assert(!sauti_aac_sbr_control_parse(b.bytes, bytes_used(&b), &pos,
                                        &grid, &tables, &out));
    grid.env_count = 5u;
    grid.noise_count = 1u; /* Requires two noise time envelopes */
    assert(!sauti_aac_sbr_control_parse(b.bytes, bytes_used(&b), &pos,
                                        &grid, &tables, &out));
    grid.noise_count = 2u;
    grid.freq_res[4] = 2u;
    assert(!sauti_aac_sbr_control_parse(b.bytes, bytes_used(&b), &pos,
                                        &grid, &tables, &out));
    grid.freq_res[4] = 0u;
    tables.n_noise = 0u;
    assert(!sauti_aac_sbr_control_parse(b.bytes, bytes_used(&b), &pos,
                                        &grid, &tables, &out));
    tables.n_noise = 6u;
    assert(!sauti_aac_sbr_control_parse(b.bytes, bytes_used(&b), &pos,
                                        &grid, &tables, &out));
    tables.n_noise = 5u;
    tables.n_high = 0u;
    assert(!sauti_aac_sbr_control_parse(b.bytes, bytes_used(&b), &pos,
                                        &grid, &tables, &out));
    tables.n_high = 16u;
    tables.kx = 33u;
    assert(!sauti_aac_sbr_control_parse(b.bytes, bytes_used(&b), &pos,
                                        &grid, &tables, &out));
    tables.kx = 16u;
    assert(!sauti_aac_sbr_control_parse(NULL, bytes_used(&b), &pos,
                                        &grid, &tables, &out));
    assert(!sauti_aac_sbr_control_parse(b.bytes, bytes_used(&b), NULL,
                                        &grid, &tables, &out));
    assert(!sauti_aac_sbr_control_parse(b.bytes, bytes_used(&b), &pos,
                                        NULL, &tables, &out));
    assert(!sauti_aac_sbr_control_parse(b.bytes, bytes_used(&b), &pos,
                                        &grid, NULL, &out));
    assert(!sauti_aac_sbr_control_parse(b.bytes, bytes_used(&b), &pos,
                                        &grid, &tables, NULL));
    assert(pos == start && memcmp(&out, &original, sizeof(out)) == 0);
}

static void test_matrix(void) {
    unsigned checked = 0, truncated = 0;
    for (unsigned env = 1; env <= SAUTI_AAC_SBR_MAX_ENV; ++env)
      for (unsigned freq_noise = 1; freq_noise <= SAUTI_AAC_SBR_MAX_NOISE_BANDS;
           ++freq_noise)
        for (unsigned prefix = 0; prefix < 8; ++prefix)
          for (unsigned pattern = 0; pattern < 4; ++pattern) {
            SautiAacSbrGrid grid;
            SautiAacSbrTables tables;
            layout(&grid, &tables, env, freq_noise);
            Bits b = {0};
            if (prefix) put(&b, prefix, (1u << prefix) - 1u);
            const size_t start = b.bits;
            for (unsigned i = 0; i < env; ++i)
                put(&b, 1, (i + pattern) & 1u);
            for (unsigned i = 0; i < grid.noise_count; ++i)
                put(&b, 1, (i + pattern + 1u) & 1u);
            for (unsigned i = 0; i < freq_noise; ++i)
                put(&b, 2, (i + pattern) & 3u);
            SautiAacSbrControl result;
            size_t pos = start;
            assert(sauti_aac_sbr_control_parse(b.bytes, bytes_used(&b), &pos,
                                                &grid, &tables, &result));
            assert(pos == b.bits && result.env_count == env &&
                   result.noise_count == grid.noise_count &&
                   result.noise_band_count == freq_noise);
            for (unsigned i = 0; i < env; ++i)
                assert(result.df_env[i] == ((i + pattern) & 1u));
            for (unsigned i = 0; i < grid.noise_count; ++i)
                assert(result.df_noise[i] == ((i + pattern + 1u) & 1u));
            for (unsigned i = 0; i < freq_noise; ++i)
                assert(result.invf_mode[i] == ((i + pattern) & 3u));
            ++checked;
            SautiAacSbrControl sentinel;
            memset(&sentinel, 0xd7, sizeof(sentinel));
            for (size_t n = 0; n < bytes_used(&b); ++n) {
                SautiAacSbrControl value = sentinel;
                pos = start;
                assert(!sauti_aac_sbr_control_parse(b.bytes, n, &pos,
                                                    &grid, &tables, &value));
                assert(pos == start && memcmp(&value, &sentinel,
                                               sizeof(value)) == 0);
                ++truncated;
            }
          }
    printf("SBR-06 controls: %u valid, %u truncation checks fail-closed\n",
           checked, truncated);
}
int main(void) {
    test_nonbyte_and_values();
    test_real_grid_and_tables();
    test_truncation_and_fail_closed();
    test_matrix();
    puts("PASS HE-AAC SBR-06 dtdf/invf: time/frequency counts, nonbyte offsets, rollback");
    return 0;
}
