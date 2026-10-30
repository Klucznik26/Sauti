#include "sauti.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void near(float actual, float expected) {
    assert(fabsf(actual - expected) < 0.0001f);
}
int main(void) {
    SautiStereoRing *r = calloc(1, sizeof(*r));
    assert(r);
    float left[4096] = {0}, right[4096] = {0};
    const float input[] = { 0.8f, 0.0f,  0.0f, -0.6f,  0.5f, -0.5f };
    assert(sauti_stereo_ring_push_f32(r, input, 3) == 3);
    assert(sauti_stereo_ring_drain_f32(r, left, right, 4) == 3);
    near(left[0], 0); near(right[0], 0);
    near(left[1], 0.8f); near(right[1], 0);
    near(left[2], 0); near(right[2], -0.6f);
    near(left[3], 0.5f); near(right[3], -0.5f);
    assert(sauti_stereo_ring_drain_f32(r, left, right, 4) == 0);

    /* Preserve right/left independence, even when stereo cancels in mono. */
    const float antiphase[] = {0.75f, -0.75f};
    assert(sauti_stereo_ring_push_f32(r, antiphase, 1) == 1);
    assert(sauti_stereo_ring_drain_f32(r, left, right, 4) == 1);
    near(left[3], 0.75f); near(right[3], -0.75f);

    /* Large pushes are rejected; capacity is in FRAMES, not samples. */
    assert(sauti_stereo_ring_push_f32(r, input, 16385) == 0);
    assert(sauti_stereo_ring_drain_f32(NULL, left, right, 4) == 0);
    assert(sauti_stereo_ring_drain_f32(r, left, right, 4097) == 0);

    /* Wraparound and overflow: exercise many independent stereo frames. */
    float *block = malloc(2048u * 2u * sizeof(float));
    assert(block);
    for (unsigned repeat = 0; repeat < 20; ++repeat) {
        for (unsigned i = 0; i < 2048; ++i) {
            block[i*2] = (repeat & 1u) ? -0.25f : 0.25f;
            block[i*2+1] = (repeat & 1u) ? 0.5f : -0.5f;
        }
        assert(sauti_stereo_ring_push_f32(r, block, 2048) == 2048);
        assert(sauti_stereo_ring_drain_f32(r, left, right, 4096) == 2048);
        near(left[4095], (repeat & 1u) ? -0.25f : 0.25f);
        near(right[4095], (repeat & 1u) ? 0.5f : -0.5f);
    }
    free(block);
    free(r);
    puts("sauti stereo ring tests passed");
    return 0;
}
