#ifndef SAYARI_SAUTI_H
#define SAYARI_SAUTI_H

#include <stdint.h>

/* Stage 3 engine ABI: x86-64 System V, IEEE-754 f32, ARGB8888 little-endian. */
/* No window or audio dependencies. Windows x64 needs an ABI adapter. */
#define SAUTI_FFT_SAMPLES 4096u
#define SAUTI_LOG_BANDS 256u
#define SAUTI_RING_CAPACITY 16384u
#define SAUTI_STEREO_RING_CAPACITY 16384u
typedef struct {
    float peak;
    float rms;
} SautiMetrics;

/* Always caller-owned and kept alive across frames; calloc before first use.
 * Array order/size are part of the current NASM/C link contract.
 * 6 x 4096 f32 + 2 x 256 f32, plus two oscillator phases (~98 KiB).
 */
typedef struct {
    float samples[SAUTI_FFT_SAMPLES];
    float tone[SAUTI_FFT_SAMPLES];
    float fft_real[SAUTI_FFT_SAMPLES];
    float fft_imag[SAUTI_FFT_SAMPLES];
    float magnitude[SAUTI_FFT_SAMPLES];
    float phase_a, phase_b;
    float db_bins[SAUTI_FFT_SAMPLES];
    float log_bands[SAUTI_LOG_BANDS];
    float smooth_bands[SAUTI_LOG_BANDS];
} SautiFrameScratch;

/* Fixed-capacity SPSC mono ring; built for x86-64 TSO. Only the NASM
 * functions touch indices after zero-initialization; never copy while live. */
typedef struct {
    uint32_t write_index, read_index;
    float samples[SAUTI_RING_CAPACITY];
} SautiAudioRing;

#include <stddef.h>
/* Independent L/R ring: one SPSC producer, one consumer. Caller calloc()s. */
typedef struct {
    uint32_t write_index, read_index;
    float interleaved[SAUTI_STEREO_RING_CAPACITY * 2u];
} SautiStereoRing;

typedef struct {
    uint32_t level[24];
    uint32_t last_peak_ms[24];
} SautiPeakHold;

#ifdef __cplusplus
extern "C" {
#endif
void sauti_sine_f32(float *dst, uint32_t count, float *phase, float radians_per_sample);
void sauti_metrics_f32(const float *src, uint32_t count, SautiMetrics *out);
int sauti_fft_real_f32(const float *samples, uint32_t n,
                       float *real_out, float *imag_out);
int sauti_fft_magnitude_f32(const float *real, const float *imag,
                            uint32_t n, float *magnitudes);
void sauti_render_spectrum_argb32(uint32_t *argb, uint32_t width, uint32_t height,
                                  const float *magnitudes, uint32_t bin_count);
void sauti_render_wave_argb32(uint32_t *argb, uint32_t width, uint32_t height,
                               const float *src, uint32_t sample_count);
/* Visual-only gain [1..64], original samples stay unmodified. */
void sauti_render_wave_scaled_argb32(uint32_t *argb, uint32_t width,
    uint32_t height, const float *src, uint32_t sample_count, float gain);
/* Peak/RMS levels are normalized [-90..0dBFS] to [0..1]. */
void sauti_draw_levels_argb32(uint32_t *argb, uint32_t width,
    uint32_t height, const float normalized_peak_rms[2]);
int sauti_render_demo_frame(uint32_t *argb, uint32_t width, uint32_t height,
                            SautiFrameScratch *scratch);
/* Same analysis/render path, but uses scratch->samples from audio ring. */
int sauti_render_audio_frame(uint32_t *argb, uint32_t width, uint32_t height,
                             SautiFrameScratch *scratch);
/* One 4096-sample window per channel. Separate persistent scratch states. */
int sauti_analyze_stereo_bands_f32(const float *left, const float *right,
                                   SautiFrameScratch *left_scratch,
                                   SautiFrameScratch *right_scratch);
void sauti_peak_hold_update(SautiPeakHold *state, const float *left_bands,
                            const float *right_bands, uint32_t milliseconds);
void sauti_draw_peak_hold_argb32(uint32_t *argb, uint32_t width,
                                 uint32_t height, const SautiPeakHold *state);
void sauti_render_stereo_bars_argb32(uint32_t *argb, uint32_t width,
    uint32_t height, const float *left_bands, const float *right_bands,
    uint32_t phase);
uint32_t sauti_stereo_ring_push_f32(SautiStereoRing *ring, const float *interleaved,
                                   uint32_t frames);
uint32_t sauti_stereo_ring_drain_f32(SautiStereoRing *ring, float *left, float *right,
                                    uint32_t frames);
uint32_t sauti_ring_push_stereo_f32(SautiAudioRing *ring, const float *stereo,
                                    uint32_t frames);
uint32_t sauti_ring_drain_window_f32(SautiAudioRing *ring, float *window,
                                     uint32_t window_count);
int sauti_db_normalize_f32(const float *amplitudes, uint32_t count, float *db_normalized);
int sauti_log_bands_f32(const float *db_bins, uint32_t bin_count,
                        float *bands, uint32_t band_count);
int sauti_smooth_bands_f32(const float *target, float *history, uint32_t count,
                           float attack, float release);
#ifdef __cplusplus
}
#endif

#endif
