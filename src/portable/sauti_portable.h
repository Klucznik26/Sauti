/* Przenosne implementacje w C jader NASM (AAC, AVR, DVSM, YM).  Kazde jadro ma TE SAME nazwe, sygnature i wynik co wersja asemblerowa; plik .c w tym katalogu odpowiada plikowi .asm o tej samej
 * nazwie bazowej (sauti_aac_huffman.asm -> sauti_aac_huffman_portable.c).  Uzycie:
 *   - budowa przenosna (make PORTABLE=1): zamiast NASM kompilowany jest ten plik (ARM64, MSan, platformy bez NASM);
 *   - test rownowaznosci (tests/test_portable_equivalence.c): ten sam plik z -DSAUTI_PORTABLE_REF_SUFFIX daje nazwy z przyrostkiem _c, porownywane bit w bit z asemblerem na tych samych wejsciach.
 * Zmiennoprzecinkowe operacje sa pisane w tej samej kolejnosci co w asemblerze (bez FMA: build dodaje -ffp-contract=off). */
#ifndef SAUTI_PORTABLE_H
#define SAUTI_PORTABLE_H
#ifdef SAUTI_PORTABLE_REF_SUFFIX
#define sauti_aac_huffman_decode_asm sauti_aac_huffman_decode_asm_c
#define sauti_aac_dequant_scale_asm sauti_aac_dequant_scale_asm_c
#define sauti_aac_adts_parse_asm sauti_aac_adts_parse_asm_c
#define sauti_aac_bits_read_asm sauti_aac_bits_read_asm_c
#define sauti_aac_imdct_dot_asm sauti_aac_imdct_dot_asm_c
#define sauti_aac_spectral_index_asm sauti_aac_spectral_index_asm_c
#define sauti_aac_window_overlap_asm sauti_aac_window_overlap_asm_c
#define sauti_aac_ps_mix_strided_asm sauti_aac_ps_mix_strided_asm_c
#define sauti_aac_ps_mix_complex_strided_asm sauti_aac_ps_mix_complex_strided_asm_c
#define sauti_avr_parse_asm sauti_avr_parse_asm_c
#define sauti_avr_pcm_to_f32_asm sauti_avr_pcm_to_f32_asm_c
#define sauti_dvsm_pcm_f32_asm sauti_dvsm_pcm_f32_asm_c
#define sauti_dvsm_delta_mono_asm sauti_dvsm_delta_mono_asm_c
#define sauti_dvsm_delta_stereo_block_asm sauti_dvsm_delta_stereo_block_asm_c
#define sauti_dvsm_voice_mono_asm sauti_dvsm_voice_mono_asm_c
#define sauti_dvsm_voice_stereo_block_asm sauti_dvsm_voice_stereo_block_asm_c
#define sauti_ym_tone_step_asm sauti_ym_tone_step_asm_c
#define sauti_ym_noise_step_asm sauti_ym_noise_step_asm_c
#define sauti_ym_env_init_asm sauti_ym_env_init_asm_c
#define sauti_ym_env_step_asm sauti_ym_env_step_asm_c
#define sauti_ym_drum_sample_asm sauti_ym_drum_sample_asm_c
#define maktabasauti_mod_voice_add maktabasauti_mod_voice_add_c
#endif
#include <stddef.h>
#include <stdint.h>
#include <string.h>
static inline uint32_t sp_f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static inline float sp_u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static inline uint16_t sp_be16(const uint8_t *p) { return (uint16_t)(((unsigned)p[0] << 8) | p[1]); }
static inline uint32_t sp_be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
#endif
