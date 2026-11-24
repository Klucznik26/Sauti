# Stage 6: x86-64 NASM engine, host tests and optional Linux SDL2/Wayland.
NASM ?= nasm
CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Werror
# PORTABLE=1: bez NASM. Jadra asemblerowe (AAC, AVR, DVSM, YM) zastepuja ich odpowiedniki w C z src/portable (bit w bit jak asembler, patrz tests/test_portable_equivalence.c), jadra QMF wybieraja wersje
# w C (SAUTI_QMF_DISABLE_ASM), a -ffp-contract=off zabezpiecza kolejnosc dzialan zmiennoprzecinkowych przed FMA (ARM64).  Uzycie: make PORTABLE=1 OBJDIR=build_portable test-aac-native
PORTABLE ?= 0
SAUTI_PORTABLE_DIR := src/portable
ifeq ($(PORTABLE),1)
CFLAGS += -ffp-contract=off -DSAUTI_QMF_DISABLE_ASM
define ASM_OBJ
$(if $(wildcard $(SAUTI_PORTABLE_DIR)/$(basename $(notdir $(1)))_portable.c),$(CC) $(CFLAGS) -Iinclude -I$(SAUTI_PORTABLE_DIR) -c -o $@ $(SAUTI_PORTABLE_DIR)/$(basename $(notdir $(1)))_portable.c,$(error PORTABLE=1: brak odpowiednika w C dla $(1)))
endef
else
define ASM_OBJ
$(NASM) -f elf64 -o $@ $(1)
endef
endif
OBJDIR := build
ARCHIVE_AVAILABLE := $(shell $(PKG_CONFIG) --exists libarchive && echo yes)
ifeq ($(ARCHIVE_AVAILABLE),yes)
ARCHIVE_FLAGS := -DSAUTI_HAVE_LIBARCHIVE $(shell $(PKG_CONFIG) --cflags libarchive)
ARCHIVE_LIBS := $(shell $(PKG_CONFIG) --libs libarchive)
endif
# Optional portable Opus stack; no change to builds without libopusfile.
OPUS_AVAILABLE := $(shell $(PKG_CONFIG) --exists opusfile && echo yes)
ifeq ($(OPUS_AVAILABLE),yes)
OPUS_FLAGS := -DSAUTI_HAVE_OPUSFILE $(shell $(PKG_CONFIG) --cflags opusfile)
OPUS_LIBS := $(shell $(PKG_CONFIG) --libs opusfile)
endif
# Optional lossless WavPack codec.
WAVPACK_AVAILABLE := $(shell $(PKG_CONFIG) --exists wavpack && echo yes)
ifeq ($(WAVPACK_AVAILABLE),yes)
WAVPACK_FLAGS := -DSAUTI_HAVE_WAVPACK $(shell $(PKG_CONFIG) --cflags wavpack)
WAVPACK_LIBS := $(shell $(PKG_CONFIG) --libs wavpack)
endif
WAVPACK_SRC := src/decoders/modern/sauti_decoder_wavpack.c

# Optional Falcon DTM tracker playback through libxmp.
XMP_AVAILABLE := $(shell $(PKG_CONFIG) --exists libxmp && echo yes)
ifeq ($(XMP_AVAILABLE),yes)
XMP_FLAGS := -DSAUTI_HAVE_LIBXMP $(shell $(PKG_CONFIG) --cflags libxmp)
XMP_LIBS := $(shell $(PKG_CONFIG) --libs libxmp)
endif
# Optional in-process FFmpeg AAC/ALAC/WebM support.
FFMPEG_AVAILABLE := $(shell $(PKG_CONFIG) --exists libavformat libavcodec libavutil libswresample && echo yes)
ifeq ($(FFMPEG_AVAILABLE),yes)
FFMPEG_FLAGS := -DSAUTI_HAVE_FFMPEG $(shell $(PKG_CONFIG) --cflags libavformat libavcodec libavutil libswresample)
FFMPEG_LIBS := $(shell $(PKG_CONFIG) --libs libavformat libavcodec libavutil libswresample)
endif
# Optional locally cloned upstream AtariAudio 1.26, kept outside tracked sources.
# When absent, all existing Sauti build targets continue unchanged.
CXX ?= c++
ATARIAUDIO_DIR := third_party/AtariAudio
ifneq ($(wildcard $(ATARIAUDIO_DIR)/src/AtariAudioRenderer.h),)
SNDH_CPP := src/decoders/retro/atari/sauti_decoder_sndh.cpp
ATARIAUDIO_CPP := $(wildcard $(ATARIAUDIO_DIR)/src/*.cpp) $(wildcard $(ATARIAUDIO_DIR)/src/external/*.cpp) $(wildcard $(ATARIAUDIO_DIR)/src/external/Musashi/*.cpp)
SNDH_OBJECTS := $(OBJDIR)/sauti_decoder_sndh.o $(patsubst $(ATARIAUDIO_DIR)/src/%.cpp,$(OBJDIR)/atariaudio/%.o,$(ATARIAUDIO_CPP))
SNDH_CFLAGS := -DSAUTI_HAVE_ATARIAUDIO
SNDH_LINK := -lstdc++
endif

$(OBJDIR)/sauti_decoder_sndh.o: src/decoders/retro/atari/sauti_decoder_sndh.cpp include/sauti_decoder_sndh.h | $(OBJDIR)
	$(CXX) -O2 -std=c++17 -Iinclude -I$(ATARIAUDIO_DIR)/src $(SNDH_CFLAGS) -c $< -o $@

$(OBJDIR)/atariaudio/%.o: $(ATARIAUDIO_DIR)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) -O2 -std=c++17 -I$(ATARIAUDIO_DIR)/src -c $< -o $@

VOC_SRC := src/decoders/retro/pc/sauti_decoder_voc.c
AIFF_SRC := src/decoders/modern/sauti_decoder_aiff.c
FFMPEG_SRC := src/decoders/modern/sauti_decoder_ffmpeg.c
DTM_SRC := src/decoders/retro/atari/sauti_decoder_dtm.c
AVR_SRC := src/decoders/retro/atari/sauti_decoder_avr.c
DVSM_SRC := src/decoders/retro/atari/sauti_decoder_dvsm.c
PORTABLE_SRC := src/sauti_playlist.c src/sauti_transport.c src/sauti_app_state.c src/sauti_library.c src/sauti_wav.c src/sauti_record_ring.c src/sauti_audio_backend.c src/sauti_playback_time.c src/sauti_playback_backend.c src/sauti_decoder.c src/decoders/modern/sauti_decoder_wav.c src/decoders/modern/sauti_decoder_mp3.c src/decoders/modern/sauti_decoder_vorbis.c src/decoders/modern/sauti_decoder_midi.c src/decoders/modern/sauti_decoder_opus.c src/decoders/modern/sauti_decoder_flac.c src/sauti_pcm_output.c $(AVR_SRC) $(DVSM_SRC) $(DTM_SRC) $(VOC_SRC) $(AIFF_SRC) $(FFMPEG_SRC)
AVR_ASM := $(OBJDIR)/sauti_decoder_avr.o
DVSM_ASM := $(OBJDIR)/sauti_decoder_dvsm.o
YM_SRC := src/decoders/retro/atari/sauti_decoder_ym.c
YM_ARCHIVE_SRC := src/decoders/retro/atari/sauti_ym_archive.c
YM_ASM := $(OBJDIR)/sauti_decoder_ym.o
AAC_HEADERS_ASM := $(OBJDIR)/sauti_aac_headers.o
AAC_CONFIG_SRC := src/decoders/modern/aac/sauti_aac_config.c
AAC_SYNTAX_SRC := src/decoders/modern/aac/sauti_aac_syntax.c
AAC_PS_MAP_SRC := src/decoders/modern/aac/sauti_aac_ps_map.c
AAC_PS_HYBRID_SRC := src/decoders/modern/aac/sauti_aac_ps_hybrid.c
AAC_PS_MATRIX_SRC := src/decoders/modern/aac/sauti_aac_ps_matrix.c
AAC_PS_PHASE_SRC := src/decoders/modern/aac/sauti_aac_ps_phase.c
AAC_PS_MIX_SRC := src/decoders/modern/aac/sauti_aac_ps_mix.c
AAC_PS_RENDER_SRC := src/decoders/modern/aac/sauti_aac_ps_render.c
AAC_PS_MIX_ASM := $(OBJDIR)/sauti_aac_ps_mix.o
AAC_SBR_GRID_SRC := src/decoders/modern/aac/sauti_aac_sbr_grid.c
AAC_SBR_MASTER_SRC := src/decoders/modern/aac/sauti_aac_sbr_master.c
AAC_SBR_MASTER_LOG_SRC := src/decoders/modern/aac/sauti_aac_sbr_master_log.c
AAC_SBR_LIMITS_SRC := src/decoders/modern/aac/sauti_aac_sbr_limits.c
AAC_SBR_FREQ_TABLES_SRC := src/decoders/modern/aac/sauti_aac_sbr_tables.c
AAC_SBR_CONTROL_SRC := src/decoders/modern/aac/sauti_aac_sbr_control.c
AAC_SBR_HUFFMAN_SRC := src/decoders/modern/aac/sauti_aac_sbr_huffman.c
AAC_SBR_QUANT_SRC := src/decoders/modern/aac/sauti_aac_sbr_quant.c
AAC_SBR_ENERGY_SRC := src/decoders/modern/aac/sauti_aac_sbr_energy.c
AAC_SBR_PATCH_SRC := src/decoders/modern/aac/sauti_aac_sbr_patch.c
AAC_SBR_HF_SRC := src/decoders/modern/aac/sauti_aac_sbr_hf.c
AAC_SBR_HF_PREDICT_SRC := src/decoders/modern/aac/sauti_aac_sbr_hf_predict.c
AAC_SBR_LPC_SRC := src/decoders/modern/aac/sauti_aac_sbr_lpc.c
AAC_SBR_HF_ADJUST_SRC := src/decoders/modern/aac/sauti_aac_sbr_hf_adjust.c
AAC_SBR_HF_LIMITER_SRC := src/decoders/modern/aac/sauti_aac_sbr_hf_limiter.c
AAC_SBR_LIMITER_BANDS_SRC := src/decoders/modern/aac/sauti_aac_sbr_limiter_bands.c
AAC_SBR_HF_NOISE_SRC := src/decoders/modern/aac/sauti_aac_sbr_hf_noise.c
AAC_SBR_HF_SINE_SRC := src/decoders/modern/aac/sauti_aac_sbr_hf_sine.c
AAC_SBR_HF_JOINT_SRC := src/decoders/modern/aac/sauti_aac_sbr_hf_joint.c
AAC_SBR_SMOOTH_SRC := src/decoders/modern/aac/sauti_aac_sbr_smooth.c
AAC_SBR_HF_JOINT_SMOOTH_SRC := src/decoders/modern/aac/sauti_aac_sbr_hf_joint_smooth.c
AAC_SBR_NOISE_ROM_SRC := src/decoders/modern/aac/sauti_aac_sbr_noise_rom.c
AAC_SBR_HF_JOINT_ROM_SRC := src/decoders/modern/aac/sauti_aac_sbr_hf_joint_rom.c
AAC_SBR_QMF_SYNTH_WINDOW_ASM := $(OBJDIR)/sauti_aac_sbr_qmf_synth_window_asm.o
AAC_SBR_QMF_SYNTH_MOD_ASM := $(OBJDIR)/sauti_aac_sbr_qmf_synth_mod_asm.o
AAC_SBR_QMF_SYNTH_WINDOW_AVX2 := $(OBJDIR)/sauti_aac_sbr_qmf_synth_window_avx2.o
AAC_SBR_QMF_SYNTH_MOD_AVX2 := $(OBJDIR)/sauti_aac_sbr_qmf_synth_mod_avx2.o
# Keep the original SSE2 kernels; AVX2 is dispatched only after CPUID/OSXSAVE.
AAC_SBR_QMF_SYNTH_WINDOW_SRC := src/decoders/modern/aac/sauti_aac_sbr_qmf_synth_window.c $(AAC_SBR_QMF_SYNTH_WINDOW_ASM) $(AAC_SBR_QMF_SYNTH_WINDOW_AVX2)
AAC_SBR_QMF_SYNTH_MOD_SRC := src/decoders/modern/aac/sauti_aac_sbr_qmf_synth_mod.c $(AAC_SBR_QMF_SYNTH_MOD_ASM) $(AAC_SBR_QMF_SYNTH_MOD_AVX2)
AAC_SBR_QMF_ANALYSIS_SRC := src/decoders/modern/aac/sauti_aac_sbr_qmf_analysis.c src/decoders/modern/aac/sauti_aac_sbr_ld_qmf.c src/decoders/modern/aac/sauti_aac_sbr_ld_table.c
AAC_SBR_QMF_FRAME_SRC := src/decoders/modern/aac/sauti_aac_sbr_qmf_frame.c
AAC_SBR_PAYLOAD_SRC := src/decoders/modern/aac/sauti_aac_sbr_payload.c
AAC_SBR_CRC_SRC := src/decoders/modern/aac/sauti_aac_sbr_crc.c
AAC_HE_ADTS_MONO_SRC := src/decoders/modern/aac/sauti_aac_he_adts_mono.c
AAC_HE_ADTS_QMF_SRC := src/decoders/modern/aac/sauti_aac_he_adts_qmf.c
AAC_HE_ADTS_CPE_SRC := src/decoders/modern/aac/sauti_aac_he_adts_cpe.c
AAC_HE_ADTS_CPE_QMF_SRC := src/decoders/modern/aac/sauti_aac_he_adts_cpe_qmf.c
AAC_ADTS_CRC_SRC := src/decoders/modern/aac/sauti_aac_adts_crc.c
AAC_LC_MULTI_SRC := src/decoders/modern/aac/sauti_aac_lc_multi.c
AAC_MP4_SRC := src/decoders/modern/aac/sauti_aac_mp4.c src/decoders/modern/aac/sauti_decoder_aac_mp4.c src/decoders/modern/aac/sauti_aac_latm.c src/decoders/modern/aac/sauti_decoder_aac_latm.c src/decoders/modern/aac/sauti_aac_adif.c src/decoders/modern/aac/sauti_decoder_aac_adif.c
AAC_NATIVE_ADAPTER_DEPS = $(sort $(AAC_NATIVE_ADAPTER_SRC) $(AAC_CPE_SRC) $(AAC_LC_MULTI_SRC) $(AAC_MP4_SRC) $(AAC_SBR_ADTS_CPE_DEPS) $(AAC_SBR_CHANNEL_CLI_SRC) src/decoders/modern/aac/sauti_decoder_aac_he.c src/decoders/modern/aac/sauti_aac_er.c src/decoders/modern/aac/sauti_aac_eld_window.c src/decoders/modern/aac/sauti_aac_lcn.c src/decoders/modern/aac/sauti_aac_pred.c src/decoders/modern/aac/sauti_aac_ltp.c src/decoders/modern/aac/sauti_aac_lcm.c src/decoders/modern/aac/sauti_usac.c src/decoders/modern/aac/sauti_usac_ac.c src/decoders/modern/aac/sauti_usac_tables.c src/decoders/modern/aac/sauti_usac_mdst.c src/decoders/modern/aac/sauti_usac_sbr.c src/decoders/modern/aac/sauti_usac_lpd.c src/decoders/modern/aac/sauti_usac_lpd_lpc.c src/decoders/modern/aac/sauti_usac_lpd_dec.c src/decoders/modern/aac/sauti_usac_lpd_tables.c src/decoders/modern/aac/sauti_usac_pvc.c src/decoders/modern/aac/sauti_usac_pvc_tables.c src/decoders/modern/aac/sauti_usac_hbe.c src/decoders/modern/aac/sauti_usac_hbe_tables.c src/decoders/modern/aac/sauti_usac_mps.c src/decoders/modern/aac/sauti_aac_eld_sac.c src/decoders/modern/aac/sauti_unidrc.c src/decoders/modern/aac/sauti_unidrc_run.c src/decoders/modern/aac/sauti_unidrc_tables.c src/decoders/modern/aac/sauti_aac_sbr_mpsld.c src/decoders/modern/aac/sauti_aac_sbr_mpsld_table.c src/decoders/modern/aac/sauti_aac_eld_sac_dec.c src/decoders/modern/aac/sauti_aac_eld_sac_tables.c src/decoders/modern/aac/sauti_usac_mps_tables.c src/decoders/modern/aac/sauti_usac_mps_dec.c)
AAC_LC_SECTIONS_SRC := src/decoders/modern/aac/sauti_aac_lc_sections.c
AAC_HUFFMAN_SRC := src/decoders/modern/aac/sauti_aac_huffman.c
AAC_HUFFMAN_ASM := $(OBJDIR)/sauti_aac_huffman.o
AAC_SPECTRAL_SRC := src/decoders/modern/aac/sauti_aac_spectral.c
AAC_TABLES_SRC := src/decoders/modern/aac/sauti_aac_tables.c
AAC_SCALEFACTORS_SRC := src/decoders/modern/aac/sauti_aac_scalefactors.c
AAC_BANDS_SRC := src/decoders/modern/aac/sauti_aac_bands.c
AAC_SPECTRAL_DATA_SRC := src/decoders/modern/aac/sauti_aac_spectral_data.c
AAC_DEQUANT_SRC := src/decoders/modern/aac/sauti_aac_dequant.c
AAC_DEQUANT_ASM := $(OBJDIR)/sauti_aac_dequant.o
AAC_IMDCT_SRC := src/decoders/modern/aac/sauti_aac_imdct.c
AAC_IMDCT_ASM := $(OBJDIR)/sauti_aac_imdct.o
AAC_WINDOW_SRC := src/decoders/modern/aac/sauti_aac_window.c
AAC_LC_SYNTH_SRC := src/decoders/modern/aac/sauti_aac_lc_synth.c
AAC_LC_FRAME_SRC := src/decoders/modern/aac/sauti_aac_lc_frame.c $(AAC_ADTS_CRC_SRC)
AAC_PNS_SRC := src/decoders/modern/aac/sauti_aac_pns.c
AAC_TNS_SRC := src/decoders/modern/aac/sauti_aac_tns.c
AAC_SPECTRAL_ASM := $(OBJDIR)/sauti_aac_spectral.o
AAC_DSP_ASM := $(OBJDIR)/sauti_aac_dsp.o

ASMOBJ := $(OBJDIR)/sauti_core.o $(OBJDIR)/sauti_fft.o $(OBJDIR)/sauti_spectrum.o $(OBJDIR)/sauti_frame.o $(OBJDIR)/sauti_db_log.o $(OBJDIR)/sauti_audio_ring.o $(OBJDIR)/sauti_stereo_ring.o $(OBJDIR)/sauti_stereo_analysis.o $(OBJDIR)/sauti_stereo_bars.o $(OBJDIR)/sauti_peak_hold.o $(OBJDIR)/sauti_levels.o

.PHONY: all test demo window window-pipewire run-wayland run-pipewire-mic run-pipewire-monitor smoke-headless smoke-headless-pipewire clean
all: $(OBJDIR)/test_core $(OBJDIR)/test_fft $(OBJDIR)/test_frame $(OBJDIR)/test_db_log $(OBJDIR)/test_audio_ring $(OBJDIR)/test_stereo_ring $(OBJDIR)/test_stereo_analysis $(OBJDIR)/test_stereo_bars $(OBJDIR)/test_peak_hold $(OBJDIR)/test_auto_gain $(OBJDIR)/test_playlist $(OBJDIR)/test_transport $(OBJDIR)/test_app_state $(OBJDIR)/test_library $(OBJDIR)/test_wav $(OBJDIR)/test_record_ring $(OBJDIR)/test_audio_backend $(OBJDIR)/test_playback_time $(OBJDIR)/test_playback_backend $(OBJDIR)/test_decoder $(OBJDIR)/test_decoder_wav $(OBJDIR)/test_decoder_avr $(OBJDIR)/test_decoder_dvsm $(OBJDIR)/test_decoder_ym $(OBJDIR)/test_ym_archive $(OBJDIR)/test_decoder_mp3 $(OBJDIR)/test_decoder_voc $(OBJDIR)/test_decoder_aiff $(OBJDIR)/test_decoder_ffmpeg $(OBJDIR)/test_aac_headers $(OBJDIR)/test_aac_syntax $(OBJDIR)/test_aac_lc_sections $(OBJDIR)/test_aac_huffman $(OBJDIR)/test_aac_spectral $(OBJDIR)/test_aac_tables $(OBJDIR)/test_aac_scalefactors $(OBJDIR)/test_aac_spectral_data $(OBJDIR)/test_aac_dequant $(OBJDIR)/test_aac_imdct $(OBJDIR)/test_aac_window $(OBJDIR)/test_aac_lc_synth $(OBJDIR)/test_aac_lc_frame $(OBJDIR)/test_aac_pns $(OBJDIR)/test_aac_real_mono $(OBJDIR)/test_pcm_output $(OBJDIR)/test_decoder_vorbis $(OBJDIR)/test_decoder_midi $(OBJDIR)/test_decoder_opus $(OBJDIR)/sauti_demo

$(OBJDIR):
	mkdir -p $@

$(YM_ASM): src/decoders/retro/atari/sauti_decoder_ym.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(OBJDIR)/test_decoder_ym: tests/test_decoder_ym.c include/sauti_decoder_ym.h include/sauti_decoder.h src/sauti_decoder.c $(YM_SRC) $(YM_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_decoder_ym.c src/sauti_decoder.c $(YM_SRC) $(YM_ASM) -no-pie

# Standalone, dependency-light CPU benchmark (not part of the pass/fail tests).
$(OBJDIR)/bench_decoder_ym: tests/bench_decoder_ym.c include/sauti_decoder_ym.h include/sauti_decoder.h src/sauti_decoder.c $(YM_SRC) $(YM_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/bench_decoder_ym.c src/sauti_decoder.c $(YM_SRC) $(YM_ASM) -no-pie

.PHONY: bench-ym
bench-ym: $(OBJDIR)/bench_decoder_ym
	./$(OBJDIR)/bench_decoder_ym

# PCM reference comparison helper regression (Python standard library only).
.PHONY: test-ym-reference-tools
test-ym-reference-tools:
	python3 tests/test_compare_ym_pcm.py

# Controlled ST-Sound comparisons: optional external reference binary.
STSOUND_YM2WAV ?= $(HOME)/AYO-Narzedzia/StSound/build/Ym2Wav/Ym2Wav
.PHONY: compare-ym-reference
compare-ym-reference: $(OBJDIR)/render_decoder_ym
	python3 tools/run_ym_reference.py --sauti ./$(OBJDIR)/render_decoder_ym --stsound "$(STSOUND_YM2WAV)"

# Isolated A/B reference curve build. Production YM_ASM is never replaced.
$(OBJDIR)/sauti_decoder_ym_reference_dac.o: src/decoders/retro/atari/sauti_decoder_ym.asm | $(OBJDIR)
	$(NASM) -DSAUTI_YM_REFERENCE_DAC -f elf64 -o $@ $<

$(OBJDIR)/render_decoder_ym_reference_dac: tests/render_decoder_ym.c src/sauti_decoder.c $(YM_SRC) $(YM_ARCHIVE_SRC) $(OBJDIR)/sauti_decoder_ym_reference_dac.o | $(OBJDIR)
	$(CC) $(CFLAGS) $(ARCHIVE_FLAGS) -Iinclude -o $@ tests/render_decoder_ym.c src/sauti_decoder.c $(YM_SRC) $(OBJDIR)/sauti_decoder_ym_reference_dac.o $(YM_ARCHIVE_SRC) $(ARCHIVE_LIBS) -lm -no-pie

.PHONY: compare-ym-reference-dac
compare-ym-reference-dac: $(OBJDIR)/render_decoder_ym_reference_dac
	python3 tools/run_ym_reference.py --sauti ./$(OBJDIR)/render_decoder_ym_reference_dac --stsound "$(STSOUND_YM2WAV)" --output $(OBJDIR)/ym-reference/reference-dac --only envelope_09 envelope_0a envelope_0c envelope_0e envelope_restart tone_a tone_abc

# Offline A/B check on already rendered reference WAVs; no application DSP changes.
.PHONY: compare-ym-filter-gain
compare-ym-filter-gain:
	python3 tools/compare_ym_pcm.py $(OBJDIR)/ym-reference/reference-dac/tone_abc.sauti.wav $(OBJDIR)/ym-reference/reference-dac/tone_abc.stsound.wav --stsound-filter --fit-gain
	python3 tools/compare_ym_pcm.py $(OBJDIR)/ym-reference/reference-dac/envelope_0e.sauti.wav $(OBJDIR)/ym-reference/reference-dac/envelope_0e.stsound.wav --stsound-filter --fit-gain --envelope-rms

# Candidate gain A/B: same WAVs, no rebuild or modifications to the player.
.PHONY: compare-ym-gain-half
compare-ym-gain-half:
	python3 tools/compare_ym_pcm.py $(OBJDIR)/ym-reference/reference-dac/tone_abc.sauti.wav $(OBJDIR)/ym-reference/reference-dac/tone_abc.stsound.wav --stsound-filter --candidate-gain 0.5 --fit-gain
	python3 tools/compare_ym_pcm.py $(OBJDIR)/ym-reference/reference-dac/envelope_0e.sauti.wav $(OBJDIR)/ym-reference/reference-dac/envelope_0e.stsound.wav --stsound-filter --candidate-gain 0.5 --fit-gain --envelope-rms

# Diagnose sample timing of envelope 0E against ST-Sound; no synthesis changes.
.PHONY: compare-ym-envelope-lag
compare-ym-envelope-lag:
	python3 tools/compare_ym_pcm.py $(OBJDIR)/ym-reference/reference-dac/envelope_0e.sauti.wav $(OBJDIR)/ym-reference/reference-dac/envelope_0e.stsound.wav --stsound-filter --candidate-gain 0.5 --local-lag-report 250 --max-lag 64 --window 4096

# Isolated pre-tick envelope experiment, using the same ST-Sound DAC curve.
# Production YM object and player stay untouched.
$(OBJDIR)/sauti_decoder_ym_env_pre_tick.o: src/decoders/retro/atari/sauti_decoder_ym.asm | $(OBJDIR)
	$(NASM) -DSAUTI_YM_REFERENCE_DAC -DSAUTI_YM_ENV_SAMPLE_BEFORE_TICK -f elf64 -o $@ $<

$(OBJDIR)/render_decoder_ym_env_pre_tick: tests/render_decoder_ym.c src/sauti_decoder.c $(YM_SRC) $(YM_ARCHIVE_SRC) $(OBJDIR)/sauti_decoder_ym_env_pre_tick.o | $(OBJDIR)
	$(CC) $(CFLAGS) $(ARCHIVE_FLAGS) -Iinclude -o $@ tests/render_decoder_ym.c src/sauti_decoder.c $(YM_SRC) $(YM_ARCHIVE_SRC) $(OBJDIR)/sauti_decoder_ym_env_pre_tick.o $(ARCHIVE_LIBS) -lm -no-pie

.PHONY: compare-ym-env-pre-tick
compare-ym-env-pre-tick: $(OBJDIR)/render_decoder_ym_env_pre_tick
	python3 tools/run_ym_reference.py --sauti ./$(OBJDIR)/render_decoder_ym_env_pre_tick --stsound "$(STSOUND_YM2WAV)" --output $(OBJDIR)/ym-reference/env-pre-tick --only envelope_09 envelope_0a envelope_0c envelope_0e
	python3 tools/compare_ym_pcm.py $(OBJDIR)/ym-reference/env-pre-tick/envelope_0e.sauti.wav $(OBJDIR)/ym-reference/env-pre-tick/envelope_0e.stsound.wav --stsound-filter --candidate-gain 0.5 --fit-gain --envelope-rms

# A/B table for four YM envelope shapes, using independently rendered
# reference-DAC and pre-tick experimental WAVs.
.PHONY: compare-ym-envelope-ab
compare-ym-envelope-ab: $(OBJDIR)/render_decoder_ym_reference_dac $(OBJDIR)/render_decoder_ym_env_pre_tick
	python3 tools/run_ym_reference.py --sauti ./$(OBJDIR)/render_decoder_ym_reference_dac --stsound "$(STSOUND_YM2WAV)" --output $(OBJDIR)/ym-reference/reference-dac --only envelope_09 envelope_0a envelope_0c envelope_0e
	python3 tools/run_ym_reference.py --sauti ./$(OBJDIR)/render_decoder_ym_env_pre_tick --stsound "$(STSOUND_YM2WAV)" --output $(OBJDIR)/ym-reference/env-pre-tick --only envelope_09 envelope_0a envelope_0c envelope_0e
	python3 tools/compare_ym_envelope_ab.py --root $(OBJDIR)/ym-reference

# Inspect envelope levels and internal state without DAC, audio output or SDL.
$(OBJDIR)/trace_ym_envelope: tests/trace_ym_envelope.c $(YM_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ tests/trace_ym_envelope.c $(YM_ASM) -no-pie

$(OBJDIR)/trace_ym_envelope_pre_tick: tests/trace_ym_envelope.c $(OBJDIR)/sauti_decoder_ym_env_pre_tick.o | $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ tests/trace_ym_envelope.c $(OBJDIR)/sauti_decoder_ym_env_pre_tick.o -no-pie

.PHONY: trace-ym-envelope
trace-ym-envelope: $(OBJDIR)/trace_ym_envelope $(OBJDIR)/trace_ym_envelope_pre_tick
	@echo "Existing mode (shape 0C):"
	@./$(OBJDIR)/trace_ym_envelope 12 1 67 | head -n 68
	@echo "Pre-tick experimental mode (shape 0C):"
	@./$(OBJDIR)/trace_ym_envelope_pre_tick 12 1 67 | head -n 68

# Period sweep for shape 0C. Both reference-DAC variants are isolated builds.
.PHONY: compare-ym-envelope-periods
compare-ym-envelope-periods: $(OBJDIR)/render_decoder_ym_reference_dac $(OBJDIR)/render_decoder_ym_env_pre_tick
	python3 tools/compare_ym_envelope_periods.py --old ./$(OBJDIR)/render_decoder_ym_reference_dac --pre-tick ./$(OBJDIR)/render_decoder_ym_env_pre_tick --stsound "$(STSOUND_YM2WAV)"

# Fixed-volume PSG tone DAC diagnostic; separate reference-DAC build.
.PHONY: compare-ym-dac-levels
compare-ym-dac-levels: $(OBJDIR)/render_decoder_ym_reference_dac
	python3 tools/compare_ym_dac_levels.py --sauti ./$(OBJDIR)/render_decoder_ym_reference_dac --stsound "$(STSOUND_YM2WAV)"

# Diagnostic: all 32 slow YM envelope levels against ST-Sound, no player changes.
.PHONY: compare-ym-envelope-32-levels
compare-ym-envelope-32-levels: $(OBJDIR)/render_decoder_ym_reference_dac
	python3 tools/compare_ym_envelope_32_levels.py --sauti ./$(OBJDIR)/render_decoder_ym_reference_dac --stsound "$(STSOUND_YM2WAV)"

# Atari ST YM3b regression across all three listening modes.
# Separate binary names keep all existing player/decoder artifacts untouched.
$(OBJDIR)/test_ym_listening_classic: tests/test_ym_listening.c src/sauti_decoder.c $(YM_SRC) $(YM_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_ym_listening.c src/sauti_decoder.c $(YM_SRC) $(YM_ASM) -no-pie

$(OBJDIR)/test_ym_listening_reference: tests/test_ym_listening.c src/sauti_decoder.c $(YM_SRC) $(OBJDIR)/sauti_decoder_ym_reference_dac.o | $(OBJDIR)
	$(CC) $(CFLAGS) -DSAUTI_YM_LISTEN_REFERENCE -Iinclude -o $@ tests/test_ym_listening.c src/sauti_decoder.c $(YM_SRC) $(OBJDIR)/sauti_decoder_ym_reference_dac.o -no-pie

$(OBJDIR)/test_ym_listening_pre_tick: tests/test_ym_listening.c src/sauti_decoder.c $(YM_SRC) $(OBJDIR)/sauti_decoder_ym_env_pre_tick.o | $(OBJDIR)
	$(CC) $(CFLAGS) -DSAUTI_YM_LISTEN_REFERENCE -Iinclude -o $@ tests/test_ym_listening.c src/sauti_decoder.c $(YM_SRC) $(OBJDIR)/sauti_decoder_ym_env_pre_tick.o -no-pie

.PHONY: test-ym-listening-modes
test-ym-listening-modes: $(OBJDIR)/test_ym_listening_classic $(OBJDIR)/test_ym_listening_reference $(OBJDIR)/test_ym_listening_pre_tick
	./$(OBJDIR)/test_ym_listening_classic
	./$(OBJDIR)/test_ym_listening_reference
	./$(OBJDIR)/test_ym_listening_pre_tick

# Parser robustness suite; intentionally independent of SDL/PipeWire.
$(OBJDIR)/test_ym_malformed: tests/test_ym_malformed.c include/sauti_decoder_ym.h include/sauti_decoder.h src/sauti_decoder.c $(YM_SRC) $(YM_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_ym_malformed.c src/sauti_decoder.c $(YM_SRC) $(YM_ASM) -no-pie

# Keep diagnostic binaries separate so the standard benchmark remains optimized.
YM_SAN_FLAGS := -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
$(OBJDIR)/test_ym_malformed_san: tests/test_ym_malformed.c include/sauti_decoder_ym.h include/sauti_decoder.h src/sauti_decoder.c $(YM_SRC) $(YM_ASM) | $(OBJDIR)
	$(CC) $(YM_SAN_FLAGS) -Iinclude -o $@ tests/test_ym_malformed.c src/sauti_decoder.c $(YM_SRC) $(YM_ASM) -no-pie

$(OBJDIR)/test_decoder_ym_san: tests/test_decoder_ym.c include/sauti_decoder_ym.h include/sauti_decoder.h src/sauti_decoder.c $(YM_SRC) $(YM_ASM) | $(OBJDIR)
	$(CC) $(YM_SAN_FLAGS) -Iinclude -o $@ tests/test_decoder_ym.c src/sauti_decoder.c $(YM_SRC) $(YM_ASM) -no-pie

# YM2 Mad Max synthetic PCM regression (normal and sanitizer builds).
$(OBJDIR)/test_ym2_madmax_pcm: tests/test_ym2_madmax_pcm.c include/sauti_decoder_ym.h include/sauti_decoder.h src/sauti_decoder.c $(YM_SRC) $(YM_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_ym2_madmax_pcm.c src/sauti_decoder.c $(YM_SRC) $(YM_ASM) -no-pie

$(OBJDIR)/test_ym2_madmax_pcm_san: tests/test_ym2_madmax_pcm.c include/sauti_decoder_ym.h include/sauti_decoder.h src/sauti_decoder.c $(YM_SRC) $(YM_ASM) | $(OBJDIR)
	$(CC) $(YM_SAN_FLAGS) -Iinclude -o $@ tests/test_ym2_madmax_pcm.c src/sauti_decoder.c $(YM_SRC) $(YM_ASM) -no-pie

.PHONY: test-ym-sanitize
test-ym-sanitize: $(OBJDIR)/test_decoder_ym_san $(OBJDIR)/test_ym_malformed_san $(OBJDIR)/test_ym2_madmax_pcm_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_decoder_ym_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_ym_malformed_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_ym2_madmax_pcm_san

.PHONY: test-ym-extended
# Full YM regression including malformed/truncated input corpus.
test-ym-extended: $(OBJDIR)/test_decoder_ym $(OBJDIR)/test_ym_malformed $(OBJDIR)/test_ym_archive $(OBJDIR)/test_ym2_madmax_pcm
	./$(OBJDIR)/test_decoder_ym
	./$(OBJDIR)/test_ym_malformed
	./$(OBJDIR)/test_ym_archive
	./$(OBJDIR)/test_ym2_madmax_pcm

$(OBJDIR)/render_decoder_ym: tests/render_decoder_ym.c include/sauti_decoder_ym.h include/sauti_decoder.h include/sauti_ym_archive.h src/sauti_decoder.c $(YM_SRC) $(YM_ASM) $(YM_ARCHIVE_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) $(ARCHIVE_FLAGS) -Iinclude -o $@ tests/render_decoder_ym.c src/sauti_decoder.c $(YM_SRC) $(YM_ASM) $(YM_ARCHIVE_SRC) $(ARCHIVE_LIBS) -lm -no-pie

$(OBJDIR)/probe_ym_archive: tests/probe_ym_archive.c include/sauti_ym_archive.h $(YM_ARCHIVE_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) $(ARCHIVE_FLAGS) -Iinclude -o $@ tests/probe_ym_archive.c $(YM_ARCHIVE_SRC) $(ARCHIVE_LIBS)

$(OBJDIR)/test_ym_archive: tests/test_ym_archive.c include/sauti_ym_archive.h $(YM_ARCHIVE_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) $(ARCHIVE_FLAGS) -Iinclude -o $@ tests/test_ym_archive.c $(YM_ARCHIVE_SRC) $(ARCHIVE_LIBS)

$(DVSM_ASM): src/decoders/retro/atari/sauti_decoder_dvsm.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(OBJDIR)/test_decoder_dvsm_san: tests/test_decoder_dvsm.c include/sauti_decoder_dvsm.h include/sauti_decoder.h src/sauti_decoder.c $(DVSM_SRC) $(DVSM_ASM) | $(OBJDIR)
	$(CC) $(YM_SAN_FLAGS) -Iinclude -o $@ tests/test_decoder_dvsm.c src/sauti_decoder.c $(DVSM_SRC) $(DVSM_ASM) -lm -no-pie

.PHONY: test-dvsm
test-dvsm: $(OBJDIR)/test_decoder_dvsm $(OBJDIR)/test_decoder_dvsm_san
	./$(OBJDIR)/test_decoder_dvsm
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_decoder_dvsm_san

$(OBJDIR)/test_decoder_wavpack: tests/test_decoder_wavpack.c include/sauti_decoder_wavpack.h $(WAVPACK_SRC) src/sauti_decoder.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(WAVPACK_FLAGS) -Iinclude -o $@ tests/test_decoder_wavpack.c src/sauti_decoder.c $(WAVPACK_SRC) $(WAVPACK_LIBS) -no-pie

.PHONY: test-wavpack
test-wavpack: $(OBJDIR)/test_decoder_wavpack
	./$(OBJDIR)/test_decoder_wavpack

$(OBJDIR)/test_decoder_dtm: tests/test_decoder_dtm.c include/sauti_decoder_dtm.h include/sauti_decoder.h src/sauti_decoder.c $(DTM_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) $(XMP_FLAGS) -Iinclude -o $@ tests/test_decoder_dtm.c src/sauti_decoder.c $(DTM_SRC) $(XMP_LIBS) -no-pie

.PHONY: test-dtm
test-dtm: $(OBJDIR)/test_decoder_dtm
	./$(OBJDIR)/test_decoder_dtm

$(OBJDIR)/test_decoder_dvsm: tests/test_decoder_dvsm.c include/sauti_decoder_dvsm.h src/sauti_decoder.c $(DVSM_SRC) $(DVSM_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_decoder_dvsm.c src/sauti_decoder.c $(DVSM_SRC) $(DVSM_ASM) -lm -no-pie

$(OBJDIR)/dump_decoder_dvsm: tests/dump_decoder_dvsm.c include/sauti_decoder_dvsm.h src/sauti_decoder.c $(DVSM_SRC) $(DVSM_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/dump_decoder_dvsm.c src/sauti_decoder.c $(DVSM_SRC) $(DVSM_ASM) -lm -no-pie

$(AVR_ASM): src/decoders/retro/atari/sauti_decoder_avr.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(OBJDIR)/test_decoder_avr_san: tests/test_decoder_avr.c include/sauti_decoder_avr.h include/sauti_decoder.h src/sauti_decoder.c $(AVR_SRC) $(AVR_ASM) | $(OBJDIR)
	$(CC) $(YM_SAN_FLAGS) -Iinclude -o $@ tests/test_decoder_avr.c src/sauti_decoder.c $(AVR_SRC) $(AVR_ASM) -no-pie

.PHONY: test-avr
test-avr: $(OBJDIR)/test_decoder_avr $(OBJDIR)/test_decoder_avr_san
	./$(OBJDIR)/test_decoder_avr
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_decoder_avr_san

$(OBJDIR)/test_decoder_avr: tests/test_decoder_avr.c include/sauti_decoder_avr.h include/sauti_decoder.h src/sauti_decoder.c $(AVR_SRC) $(AVR_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_decoder_avr.c src/sauti_decoder.c $(AVR_SRC) $(AVR_ASM) -no-pie

$(OBJDIR)/sauti_core.o: src/sauti_core.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(OBJDIR)/sauti_fft.o: src/sauti_fft.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(OBJDIR)/sauti_spectrum.o: src/sauti_spectrum.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(OBJDIR)/sauti_frame.o: src/sauti_frame.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(OBJDIR)/sauti_db_log.o: src/sauti_db_log.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(OBJDIR)/sauti_audio_ring.o: src/sauti_audio_ring.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(OBJDIR)/sauti_stereo_ring.o: src/sauti_stereo_ring.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(OBJDIR)/sauti_stereo_analysis.o: src/sauti_stereo_analysis.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(OBJDIR)/sauti_stereo_bars.o: src/sauti_stereo_bars.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(OBJDIR)/sauti_peak_hold.o: src/sauti_peak_hold.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(OBJDIR)/sauti_levels.o: src/sauti_levels.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

# Optional Linux-only end-to-end MP3 -> float PCM -> SDL queued audio smoke test.
$(OBJDIR)/demo_mp3_playback_sdl: tests/demo_mp3_playback_sdl.c include/sauti_decoder.h include/sauti_decoder_mp3.h include/sauti_pcm_output.h src/sauti_decoder.c src/decoders/modern/sauti_decoder_mp3.c src/sauti_pcm_output.c third_party/minimp3/minimp3.h third_party/minimp3/minimp3_ex.h | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude $(shell $(PKG_CONFIG) --cflags sdl2) -o $@ tests/demo_mp3_playback_sdl.c src/sauti_decoder.c src/decoders/modern/sauti_decoder_mp3.c src/sauti_pcm_output.c $(shell $(PKG_CONFIG) --libs sdl2)

$(OBJDIR)/test_pcm_output: tests/test_pcm_output.c include/sauti_pcm_output.h src/sauti_pcm_output.c | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_pcm_output.c src/sauti_pcm_output.c

$(OBJDIR)/test_decoder_opus: tests/test_decoder_opus.c include/sauti_decoder_opus.h src/decoders/modern/sauti_decoder_opus.c src/sauti_decoder.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(OPUS_FLAGS) -Iinclude -o $@ tests/test_decoder_opus.c src/sauti_decoder.c src/decoders/modern/sauti_decoder_opus.c $(OPUS_LIBS)

$(OBJDIR)/test_decoder_midi: tests/test_decoder_midi.c include/sauti_decoder_midi.h src/decoders/modern/sauti_decoder_midi.c src/sauti_decoder.c third_party/tml/tml.h | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_decoder_midi.c src/sauti_decoder.c src/decoders/modern/sauti_decoder_midi.c -lm

$(OBJDIR)/test_decoder_flac: tests/test_decoder_flac.c include/sauti_decoder_flac.h src/sauti_decoder.c src/decoders/modern/sauti_decoder_flac.c third_party/dr_libs/dr_flac.h | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_decoder_flac.c src/sauti_decoder.c src/decoders/modern/sauti_decoder_flac.c -lm

$(OBJDIR)/test_decoder_vorbis: tests/test_decoder_vorbis.c include/sauti_decoder_vorbis.h src/decoders/modern/sauti_decoder_vorbis.c src/sauti_decoder.c third_party/stb/stb_vorbis.c | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_decoder_vorbis.c src/sauti_decoder.c src/decoders/modern/sauti_decoder_vorbis.c -lm

$(OBJDIR)/test_decoder_mp3: tests/test_decoder_mp3.c include/sauti_decoder.h include/sauti_decoder_mp3.h src/sauti_decoder.c src/decoders/modern/sauti_decoder_mp3.c third_party/minimp3/minimp3.h third_party/minimp3/minimp3_ex.h | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_decoder_mp3.c src/sauti_decoder.c src/decoders/modern/sauti_decoder_mp3.c

# AAC family parser is tested independently of the existing FFmpeg decoder.
# This target does not claim to decode AAC to PCM.
$(AAC_HEADERS_ASM): src/decoders/modern/aac/sauti_aac_headers.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(OBJDIR)/test_aac_headers: tests/test_aac_headers.c include/sauti_aac_headers.h $(AAC_CONFIG_SRC) $(AAC_HEADERS_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_headers.c $(AAC_CONFIG_SRC) $(AAC_HEADERS_ASM) -no-pie

.PHONY: test-aac-headers
test-aac-headers: $(OBJDIR)/test_aac_headers
	./$(OBJDIR)/test_aac_headers

$(AAC_DSP_ASM): src/decoders/modern/aac/sauti_aac_dsp.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(AAC_PS_MIX_ASM): src/decoders/modern/aac/sauti_aac_ps_mix.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(AAC_SBR_QMF_SYNTH_WINDOW_ASM): src/decoders/modern/aac/sauti_aac_sbr_qmf_synth_window.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(AAC_SBR_QMF_SYNTH_MOD_ASM): src/decoders/modern/aac/sauti_aac_sbr_qmf_synth_mod.asm src/decoders/modern/aac/sauti_aac_sbr_qmf_synth_tables.inc | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(AAC_SBR_QMF_SYNTH_WINDOW_AVX2): src/decoders/modern/aac/sauti_aac_sbr_qmf_synth_window_avx2.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(AAC_SBR_QMF_SYNTH_MOD_AVX2): src/decoders/modern/aac/sauti_aac_sbr_qmf_synth_mod_avx2.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(OBJDIR)/test_aac_syntax: tests/test_aac_syntax.c include/sauti_aac_syntax.h $(AAC_SYNTAX_SRC) $(AAC_DSP_ASM) $(AAC_HEADERS_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_syntax.c $(AAC_SYNTAX_SRC) $(AAC_HEADERS_ASM) $(AAC_DSP_ASM) -no-pie

.PHONY: test-aac-syntax
test-aac-syntax: $(OBJDIR)/test_aac_syntax
	./$(OBJDIR)/test_aac_syntax

$(OBJDIR)/test_aac_ps_map: tests/test_aac_ps_map.c include/sauti_aac_ps_map.h $(AAC_PS_MAP_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_ps_map.c $(AAC_PS_MAP_SRC)

.PHONY: test-aac-ps-map
test-aac-ps-map: $(OBJDIR)/test_aac_ps_map
	./$(OBJDIR)/test_aac_ps_map

$(OBJDIR)/test_aac_ps_hybrid: tests/test_aac_ps_hybrid.c include/sauti_aac_ps_hybrid.h $(AAC_PS_HYBRID_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_ps_hybrid.c $(AAC_PS_HYBRID_SRC)

.PHONY: test-aac-ps-hybrid
test-aac-ps-hybrid: $(OBJDIR)/test_aac_ps_hybrid
	./$(OBJDIR)/test_aac_ps_hybrid

$(OBJDIR)/test_aac_ps_qmf_high: tests/test_aac_ps_qmf_high.c include/sauti_aac_ps_qmf_high.h src/decoders/modern/aac/sauti_aac_ps_qmf_high.c $(AAC_PS_HYBRID_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_ps_qmf_high.c src/decoders/modern/aac/sauti_aac_ps_qmf_high.c $(AAC_PS_HYBRID_SRC) -lm

.PHONY: test-aac-ps-qmf-high
test-aac-ps-qmf-high: $(OBJDIR)/test_aac_ps_qmf_high
	./$(OBJDIR)/test_aac_ps_qmf_high

$(OBJDIR)/test_aac_ps_qmf_low: tests/test_aac_ps_qmf_low.c include/sauti_aac_ps_qmf_low.h src/decoders/modern/aac/sauti_aac_ps_qmf_low.c $(AAC_PS_HYBRID_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_ps_qmf_low.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c $(AAC_PS_HYBRID_SRC) -lm

.PHONY: test-aac-ps-qmf-low
test-aac-ps-qmf-low: $(OBJDIR)/test_aac_ps_qmf_low
	./$(OBJDIR)/test_aac_ps_qmf_low

$(OBJDIR)/test_aac_ps_qmf_bank: tests/test_aac_ps_qmf_bank.c include/sauti_aac_ps_qmf_bank.h src/decoders/modern/aac/sauti_aac_ps_qmf_bank.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c src/decoders/modern/aac/sauti_aac_ps_qmf_high.c $(AAC_PS_HYBRID_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_ps_qmf_bank.c src/decoders/modern/aac/sauti_aac_ps_qmf_bank.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c src/decoders/modern/aac/sauti_aac_ps_qmf_high.c $(AAC_PS_HYBRID_SRC) -lm

.PHONY: test-aac-ps-qmf-bank
test-aac-ps-qmf-bank: $(OBJDIR)/test_aac_ps_qmf_bank
	./$(OBJDIR)/test_aac_ps_qmf_bank

$(OBJDIR)/test_aac_ps_qmf_reference: tests/test_aac_ps_qmf_reference.c include/sauti_aac_ps_qmf_low.h src/decoders/modern/aac/sauti_aac_ps_qmf_low.c $(AAC_PS_HYBRID_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_ps_qmf_reference.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c $(AAC_PS_HYBRID_SRC) -lm

.PHONY: test-aac-ps-qmf-reference
test-aac-ps-qmf-reference: $(OBJDIR)/test_aac_ps_qmf_reference
	./$(OBJDIR)/test_aac_ps_qmf_reference

$(OBJDIR)/test_aac_ps_transient: tests/test_aac_ps_transient.c include/sauti_aac_ps_transient.h src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_ps_transient.c src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) -lm

.PHONY: test-aac-ps-transient
test-aac-ps-transient: $(OBJDIR)/test_aac_ps_transient
	./$(OBJDIR)/test_aac_ps_transient

$(OBJDIR)/test_aac_ps_decorrelate: tests/test_aac_ps_decorrelate.c include/sauti_aac_ps_decorrelate.h src/decoders/modern/aac/sauti_aac_ps_decorrelate.c $(AAC_PS_HYBRID_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_ps_decorrelate.c src/decoders/modern/aac/sauti_aac_ps_decorrelate.c $(AAC_PS_HYBRID_SRC) -lm

.PHONY: test-aac-ps-decorrelate
test-aac-ps-decorrelate: $(OBJDIR)/test_aac_ps_decorrelate
	./$(OBJDIR)/test_aac_ps_decorrelate

$(OBJDIR)/test_aac_ps_decorrelator: tests/test_aac_ps_decorrelator.c include/sauti_aac_ps_decorrelator.h src/decoders/modern/aac/sauti_aac_ps_decorrelator.c src/decoders/modern/aac/sauti_aac_ps_decorrelate.c src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_ps_decorrelator.c src/decoders/modern/aac/sauti_aac_ps_decorrelator.c src/decoders/modern/aac/sauti_aac_ps_decorrelate.c src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) -lm

.PHONY: test-aac-ps-decorrelator
test-aac-ps-decorrelator: $(OBJDIR)/test_aac_ps_decorrelator
	./$(OBJDIR)/test_aac_ps_decorrelator

$(OBJDIR)/test_aac_ps_stereo: tests/test_aac_ps_stereo.c include/sauti_aac_ps_stereo.h src/decoders/modern/aac/sauti_aac_ps_stereo.c src/decoders/modern/aac/sauti_aac_ps_qmf_bank.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c src/decoders/modern/aac/sauti_aac_ps_qmf_high.c src/decoders/modern/aac/sauti_aac_ps_decorrelator.c src/decoders/modern/aac/sauti_aac_ps_decorrelate.c src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) $(AAC_PS_MIX_SRC) $(AAC_PS_MIX_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_ps_stereo.c src/decoders/modern/aac/sauti_aac_ps_stereo.c src/decoders/modern/aac/sauti_aac_ps_qmf_bank.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c src/decoders/modern/aac/sauti_aac_ps_qmf_high.c src/decoders/modern/aac/sauti_aac_ps_decorrelator.c src/decoders/modern/aac/sauti_aac_ps_decorrelate.c src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) $(AAC_PS_MIX_SRC) $(AAC_PS_MIX_ASM) -lm -no-pie

.PHONY: test-aac-ps-stereo
test-aac-ps-stereo: $(OBJDIR)/test_aac_ps_stereo
	./$(OBJDIR)/test_aac_ps_stereo

$(OBJDIR)/test_aac_ps_hybrid_synth: tests/test_aac_ps_hybrid_synth.c include/sauti_aac_ps_hybrid_synth.h src/decoders/modern/aac/sauti_aac_ps_hybrid_synth.c $(AAC_PS_HYBRID_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_ps_hybrid_synth.c src/decoders/modern/aac/sauti_aac_ps_hybrid_synth.c $(AAC_PS_HYBRID_SRC) -lm

.PHONY: test-aac-ps-hybrid-synth
test-aac-ps-hybrid-synth: $(OBJDIR)/test_aac_ps_hybrid_synth
	./$(OBJDIR)/test_aac_ps_hybrid_synth

$(OBJDIR)/test_aac_ps_pcm_stereo: include/sauti_aac_ps_pcm_stereo.h include/sauti_aac_ps_stereo.h include/sauti_aac_ps_hybrid_synth.h include/sauti_aac_sbr_qmf_synth_mod.h tests/test_aac_ps_pcm_stereo.c src/decoders/modern/aac/sauti_aac_ps_pcm_stereo.c src/decoders/modern/aac/sauti_aac_ps_stereo.c src/decoders/modern/aac/sauti_aac_ps_hybrid_synth.c src/decoders/modern/aac/sauti_aac_ps_qmf_bank.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c src/decoders/modern/aac/sauti_aac_ps_qmf_high.c src/decoders/modern/aac/sauti_aac_ps_decorrelator.c src/decoders/modern/aac/sauti_aac_ps_decorrelate.c src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) $(AAC_PS_MIX_SRC) $(AAC_PS_MIX_ASM) $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_ps_pcm_stereo.c src/decoders/modern/aac/sauti_aac_ps_pcm_stereo.c src/decoders/modern/aac/sauti_aac_ps_stereo.c src/decoders/modern/aac/sauti_aac_ps_hybrid_synth.c src/decoders/modern/aac/sauti_aac_ps_qmf_bank.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c src/decoders/modern/aac/sauti_aac_ps_qmf_high.c src/decoders/modern/aac/sauti_aac_ps_decorrelator.c src/decoders/modern/aac/sauti_aac_ps_decorrelate.c src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) $(AAC_PS_MIX_SRC) $(AAC_PS_MIX_ASM) $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) -lm -no-pie

.PHONY: test-aac-ps-pcm-stereo
test-aac-ps-pcm-stereo: $(OBJDIR)/test_aac_ps_pcm_stereo
	./$(OBJDIR)/test_aac_ps_pcm_stereo

$(OBJDIR)/test_aac_he_ps_bridge: include/sauti_aac_he_ps_bridge.h include/sauti_aac_ps_render.h include/sauti_aac_ps_pcm_stereo.h tests/test_aac_he_ps_bridge.c src/decoders/modern/aac/sauti_aac_he_ps_bridge.c src/decoders/modern/aac/sauti_aac_ps_render.c src/decoders/modern/aac/sauti_aac_ps_map.c src/decoders/modern/aac/sauti_aac_ps_matrix.c src/decoders/modern/aac/sauti_aac_ps_phase.c src/decoders/modern/aac/sauti_aac_ps_pcm_stereo.c src/decoders/modern/aac/sauti_aac_ps_stereo.c src/decoders/modern/aac/sauti_aac_ps_hybrid_synth.c src/decoders/modern/aac/sauti_aac_ps_qmf_bank.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c src/decoders/modern/aac/sauti_aac_ps_qmf_high.c src/decoders/modern/aac/sauti_aac_ps_decorrelator.c src/decoders/modern/aac/sauti_aac_ps_decorrelate.c src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) $(AAC_PS_MIX_SRC) $(AAC_PS_MIX_ASM) $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_he_ps_bridge.c src/decoders/modern/aac/sauti_aac_he_ps_bridge.c src/decoders/modern/aac/sauti_aac_ps_render.c src/decoders/modern/aac/sauti_aac_ps_map.c src/decoders/modern/aac/sauti_aac_ps_matrix.c src/decoders/modern/aac/sauti_aac_ps_phase.c src/decoders/modern/aac/sauti_aac_ps_pcm_stereo.c src/decoders/modern/aac/sauti_aac_ps_stereo.c src/decoders/modern/aac/sauti_aac_ps_hybrid_synth.c src/decoders/modern/aac/sauti_aac_ps_qmf_bank.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c src/decoders/modern/aac/sauti_aac_ps_qmf_high.c src/decoders/modern/aac/sauti_aac_ps_decorrelator.c src/decoders/modern/aac/sauti_aac_ps_decorrelate.c src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) $(AAC_PS_MIX_SRC) $(AAC_PS_MIX_ASM) $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) -lm -no-pie

.PHONY: test-aac-he-ps-bridge
test-aac-he-ps-bridge: $(OBJDIR)/test_aac_he_ps_bridge
	./$(OBJDIR)/test_aac_he_ps_bridge

$(OBJDIR)/test_aac_he_ps_delay: include/sauti_aac_he_ps_bridge.h include/sauti_aac_ps_render.h include/sauti_aac_ps_pcm_stereo.h tests/test_aac_he_ps_delay.c include/sauti_aac_he_ps_delay.h src/decoders/modern/aac/sauti_aac_he_ps_delay.c src/decoders/modern/aac/sauti_aac_he_ps_bridge.c src/decoders/modern/aac/sauti_aac_ps_render.c src/decoders/modern/aac/sauti_aac_ps_map.c src/decoders/modern/aac/sauti_aac_ps_matrix.c src/decoders/modern/aac/sauti_aac_ps_phase.c src/decoders/modern/aac/sauti_aac_ps_pcm_stereo.c src/decoders/modern/aac/sauti_aac_ps_stereo.c src/decoders/modern/aac/sauti_aac_ps_hybrid_synth.c src/decoders/modern/aac/sauti_aac_ps_qmf_bank.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c src/decoders/modern/aac/sauti_aac_ps_qmf_high.c src/decoders/modern/aac/sauti_aac_ps_decorrelator.c src/decoders/modern/aac/sauti_aac_ps_decorrelate.c src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) $(AAC_PS_MIX_SRC) $(AAC_PS_MIX_ASM) $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_he_ps_delay.c src/decoders/modern/aac/sauti_aac_he_ps_delay.c src/decoders/modern/aac/sauti_aac_he_ps_bridge.c src/decoders/modern/aac/sauti_aac_ps_render.c src/decoders/modern/aac/sauti_aac_ps_map.c src/decoders/modern/aac/sauti_aac_ps_matrix.c src/decoders/modern/aac/sauti_aac_ps_phase.c src/decoders/modern/aac/sauti_aac_ps_pcm_stereo.c src/decoders/modern/aac/sauti_aac_ps_stereo.c src/decoders/modern/aac/sauti_aac_ps_hybrid_synth.c src/decoders/modern/aac/sauti_aac_ps_qmf_bank.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c src/decoders/modern/aac/sauti_aac_ps_qmf_high.c src/decoders/modern/aac/sauti_aac_ps_decorrelator.c src/decoders/modern/aac/sauti_aac_ps_decorrelate.c src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) $(AAC_PS_MIX_SRC) $(AAC_PS_MIX_ASM) $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) -lm -no-pie

.PHONY: test-aac-he-ps-delay
test-aac-he-ps-delay: $(OBJDIR)/test_aac_he_ps_delay
	./$(OBJDIR)/test_aac_he_ps_delay

$(OBJDIR)/test_aac_ps_matrix: tests/test_aac_ps_matrix.c include/sauti_aac_ps_matrix.h $(AAC_PS_MATRIX_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_ps_matrix.c $(AAC_PS_MATRIX_SRC) -lm

.PHONY: test-aac-ps-matrix
test-aac-ps-matrix: $(OBJDIR)/test_aac_ps_matrix
	./$(OBJDIR)/test_aac_ps_matrix

$(OBJDIR)/test_aac_ps_phase: tests/test_aac_ps_phase.c include/sauti_aac_ps_phase.h $(AAC_PS_PHASE_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_ps_phase.c $(AAC_PS_PHASE_SRC) -lm

.PHONY: test-aac-ps-phase
test-aac-ps-phase: $(OBJDIR)/test_aac_ps_phase
	./$(OBJDIR)/test_aac_ps_phase

$(OBJDIR)/test_aac_ps_mix: tests/test_aac_ps_mix.c include/sauti_aac_ps_mix.h $(AAC_PS_MIX_SRC) $(AAC_PS_MIX_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_ps_mix.c $(AAC_PS_MIX_SRC) $(AAC_PS_MIX_ASM) -lm -no-pie

.PHONY: test-aac-ps-mix
test-aac-ps-mix: $(OBJDIR)/test_aac_ps_mix
	./$(OBJDIR)/test_aac_ps_mix

$(OBJDIR)/test_aac_ps_render: tests/test_aac_ps_render.c include/sauti_aac_ps_render.h $(AAC_PS_MAP_SRC) $(AAC_PS_HYBRID_SRC) $(AAC_PS_MATRIX_SRC) $(AAC_PS_PHASE_SRC) $(AAC_PS_RENDER_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_ps_render.c $(AAC_PS_MAP_SRC) $(AAC_PS_HYBRID_SRC) $(AAC_PS_MATRIX_SRC) $(AAC_PS_PHASE_SRC) $(AAC_PS_RENDER_SRC) -lm

.PHONY: test-aac-ps-render
test-aac-ps-render: $(OBJDIR)/test_aac_ps_render
	./$(OBJDIR)/test_aac_ps_render

$(OBJDIR)/test_aac_sbr_grid: tests/test_aac_sbr_grid.c include/sauti_aac_sbr_grid.h $(AAC_SBR_GRID_SRC) $(AAC_HEADERS_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_grid.c $(AAC_SBR_GRID_SRC) $(AAC_HEADERS_ASM) -no-pie

.PHONY: test-aac-sbr-grid
test-aac-sbr-grid: $(OBJDIR)/test_aac_sbr_grid
	./$(OBJDIR)/test_aac_sbr_grid

$(OBJDIR)/test_aac_sbr_master: tests/test_aac_sbr_master.c include/sauti_aac_sbr_master.h $(AAC_SBR_MASTER_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_master.c $(AAC_SBR_MASTER_SRC)

.PHONY: test-aac-sbr-master
test-aac-sbr-master: $(OBJDIR)/test_aac_sbr_master
	./$(OBJDIR)/test_aac_sbr_master

$(OBJDIR)/test_aac_sbr_master_log: tests/test_aac_sbr_master_log.c include/sauti_aac_sbr_master.h $(AAC_SBR_MASTER_LOG_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_master_log.c $(AAC_SBR_MASTER_LOG_SRC) -lm

.PHONY: test-aac-sbr-master-log
test-aac-sbr-master-log: $(OBJDIR)/test_aac_sbr_master_log
	./$(OBJDIR)/test_aac_sbr_master_log

$(OBJDIR)/test_aac_sbr_limits: tests/test_aac_sbr_limits.c include/sauti_aac_sbr_limits.h $(AAC_SBR_LIMITS_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_limits.c $(AAC_SBR_LIMITS_SRC) -lm

.PHONY: test-aac-sbr-limits
test-aac-sbr-limits: $(OBJDIR)/test_aac_sbr_limits
	./$(OBJDIR)/test_aac_sbr_limits

$(OBJDIR)/test_aac_sbr_tables: tests/test_aac_sbr_tables.c include/sauti_aac_sbr_tables.h include/sauti_aac_sbr_master.h include/sauti_aac_sbr_limits.h $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_LIMITS_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_tables.c $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_LIMITS_SRC) -lm

.PHONY: test-aac-sbr-tables
test-aac-sbr-tables: $(OBJDIR)/test_aac_sbr_tables
	./$(OBJDIR)/test_aac_sbr_tables

$(OBJDIR)/test_aac_sbr_control: tests/test_aac_sbr_control.c include/sauti_aac_sbr_control.h include/sauti_aac_sbr_grid.h include/sauti_aac_sbr_tables.h include/sauti_aac_sbr_master.h $(AAC_SBR_CONTROL_SRC) $(AAC_SBR_GRID_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_HEADERS_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_control.c $(AAC_SBR_CONTROL_SRC) $(AAC_SBR_GRID_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_HEADERS_ASM) -lm -no-pie

.PHONY: test-aac-sbr-control
test-aac-sbr-control: $(OBJDIR)/test_aac_sbr_control
	./$(OBJDIR)/test_aac_sbr_control

$(OBJDIR)/test_aac_sbr_huffman: tests/test_aac_sbr_huffman.c include/sauti_aac_sbr_huffman.h $(AAC_SBR_HUFFMAN_SRC) $(AAC_HEADERS_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_huffman.c $(AAC_SBR_HUFFMAN_SRC) $(AAC_HEADERS_ASM) -no-pie

.PHONY: test-aac-sbr-huffman
test-aac-sbr-huffman: $(OBJDIR)/test_aac_sbr_huffman
	./$(OBJDIR)/test_aac_sbr_huffman

$(OBJDIR)/test_aac_sbr_quant: tests/test_aac_sbr_quant.c include/sauti_aac_sbr_quant.h include/sauti_aac_sbr_control.h include/sauti_aac_sbr_huffman.h include/sauti_aac_sbr_tables.h $(AAC_SBR_QUANT_SRC) $(AAC_SBR_HUFFMAN_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_HEADERS_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_quant.c $(AAC_SBR_QUANT_SRC) $(AAC_SBR_HUFFMAN_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_HEADERS_ASM) -lm -no-pie

.PHONY: test-aac-sbr-quant
test-aac-sbr-quant: $(OBJDIR)/test_aac_sbr_quant
	./$(OBJDIR)/test_aac_sbr_quant

$(OBJDIR)/test_aac_sbr_energy: tests/test_aac_sbr_energy.c include/sauti_aac_sbr_energy.h include/sauti_aac_sbr_quant.h $(AAC_SBR_ENERGY_SRC) $(AAC_SBR_QUANT_SRC) $(AAC_SBR_HUFFMAN_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_HEADERS_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_energy.c $(AAC_SBR_ENERGY_SRC) $(AAC_SBR_QUANT_SRC) $(AAC_SBR_HUFFMAN_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_HEADERS_ASM) -lm -no-pie

.PHONY: test-aac-sbr-energy
test-aac-sbr-energy: $(OBJDIR)/test_aac_sbr_energy
	./$(OBJDIR)/test_aac_sbr_energy

$(OBJDIR)/test_aac_sbr_coupled: tests/test_aac_sbr_coupled.c include/sauti_aac_sbr_energy.h include/sauti_aac_sbr_quant.h $(AAC_SBR_ENERGY_SRC) $(AAC_SBR_QUANT_SRC) $(AAC_SBR_HUFFMAN_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_HEADERS_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_coupled.c $(AAC_SBR_ENERGY_SRC) $(AAC_SBR_QUANT_SRC) $(AAC_SBR_HUFFMAN_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_HEADERS_ASM) -lm -no-pie

.PHONY: test-aac-sbr-coupled
test-aac-sbr-coupled: $(OBJDIR)/test_aac_sbr_coupled
	./$(OBJDIR)/test_aac_sbr_coupled

$(OBJDIR)/test_aac_sbr_patch: tests/test_aac_sbr_patch.c include/sauti_aac_sbr_patch.h include/sauti_aac_sbr_tables.h $(AAC_SBR_PATCH_SRC) $(AAC_SBR_LIMITS_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_patch.c $(AAC_SBR_PATCH_SRC) $(AAC_SBR_LIMITS_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) -lm

.PHONY: test-aac-sbr-patch
test-aac-sbr-patch: $(OBJDIR)/test_aac_sbr_patch
	./$(OBJDIR)/test_aac_sbr_patch

$(OBJDIR)/test_aac_sbr_hf: tests/test_aac_sbr_hf.c include/sauti_aac_sbr_hf.h include/sauti_aac_sbr_patch.h $(AAC_SBR_HF_SRC) $(AAC_SBR_PATCH_SRC) $(AAC_SBR_LIMITS_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_hf.c $(AAC_SBR_HF_SRC) $(AAC_SBR_PATCH_SRC) $(AAC_SBR_LIMITS_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) -lm

.PHONY: test-aac-sbr-hf
test-aac-sbr-hf: $(OBJDIR)/test_aac_sbr_hf
	./$(OBJDIR)/test_aac_sbr_hf

$(OBJDIR)/test_aac_sbr_hf_predict: tests/test_aac_sbr_hf_predict.c include/sauti_aac_sbr_hf_predict.h include/sauti_aac_sbr_hf.h $(AAC_SBR_HF_PREDICT_SRC) $(AAC_SBR_HF_SRC) $(AAC_SBR_PATCH_SRC) $(AAC_SBR_LIMITS_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_hf_predict.c $(AAC_SBR_HF_PREDICT_SRC) $(AAC_SBR_HF_SRC) $(AAC_SBR_PATCH_SRC) $(AAC_SBR_LIMITS_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) -lm

.PHONY: test-aac-sbr-hf-predict
test-aac-sbr-hf-predict: $(OBJDIR)/test_aac_sbr_hf_predict
	./$(OBJDIR)/test_aac_sbr_hf_predict

$(OBJDIR)/test_aac_sbr_lpc: tests/test_aac_sbr_lpc.c include/sauti_aac_sbr_lpc.h $(AAC_SBR_LPC_SRC) $(AAC_SBR_HF_PREDICT_SRC) $(AAC_SBR_HF_SRC) $(AAC_SBR_PATCH_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_FREQ_TABLES_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_lpc.c $(AAC_SBR_LPC_SRC) $(AAC_SBR_HF_PREDICT_SRC) $(AAC_SBR_HF_SRC) $(AAC_SBR_PATCH_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_FREQ_TABLES_SRC) -lm

.PHONY: test-aac-sbr-lpc
test-aac-sbr-lpc: $(OBJDIR)/test_aac_sbr_lpc
	./$(OBJDIR)/test_aac_sbr_lpc

$(OBJDIR)/test_aac_sbr_hf_adjust: tests/test_aac_sbr_hf_adjust.c include/sauti_aac_sbr_hf_adjust.h $(AAC_SBR_HF_ADJUST_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_SBR_LIMITS_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_hf_adjust.c $(AAC_SBR_HF_ADJUST_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_SBR_LIMITS_SRC) -lm

.PHONY: test-aac-sbr-hf-adjust
test-aac-sbr-hf-adjust: $(OBJDIR)/test_aac_sbr_hf_adjust
	./$(OBJDIR)/test_aac_sbr_hf_adjust

$(OBJDIR)/test_aac_sbr_hf_limiter: tests/test_aac_sbr_hf_limiter.c include/sauti_aac_sbr_hf_limiter.h $(AAC_SBR_HF_LIMITER_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_SBR_LIMITS_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_hf_limiter.c $(AAC_SBR_HF_LIMITER_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_SBR_LIMITS_SRC) -lm

.PHONY: test-aac-sbr-hf-limiter
test-aac-sbr-hf-limiter: $(OBJDIR)/test_aac_sbr_hf_limiter
	./$(OBJDIR)/test_aac_sbr_hf_limiter

$(OBJDIR)/test_aac_sbr_limiter_bands: tests/test_aac_sbr_limiter_bands.c include/sauti_aac_sbr_limiter_bands.h $(AAC_SBR_LIMITER_BANDS_SRC) $(AAC_SBR_HF_LIMITER_SRC) $(AAC_SBR_HF_SRC) $(AAC_SBR_PATCH_SRC) $(AAC_SBR_LIMITS_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_limiter_bands.c $(AAC_SBR_LIMITER_BANDS_SRC) $(AAC_SBR_HF_LIMITER_SRC) $(AAC_SBR_HF_SRC) $(AAC_SBR_PATCH_SRC) $(AAC_SBR_LIMITS_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) -lm

.PHONY: test-aac-sbr-limiter-bands
test-aac-sbr-limiter-bands: $(OBJDIR)/test_aac_sbr_limiter_bands
	./$(OBJDIR)/test_aac_sbr_limiter_bands

$(OBJDIR)/test_aac_sbr_hf_noise: tests/test_aac_sbr_hf_noise.c include/sauti_aac_sbr_hf_noise.h $(AAC_SBR_HF_NOISE_SRC) $(AAC_SBR_ENERGY_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_SBR_LIMITS_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_hf_noise.c $(AAC_SBR_HF_NOISE_SRC) $(AAC_SBR_ENERGY_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_SBR_LIMITS_SRC) -lm

.PHONY: test-aac-sbr-hf-noise
test-aac-sbr-hf-noise: $(OBJDIR)/test_aac_sbr_hf_noise
	./$(OBJDIR)/test_aac_sbr_hf_noise

$(OBJDIR)/test_aac_sbr_hf_sine: tests/test_aac_sbr_hf_sine.c include/sauti_aac_sbr_hf_sine.h $(AAC_SBR_HF_SINE_SRC) $(AAC_SBR_ENERGY_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_SBR_LIMITS_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_hf_sine.c $(AAC_SBR_HF_SINE_SRC) $(AAC_SBR_ENERGY_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_SBR_LIMITS_SRC) -lm

.PHONY: test-aac-sbr-hf-sine
test-aac-sbr-hf-sine: $(OBJDIR)/test_aac_sbr_hf_sine
	./$(OBJDIR)/test_aac_sbr_hf_sine

$(OBJDIR)/test_aac_sbr_hf_joint: tests/test_aac_sbr_hf_joint.c include/sauti_aac_sbr_hf_joint.h $(AAC_SBR_HF_JOINT_SRC) $(AAC_SBR_LIMITER_BANDS_SRC) $(AAC_SBR_HF_LIMITER_SRC) $(AAC_SBR_HF_NOISE_SRC) $(AAC_SBR_HF_SINE_SRC) $(AAC_SBR_HF_SRC) $(AAC_SBR_PATCH_SRC) $(AAC_SBR_LIMITS_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_hf_joint.c $(AAC_SBR_HF_JOINT_SRC) $(AAC_SBR_LIMITER_BANDS_SRC) $(AAC_SBR_HF_LIMITER_SRC) $(AAC_SBR_HF_NOISE_SRC) $(AAC_SBR_HF_SINE_SRC) $(AAC_SBR_HF_SRC) $(AAC_SBR_PATCH_SRC) $(AAC_SBR_LIMITS_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) -lm

.PHONY: test-aac-sbr-hf-joint
test-aac-sbr-hf-joint: $(OBJDIR)/test_aac_sbr_hf_joint
	./$(OBJDIR)/test_aac_sbr_hf_joint

$(OBJDIR)/test_aac_sbr_smooth: tests/test_aac_sbr_smooth.c include/sauti_aac_sbr_smooth.h $(AAC_SBR_SMOOTH_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_FREQ_TABLES_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_smooth.c $(AAC_SBR_SMOOTH_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_FREQ_TABLES_SRC) -lm

.PHONY: test-aac-sbr-smooth
test-aac-sbr-smooth: $(OBJDIR)/test_aac_sbr_smooth
	./$(OBJDIR)/test_aac_sbr_smooth

$(OBJDIR)/test_aac_sbr_hf_joint_smooth: tests/test_aac_sbr_hf_joint_smooth.c include/sauti_aac_sbr_hf_joint_smooth.h $(AAC_SBR_HF_JOINT_SMOOTH_SRC) $(AAC_SBR_HF_JOINT_SRC) $(AAC_SBR_SMOOTH_SRC) $(AAC_SBR_LIMITER_BANDS_SRC) $(AAC_SBR_HF_LIMITER_SRC) $(AAC_SBR_HF_NOISE_SRC) $(AAC_SBR_HF_SINE_SRC) $(AAC_SBR_HF_SRC) $(AAC_SBR_PATCH_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_FREQ_TABLES_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_hf_joint_smooth.c $(AAC_SBR_HF_JOINT_SMOOTH_SRC) $(AAC_SBR_HF_JOINT_SRC) $(AAC_SBR_SMOOTH_SRC) $(AAC_SBR_LIMITER_BANDS_SRC) $(AAC_SBR_HF_LIMITER_SRC) $(AAC_SBR_HF_NOISE_SRC) $(AAC_SBR_HF_SINE_SRC) $(AAC_SBR_HF_SRC) $(AAC_SBR_PATCH_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_FREQ_TABLES_SRC) -lm

.PHONY: test-aac-sbr-hf-joint-smooth
test-aac-sbr-hf-joint-smooth: $(OBJDIR)/test_aac_sbr_hf_joint_smooth
	./$(OBJDIR)/test_aac_sbr_hf_joint_smooth

$(OBJDIR)/test_aac_sbr_noise_rom: tests/test_aac_sbr_noise_rom.c include/sauti_aac_sbr_noise_rom.h $(AAC_SBR_NOISE_ROM_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_FREQ_TABLES_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_noise_rom.c $(AAC_SBR_NOISE_ROM_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_FREQ_TABLES_SRC) -lm

.PHONY: test-aac-sbr-noise-rom
test-aac-sbr-noise-rom: $(OBJDIR)/test_aac_sbr_noise_rom
	./$(OBJDIR)/test_aac_sbr_noise_rom

$(OBJDIR)/test_aac_sbr_hf_joint_rom: tests/test_aac_sbr_hf_joint_rom.c include/sauti_aac_sbr_hf_joint_rom.h $(AAC_SBR_HF_JOINT_ROM_SRC) $(AAC_SBR_NOISE_ROM_SRC) $(AAC_SBR_SMOOTH_SRC) $(AAC_SBR_LIMITER_BANDS_SRC) $(AAC_SBR_HF_LIMITER_SRC) $(AAC_SBR_HF_SINE_SRC) $(AAC_SBR_HF_SRC) $(AAC_SBR_PATCH_SRC) $(AAC_SBR_LIMITS_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_SBR_HF_JOINT_SMOOTH_SRC) $(AAC_SBR_HF_NOISE_SRC) $(AAC_SBR_HF_JOINT_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_hf_joint_rom.c $(AAC_SBR_HF_JOINT_ROM_SRC) $(AAC_SBR_NOISE_ROM_SRC) $(AAC_SBR_SMOOTH_SRC) $(AAC_SBR_LIMITER_BANDS_SRC) $(AAC_SBR_HF_LIMITER_SRC) $(AAC_SBR_HF_SINE_SRC) $(AAC_SBR_HF_SRC) $(AAC_SBR_PATCH_SRC) $(AAC_SBR_LIMITS_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_SBR_HF_JOINT_SMOOTH_SRC) $(AAC_SBR_HF_NOISE_SRC) $(AAC_SBR_HF_JOINT_SRC) -lm

.PHONY: test-aac-sbr-hf-joint-rom
test-aac-sbr-hf-joint-rom: $(OBJDIR)/test_aac_sbr_hf_joint_rom
	./$(OBJDIR)/test_aac_sbr_hf_joint_rom

$(OBJDIR)/test_aac_sbr_qmf_synth_window: tests/test_aac_sbr_qmf_synth_window.c include/sauti_aac_sbr_qmf_synth_window.h $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_qmf_synth_window.c $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) -lm

.PHONY: test-aac-sbr-qmf-synth-window
test-aac-sbr-qmf-synth-window: $(OBJDIR)/test_aac_sbr_qmf_synth_window
	./$(OBJDIR)/test_aac_sbr_qmf_synth_window

$(OBJDIR)/test_aac_sbr_qmf_synth_mod: tests/test_aac_sbr_qmf_synth_mod.c include/sauti_aac_sbr_qmf_synth_mod.h $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_qmf_synth_mod.c $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) -lm

.PHONY: test-aac-sbr-qmf-synth-mod
test-aac-sbr-qmf-synth-mod: $(OBJDIR)/test_aac_sbr_qmf_synth_mod
	./$(OBJDIR)/test_aac_sbr_qmf_synth_mod

ifeq ($(PORTABLE),1)
.PHONY: test-aac-qmf-simd
test-aac-qmf-simd:
	@echo "SKIP test-aac-qmf-simd: PORTABLE=1 ma tylko odpowiedniki w C (brak jader SIMD do porownania)"
else
# Native SSE2/AVX2 differential tests; automatically SKIP on CPUs without AVX2.
$(OBJDIR)/test_aac_qmf_simd: tests/test_aac_qmf_simd.c include/sauti_aac_sbr_qmf_synth_mod.h $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_qmf_simd.c $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) -lm

.PHONY: test-aac-qmf-simd
test-aac-qmf-simd: $(OBJDIR)/test_aac_qmf_simd
	./$(OBJDIR)/test_aac_qmf_simd
endif

# Pure CPU policy and live CPUID dispatch verification, including forced modes.
$(OBJDIR)/test_aac_qmf_dispatch: tests/test_aac_qmf_dispatch.c include/sauti_aac_qmf_dispatch.h | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_qmf_dispatch.c

$(OBJDIR)/test_aac_qmf_dispatch_sse2: tests/test_aac_qmf_dispatch.c include/sauti_aac_qmf_dispatch.h | $(OBJDIR)
	$(CC) $(CFLAGS) -DSAUTI_QMF_FORCE_SSE2 -Iinclude -o $@ tests/test_aac_qmf_dispatch.c

$(OBJDIR)/test_aac_qmf_dispatch_avx2: tests/test_aac_qmf_dispatch.c include/sauti_aac_qmf_dispatch.h | $(OBJDIR)
	$(CC) $(CFLAGS) -DSAUTI_QMF_FORCE_AVX2 -Iinclude -o $@ tests/test_aac_qmf_dispatch.c

$(OBJDIR)/test_aac_qmf_dispatch_c11: tests/test_aac_qmf_dispatch.c include/sauti_aac_qmf_dispatch.h | $(OBJDIR)
	$(CC) $(CFLAGS) -DSAUTI_QMF_DISABLE_ASM -Iinclude -o $@ tests/test_aac_qmf_dispatch.c

.PHONY: test-aac-qmf-dispatch
test-aac-qmf-dispatch: $(OBJDIR)/test_aac_qmf_dispatch $(OBJDIR)/test_aac_qmf_dispatch_sse2 $(OBJDIR)/test_aac_qmf_dispatch_avx2 $(OBJDIR)/test_aac_qmf_dispatch_c11
	./$(OBJDIR)/test_aac_qmf_dispatch
	./$(OBJDIR)/test_aac_qmf_dispatch_sse2
	./$(OBJDIR)/test_aac_qmf_dispatch_avx2
	./$(OBJDIR)/test_aac_qmf_dispatch_c11

$(OBJDIR)/sauti_qmf_dispatch_probe: tools/sauti_qmf_dispatch_probe.c include/sauti_aac_qmf_dispatch.h | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tools/sauti_qmf_dispatch_probe.c

.PHONY: probe-aac-qmf
probe-aac-qmf: $(OBJDIR)/sauti_qmf_dispatch_probe
	./$(OBJDIR)/sauti_qmf_dispatch_probe

# Three independently compiled public-API pathways: scalar C11, forced
# System V SSE2 and automatic AVX2/SSE2. The C11 test deliberately does
# not link any QMF NASM objects, exercising the Windows-safe fallback.
AAC_QMF_C11_SRC := src/decoders/modern/aac/sauti_aac_sbr_qmf_synth_mod.c src/decoders/modern/aac/sauti_aac_sbr_qmf_synth_window.c

$(OBJDIR)/test_aac_qmf_c11: tests/test_aac_sbr_qmf_synth_mod.c $(AAC_QMF_C11_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -DSAUTI_QMF_DISABLE_ASM -Iinclude -o $@ tests/test_aac_sbr_qmf_synth_mod.c $(AAC_QMF_C11_SRC) -lm

$(OBJDIR)/test_aac_qmf_sse2: tests/test_aac_sbr_qmf_synth_mod.c $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -DSAUTI_QMF_FORCE_SSE2 -Iinclude -o $@ tests/test_aac_sbr_qmf_synth_mod.c $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) -lm

.PHONY: test-aac-qmf-variants
test-aac-qmf-variants: $(OBJDIR)/test_aac_qmf_c11 $(OBJDIR)/test_aac_qmf_sse2
	./$(OBJDIR)/test_aac_qmf_c11
	./$(OBJDIR)/test_aac_qmf_sse2

# Opt-in local QMF kernel benchmark; does not gate correctness tests.
$(OBJDIR)/sauti_qmf_simd_bench: tools/sauti_qmf_simd_bench.c $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tools/sauti_qmf_simd_bench.c $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) -lm

.PHONY: bench-aac-qmf
bench-aac-qmf: $(OBJDIR)/sauti_qmf_simd_bench
	./$(OBJDIR)/sauti_qmf_simd_bench

# Compare 40-slot public-API QMF synthesis with native runtime dispatch
# against a separately compiled SSE2-only wrapper. No decoder-wide claim.
AAC_QMF_FRAME_BENCH_SRC := tools/sauti_qmf_frame_bench.c
$(OBJDIR)/sauti_qmf_frame_bench_auto: $(AAC_QMF_FRAME_BENCH_SRC) $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ $(AAC_QMF_FRAME_BENCH_SRC) $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) -lm

$(OBJDIR)/sauti_qmf_frame_bench_sse2: $(AAC_QMF_FRAME_BENCH_SRC) $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -DSAUTI_QMF_FORCE_SSE2 -Iinclude -o $@ $(AAC_QMF_FRAME_BENCH_SRC) $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) -lm

.PHONY: bench-aac-qmf-frame
bench-aac-qmf-frame: $(OBJDIR)/sauti_qmf_frame_bench_auto $(OBJDIR)/sauti_qmf_frame_bench_sse2
	./$(OBJDIR)/sauti_qmf_frame_bench_auto
	./$(OBJDIR)/sauti_qmf_frame_bench_sse2

# End-to-end QMF synthesis output comparison, not merely kernel math.
.PHONY: test-aac-qmf-frame-parity
test-aac-qmf-frame-parity: $(OBJDIR)/sauti_qmf_frame_bench_auto $(OBJDIR)/sauti_qmf_frame_bench_sse2
	./$(OBJDIR)/sauti_qmf_frame_bench_auto --pcm-out $(OBJDIR)/sauti_qmf_auto.f32le
	./$(OBJDIR)/sauti_qmf_frame_bench_sse2 --pcm-out $(OBJDIR)/sauti_qmf_sse2.f32le
	python3 tools/sauti_qmf_frame_compare.py $(OBJDIR)/sauti_qmf_auto.f32le $(OBJDIR)/sauti_qmf_sse2.f32le

# Continuity across Linux P/E migrations. Both binaries emit 16 consecutive
# 40-slot frames; on homogeneous CI machines they still exercise state and
# compare complete streams but print a clear SKIP for P/E migration itself.
$(OBJDIR)/sauti_qmf_migration_auto: tools/sauti_qmf_migration_test.c include/sauti_aac_qmf_dispatch.h $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tools/sauti_qmf_migration_test.c $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) -lm

$(OBJDIR)/sauti_qmf_migration_sse2: tools/sauti_qmf_migration_test.c include/sauti_aac_qmf_dispatch.h $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -DSAUTI_QMF_FORCE_SSE2 -Iinclude -o $@ tools/sauti_qmf_migration_test.c $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) -lm

.PHONY: test-aac-qmf-migration
test-aac-qmf-migration: $(OBJDIR)/sauti_qmf_migration_auto $(OBJDIR)/sauti_qmf_migration_sse2
	./$(OBJDIR)/sauti_qmf_migration_auto $(OBJDIR)/sauti_qmf_migrate_auto.f32le
	./$(OBJDIR)/sauti_qmf_migration_sse2 $(OBJDIR)/sauti_qmf_migrate_sse2.f32le
	python3 tools/sauti_qmf_frame_compare.py $(OBJDIR)/sauti_qmf_migrate_auto.f32le $(OBJDIR)/sauti_qmf_migrate_sse2.f32le

$(OBJDIR)/test_aac_sbr_qmf_analysis: tests/test_aac_sbr_qmf_analysis.c include/sauti_aac_sbr_qmf_analysis.h $(AAC_SBR_QMF_ANALYSIS_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_qmf_analysis.c $(AAC_SBR_QMF_ANALYSIS_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) -lm

.PHONY: test-aac-sbr-qmf-analysis
test-aac-sbr-qmf-analysis: $(OBJDIR)/test_aac_sbr_qmf_analysis
	./$(OBJDIR)/test_aac_sbr_qmf_analysis

AAC_SBR_QMF_FRAME_DEPS := $(AAC_SBR_QMF_FRAME_SRC) $(AAC_SBR_QMF_ANALYSIS_SRC) $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) $(AAC_SBR_HF_JOINT_ROM_SRC) $(AAC_SBR_NOISE_ROM_SRC) $(AAC_SBR_SMOOTH_SRC) $(AAC_SBR_LIMITER_BANDS_SRC) $(AAC_SBR_HF_LIMITER_SRC) $(AAC_SBR_HF_SINE_SRC) $(AAC_SBR_HF_PREDICT_SRC) $(AAC_SBR_HF_SRC) $(AAC_SBR_PATCH_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_SBR_LIMITS_SRC)

$(OBJDIR)/test_aac_sbr_qmf_frame: tests/test_aac_sbr_qmf_frame.c include/sauti_aac_sbr_qmf_frame.h $(AAC_SBR_QMF_FRAME_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_qmf_frame.c $(AAC_SBR_QMF_FRAME_DEPS) -lm

.PHONY: test-aac-sbr-qmf-frame
test-aac-sbr-qmf-frame: $(OBJDIR)/test_aac_sbr_qmf_frame
	./$(OBJDIR)/test_aac_sbr_qmf_frame

$(OBJDIR)/test_aac_sbr_qmf_export: tests/test_aac_sbr_qmf_export.c include/sauti_aac_sbr_qmf_frame.h $(AAC_SBR_QMF_FRAME_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_qmf_export.c $(AAC_SBR_QMF_FRAME_DEPS) -lm

.PHONY: test-aac-sbr-qmf-hf-export
test-aac-sbr-qmf-hf-export: $(OBJDIR)/test_aac_sbr_qmf_export
	./$(OBJDIR)/test_aac_sbr_qmf_export

AAC_SBR_PAYLOAD_DEPS := $(AAC_SBR_PAYLOAD_SRC) $(AAC_SBR_CRC_SRC) $(AAC_SYNTAX_SRC) $(AAC_SBR_GRID_SRC) $(AAC_SBR_CONTROL_SRC) $(AAC_SBR_QUANT_SRC) $(AAC_SBR_HUFFMAN_SRC) $(AAC_SBR_ENERGY_SRC) $(AAC_SBR_LIMITS_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_SBR_PATCH_SRC) $(AAC_HEADERS_ASM)

$(OBJDIR)/test_aac_sbr_payload: tests/test_aac_sbr_payload.c include/sauti_aac_sbr_payload.h $(AAC_SBR_PAYLOAD_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_payload.c $(AAC_SBR_PAYLOAD_DEPS) -lm -no-pie

.PHONY: test-aac-sbr-payload
test-aac-sbr-payload: $(OBJDIR)/test_aac_sbr_payload
	./$(OBJDIR)/test_aac_sbr_payload

AAC_SBR_ADTS_MONO_DEPS := $(AAC_HE_ADTS_MONO_SRC) $(AAC_SBR_PAYLOAD_SRC) $(AAC_SBR_CRC_SRC) $(AAC_SBR_GRID_SRC) $(AAC_SBR_CONTROL_SRC) $(AAC_SBR_QUANT_SRC) $(AAC_SBR_HUFFMAN_SRC) $(AAC_SBR_ENERGY_SRC) $(AAC_SBR_LIMITS_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_SBR_PATCH_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM)

$(OBJDIR)/test_aac_sbr_adts_mono: tests/test_aac_sbr_adts_mono.c include/sauti_aac_he_adts_mono.h $(AAC_SBR_ADTS_MONO_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_adts_mono.c $(AAC_SBR_ADTS_MONO_DEPS) -lm -no-pie

.PHONY: test-aac-sbr-adts-mono
test-aac-sbr-adts-mono: $(OBJDIR)/test_aac_sbr_adts_mono
	./$(OBJDIR)/test_aac_sbr_adts_mono

AAC_SBR_TEMPORAL_QMF_SRC := src/decoders/modern/aac/sauti_aac_sbr_temporal_qmf.c
AAC_SBR_TEMPORAL_QMF_DEPS := $(sort $(AAC_SBR_PAYLOAD_DEPS) $(AAC_SBR_QMF_ANALYSIS_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) $(AAC_SBR_HF_SRC) $(AAC_SBR_TEMPORAL_QMF_SRC))

$(OBJDIR)/test_aac_sbr_temporal_qmf: tests/test_aac_sbr_temporal_qmf.c include/sauti_aac_sbr_temporal_qmf.h $(AAC_SBR_TEMPORAL_QMF_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_temporal_qmf.c $(AAC_SBR_TEMPORAL_QMF_DEPS) -lm -no-pie

.PHONY: test-aac-sbr-temporal-qmf
test-aac-sbr-temporal-qmf: $(OBJDIR)/test_aac_sbr_temporal_qmf
	./$(OBJDIR)/test_aac_sbr_temporal_qmf

AAC_SBR_HF_PATCH_ROUTE_AUDIT_SRC := src/decoders/modern/aac/sauti_aac_sbr_hf_patch_route_audit.c
AAC_SBR_HF_PATCH_ROUTE_AUDIT_DEPS := $(sort $(AAC_SBR_TEMPORAL_QMF_DEPS) $(AAC_SBR_HF_PATCH_ROUTE_AUDIT_SRC))

$(OBJDIR)/test_aac_sbr_hf_patch_route_audit: tests/test_aac_sbr_hf_patch_route_audit.c include/sauti_aac_sbr_hf_patch_route_audit.h $(AAC_SBR_HF_PATCH_ROUTE_AUDIT_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_hf_patch_route_audit.c $(AAC_SBR_HF_PATCH_ROUTE_AUDIT_DEPS) -lm -no-pie

.PHONY: test-aac-sbr-hf-patch-route-audit
test-aac-sbr-hf-patch-route-audit: $(OBJDIR)/test_aac_sbr_hf_patch_route_audit
	./$(OBJDIR)/test_aac_sbr_hf_patch_route_audit

AAC_SBR_HF_BAND_AUDIT_SRC := src/decoders/modern/aac/sauti_aac_sbr_hf_band_audit.c

$(OBJDIR)/test_aac_sbr_hf_band_audit: tests/test_aac_sbr_hf_band_audit.c include/sauti_aac_sbr_hf_band_audit.h $(AAC_SBR_HF_BAND_AUDIT_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_hf_band_audit.c $(AAC_SBR_HF_BAND_AUDIT_SRC) -lm -no-pie

.PHONY: test-aac-sbr-hf-band-audit
test-aac-sbr-hf-band-audit: $(OBJDIR)/test_aac_sbr_hf_band_audit
	./$(OBJDIR)/test_aac_sbr_hf_band_audit

AAC_SBR_TEMPORAL_SOURCE6_SRC := src/decoders/modern/aac/sauti_aac_sbr_temporal_source6.c
AAC_SBR_TEMPORAL_SOURCE6_DEPS := $(sort $(AAC_SBR_TEMPORAL_QMF_DEPS) $(AAC_SBR_TEMPORAL_SOURCE6_SRC))

$(OBJDIR)/test_aac_sbr_temporal_source6: tests/test_aac_sbr_temporal_source6.c include/sauti_aac_sbr_temporal_source6.h $(AAC_SBR_TEMPORAL_SOURCE6_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_temporal_source6.c $(AAC_SBR_TEMPORAL_SOURCE6_DEPS) -lm -no-pie

.PHONY: test-aac-sbr-temporal-source6
test-aac-sbr-temporal-source6: $(OBJDIR)/test_aac_sbr_temporal_source6
	./$(OBJDIR)/test_aac_sbr_temporal_source6

AAC_SBR_TEMPORAL_POWER_SRC := src/decoders/modern/aac/sauti_aac_sbr_temporal_power.c
AAC_SBR_TEMPORAL_POWER_DEPS := $(sort $(AAC_SBR_TEMPORAL_QMF_DEPS) $(AAC_SBR_TEMPORAL_POWER_SRC))

$(OBJDIR)/test_aac_sbr_temporal_power: tests/test_aac_sbr_temporal_power.c include/sauti_aac_sbr_temporal_power.h $(AAC_SBR_TEMPORAL_POWER_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_temporal_power.c $(AAC_SBR_TEMPORAL_POWER_DEPS) -lm -no-pie

.PHONY: test-aac-sbr-temporal-power
test-aac-sbr-temporal-power: $(OBJDIR)/test_aac_sbr_temporal_power
	./$(OBJDIR)/test_aac_sbr_temporal_power

AAC_SBR_TEMPORAL_GAIN_SRC := src/decoders/modern/aac/sauti_aac_sbr_temporal_gain.c
AAC_SBR_TEMPORAL_GAIN_DEPS := $(sort $(AAC_SBR_TEMPORAL_POWER_DEPS) $(AAC_SBR_LIMITER_BANDS_SRC) $(AAC_SBR_HF_LIMITER_SRC) $(AAC_SBR_TEMPORAL_GAIN_SRC))

$(OBJDIR)/test_aac_sbr_temporal_gain: tests/test_aac_sbr_temporal_gain.c include/sauti_aac_sbr_temporal_gain.h $(AAC_SBR_TEMPORAL_GAIN_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_temporal_gain.c $(AAC_SBR_TEMPORAL_GAIN_DEPS) -lm -no-pie

.PHONY: test-aac-sbr-temporal-gain
test-aac-sbr-temporal-gain: $(OBJDIR)/test_aac_sbr_temporal_gain
	./$(OBJDIR)/test_aac_sbr_temporal_gain

AAC_SBR_TEMPORAL_APPLY_SRC := src/decoders/modern/aac/sauti_aac_sbr_temporal_apply.c
AAC_SBR_TEMPORAL_APPLY_DEPS := $(sort $(AAC_SBR_TEMPORAL_GAIN_DEPS) $(AAC_SBR_TEMPORAL_APPLY_SRC))

$(OBJDIR)/test_aac_sbr_temporal_apply: tests/test_aac_sbr_temporal_apply.c include/sauti_aac_sbr_temporal_apply.h $(AAC_SBR_TEMPORAL_APPLY_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_temporal_apply.c $(AAC_SBR_TEMPORAL_APPLY_DEPS) -lm -no-pie

.PHONY: test-aac-sbr-temporal-apply
test-aac-sbr-temporal-apply: $(OBJDIR)/test_aac_sbr_temporal_apply
	./$(OBJDIR)/test_aac_sbr_temporal_apply

AAC_SBR_TEMPORAL_NOISE_SRC := src/decoders/modern/aac/sauti_aac_sbr_temporal_noise.c
AAC_SBR_TEMPORAL_NOISE_DEPS := $(sort $(AAC_SBR_TEMPORAL_APPLY_DEPS) $(AAC_SBR_NOISE_ROM_SRC) $(AAC_SBR_TEMPORAL_NOISE_SRC))

$(OBJDIR)/test_aac_sbr_temporal_noise: tests/test_aac_sbr_temporal_noise.c include/sauti_aac_sbr_temporal_noise.h $(AAC_SBR_TEMPORAL_NOISE_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_temporal_noise.c $(AAC_SBR_TEMPORAL_NOISE_DEPS) -lm -no-pie

.PHONY: test-aac-sbr-temporal-noise
test-aac-sbr-temporal-noise: $(OBJDIR)/test_aac_sbr_temporal_noise
	./$(OBJDIR)/test_aac_sbr_temporal_noise

AAC_SBR_TEMPORAL_MIX_SRC := src/decoders/modern/aac/sauti_aac_sbr_temporal_mix.c
AAC_SBR_TEMPORAL_MIX_DEPS := $(sort $(AAC_SBR_TEMPORAL_NOISE_DEPS) $(AAC_SBR_TEMPORAL_MIX_SRC))

$(OBJDIR)/test_aac_sbr_temporal_mix: tests/test_aac_sbr_temporal_mix.c include/sauti_aac_sbr_temporal_mix.h $(AAC_SBR_TEMPORAL_MIX_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_temporal_mix.c $(AAC_SBR_TEMPORAL_MIX_DEPS) -lm -no-pie

.PHONY: test-aac-sbr-temporal-mix
test-aac-sbr-temporal-mix: $(OBJDIR)/test_aac_sbr_temporal_mix
	./$(OBJDIR)/test_aac_sbr_temporal_mix

AAC_SBR_TEMPORAL_SMOOTH_SRC := src/decoders/modern/aac/sauti_aac_sbr_temporal_smooth.c
AAC_SBR_TEMPORAL_SMOOTH_DEPS := $(sort $(AAC_SBR_TEMPORAL_NOISE_DEPS) $(AAC_SBR_TEMPORAL_SMOOTH_SRC))

$(OBJDIR)/test_aac_sbr_temporal_smooth: tests/test_aac_sbr_temporal_smooth.c include/sauti_aac_sbr_temporal_smooth.h $(AAC_SBR_TEMPORAL_SMOOTH_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_temporal_smooth.c $(AAC_SBR_TEMPORAL_SMOOTH_DEPS) -lm -no-pie

.PHONY: test-aac-sbr-temporal-smooth
test-aac-sbr-temporal-smooth: $(OBJDIR)/test_aac_sbr_temporal_smooth
	./$(OBJDIR)/test_aac_sbr_temporal_smooth

AAC_SBR_TEMPORAL_PCM_PREVIEW_SRC := src/decoders/modern/aac/sauti_aac_sbr_temporal_pcm_preview.c
AAC_SBR_TEMPORAL_PCM_PREVIEW_DEPS := $(sort $(AAC_SBR_TEMPORAL_MIX_DEPS) $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) $(AAC_SBR_TEMPORAL_PCM_PREVIEW_SRC))

$(OBJDIR)/test_aac_sbr_temporal_pcm_preview: tests/test_aac_sbr_temporal_pcm_preview.c include/sauti_aac_sbr_temporal_pcm_preview.h $(AAC_SBR_TEMPORAL_PCM_PREVIEW_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_temporal_pcm_preview.c $(AAC_SBR_TEMPORAL_PCM_PREVIEW_DEPS) -lm -no-pie

.PHONY: test-aac-sbr-temporal-pcm-preview
test-aac-sbr-temporal-pcm-preview: $(OBJDIR)/test_aac_sbr_temporal_pcm_preview
	./$(OBJDIR)/test_aac_sbr_temporal_pcm_preview

AAC_SBR_GAIN_PARTITION_SRC := src/decoders/modern/aac/sauti_aac_sbr_gain_partition.c

AAC_SBR_GAIN_LIMIT_PARTITION_SRC := src/decoders/modern/aac/sauti_aac_sbr_gain_limit_partition.c

.PHONY: test-aac-sbr-fixed-lag-audit
test-aac-sbr-fixed-lag-audit:
	python3 tools/sauti_he_aac_fixed_lag_audit.py --self-test

.PHONY: test-aac-sbr-hf-shift-audit
test-aac-sbr-hf-shift-audit:
	python3 tools/sauti_he_aac_hf_shift_audit.py --self-test

.PHONY: test-aac-sbr-hf-shift-validation
test-aac-sbr-hf-shift-validation:
	python3 tools/sauti_he_aac_hf_shift_validation.py --self-test

AAC_SBR_HF_PHASE_PROBE_SRC := src/decoders/modern/aac/sauti_aac_sbr_hf_phase_probe.c

$(OBJDIR)/test_aac_sbr_hf_phase_probe: tests/test_aac_sbr_hf_phase_probe.c include/sauti_aac_sbr_hf_phase_probe.h $(AAC_SBR_HF_PHASE_PROBE_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_hf_phase_probe.c $(AAC_SBR_HF_PHASE_PROBE_SRC) -lm -no-pie

.PHONY: test-aac-sbr-qmf-index-audit
test-aac-sbr-qmf-index-audit:
	python3 tools/sauti_he_aac_qmf_index_audit.py --self-test
	python3 tools/sauti_he_aac_qmf_index_audit.py --frame 1

.PHONY: test-aac-sbr-hf-phase-probe
test-aac-sbr-hf-phase-probe: $(OBJDIR)/test_aac_sbr_hf_phase_probe
	./$(OBJDIR)/test_aac_sbr_hf_phase_probe

AAC_SBR_TEMPORAL_GAIN_BRIDGE_SRC := src/decoders/modern/aac/sauti_aac_sbr_temporal_gain_bridge.c

$(OBJDIR)/test_aac_sbr_temporal_gain_bridge: tests/test_aac_sbr_temporal_gain_bridge.c include/sauti_aac_sbr_temporal_gain_bridge.h $(AAC_SBR_TEMPORAL_GAIN_BRIDGE_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_temporal_gain_bridge.c $(AAC_SBR_TEMPORAL_GAIN_BRIDGE_SRC) -lm -no-pie

.PHONY: test-aac-sbr-temporal-gain-bridge
test-aac-sbr-temporal-gain-bridge: $(OBJDIR)/test_aac_sbr_temporal_gain_bridge
	./$(OBJDIR)/test_aac_sbr_temporal_gain_bridge

AAC_SBR_TEMPORAL_REFERENCE_GAIN_SRC := src/decoders/modern/aac/sauti_aac_sbr_temporal_reference_gain.c
AAC_SBR_TEMPORAL_REFERENCE_GAIN_DEPS := $(sort $(AAC_SBR_TEMPORAL_GAIN_DEPS) $(AAC_SBR_GAIN_PARTITION_SRC) $(AAC_SBR_GAIN_LIMIT_PARTITION_SRC) $(AAC_SBR_TEMPORAL_REFERENCE_GAIN_SRC))

$(OBJDIR)/test_aac_sbr_temporal_reference_gain: tests/test_aac_sbr_temporal_reference_gain.c include/sauti_aac_sbr_temporal_reference_gain.h $(AAC_SBR_TEMPORAL_REFERENCE_GAIN_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_temporal_reference_gain.c $(AAC_SBR_TEMPORAL_REFERENCE_GAIN_DEPS) -lm -no-pie

.PHONY: test-aac-sbr-temporal-reference-gain
test-aac-sbr-temporal-reference-gain: $(OBJDIR)/test_aac_sbr_temporal_reference_gain
	./$(OBJDIR)/test_aac_sbr_temporal_reference_gain

$(OBJDIR)/test_aac_sbr_gain_limit_partition: tests/test_aac_sbr_gain_limit_partition.c include/sauti_aac_sbr_gain_limit_partition.h $(AAC_SBR_GAIN_LIMIT_PARTITION_SRC) $(AAC_SBR_GAIN_PARTITION_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_gain_limit_partition.c $(AAC_SBR_GAIN_LIMIT_PARTITION_SRC) $(AAC_SBR_GAIN_PARTITION_SRC) -lm -no-pie

.PHONY: test-aac-sbr-gain-limit-partition
test-aac-sbr-gain-limit-partition: $(OBJDIR)/test_aac_sbr_gain_limit_partition
	./$(OBJDIR)/test_aac_sbr_gain_limit_partition

$(OBJDIR)/test_aac_sbr_gain_partition: tests/test_aac_sbr_gain_partition.c include/sauti_aac_sbr_gain_partition.h $(AAC_SBR_GAIN_PARTITION_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_gain_partition.c $(AAC_SBR_GAIN_PARTITION_SRC) -lm -no-pie

.PHONY: test-aac-sbr-gain-partition
test-aac-sbr-gain-partition: $(OBJDIR)/test_aac_sbr_gain_partition
	./$(OBJDIR)/test_aac_sbr_gain_partition

AAC_SBR_TEMPORAL_HF_UNITS_SRC := src/decoders/modern/aac/sauti_aac_sbr_temporal_hf_units.c
$(OBJDIR)/test_aac_sbr_temporal_hf_units: tests/test_aac_sbr_temporal_hf_units.c include/sauti_aac_sbr_temporal_hf_units.h $(AAC_SBR_TEMPORAL_HF_UNITS_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_temporal_hf_units.c $(AAC_SBR_TEMPORAL_HF_UNITS_SRC) -lm -no-pie

.PHONY: test-aac-sbr-temporal-hf-units
test-aac-sbr-temporal-hf-units: $(OBJDIR)/test_aac_sbr_temporal_hf_units
	./$(OBJDIR)/test_aac_sbr_temporal_hf_units

AAC_SBR_TEMPORAL_ENERGY_UNITS_SRC := src/decoders/modern/aac/sauti_aac_sbr_temporal_energy_units.c
$(OBJDIR)/test_aac_sbr_temporal_energy_units: tests/test_aac_sbr_temporal_energy_units.c include/sauti_aac_sbr_temporal_energy_units.h $(AAC_SBR_TEMPORAL_ENERGY_UNITS_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_temporal_energy_units.c $(AAC_SBR_TEMPORAL_ENERGY_UNITS_SRC) -lm -no-pie

.PHONY: test-aac-sbr-temporal-energy-units
test-aac-sbr-temporal-energy-units: $(OBJDIR)/test_aac_sbr_temporal_energy_units
	./$(OBJDIR)/test_aac_sbr_temporal_energy_units

AAC_SBR_TEMPORAL_HARMONIC_PREVIEW_SRC := src/decoders/modern/aac/sauti_aac_sbr_temporal_harmonic_preview.c
AAC_SBR_TEMPORAL_HARMONIC_PREVIEW_DEPS := $(sort $(AAC_SBR_TEMPORAL_MIX_DEPS) $(AAC_SBR_TEMPORAL_HARMONIC_PREVIEW_SRC))

$(OBJDIR)/test_aac_sbr_temporal_harmonic_preview: tests/test_aac_sbr_temporal_harmonic_preview.c include/sauti_aac_sbr_temporal_harmonic_preview.h $(AAC_SBR_TEMPORAL_HARMONIC_PREVIEW_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_temporal_harmonic_preview.c $(AAC_SBR_TEMPORAL_HARMONIC_PREVIEW_DEPS) -lm -no-pie

$(OBJDIR)/test_aac_sbr_harmonic_phase2_ab: tests/test_aac_sbr_harmonic_phase2_ab.c include/sauti_aac_sbr_temporal_harmonic_preview.h $(AAC_SBR_TEMPORAL_HARMONIC_PREVIEW_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_harmonic_phase2_ab.c $(AAC_SBR_TEMPORAL_HARMONIC_PREVIEW_DEPS) -lm -no-pie

.PHONY: test-aac-sbr-harmonic-phase2-ab
test-aac-sbr-harmonic-phase2-ab: $(OBJDIR)/test_aac_sbr_harmonic_phase2_ab
	./$(OBJDIR)/test_aac_sbr_harmonic_phase2_ab

.PHONY: test-aac-sbr-temporal-harmonic-preview
test-aac-sbr-temporal-harmonic-preview: $(OBJDIR)/test_aac_sbr_temporal_harmonic_preview
	./$(OBJDIR)/test_aac_sbr_temporal_harmonic_preview

AAC_SBR_LPC_ESTIMATE_SRC := src/decoders/modern/aac/sauti_aac_sbr_lpc_estimate.c

$(OBJDIR)/test_aac_sbr_lpc_estimate: tests/test_aac_sbr_lpc_estimate.c include/sauti_aac_sbr_lpc_estimate.h $(AAC_SBR_LPC_ESTIMATE_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_lpc_estimate.c $(AAC_SBR_LPC_ESTIMATE_SRC) -lm -no-pie

.PHONY: test-aac-sbr-lpc-estimate
test-aac-sbr-lpc-estimate: $(OBJDIR)/test_aac_sbr_lpc_estimate
	./$(OBJDIR)/test_aac_sbr_lpc_estimate

AAC_SBR_TEMPORAL_ENERGY_AUDIT_SRC := src/decoders/modern/aac/sauti_aac_sbr_temporal_energy_audit.c
AAC_SBR_TEMPORAL_ENERGY_AUDIT_DEPS := $(sort $(AAC_SBR_TEMPORAL_MIX_DEPS) $(AAC_SBR_TEMPORAL_ENERGY_AUDIT_SRC))

$(OBJDIR)/test_aac_sbr_temporal_energy_audit: tests/test_aac_sbr_temporal_energy_audit.c include/sauti_aac_sbr_temporal_energy_audit.h $(AAC_SBR_TEMPORAL_ENERGY_AUDIT_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_temporal_energy_audit.c $(AAC_SBR_TEMPORAL_ENERGY_AUDIT_DEPS) -lm -no-pie

.PHONY: test-aac-sbr-temporal-energy-audit
test-aac-sbr-temporal-energy-audit: $(OBJDIR)/test_aac_sbr_temporal_energy_audit
	./$(OBJDIR)/test_aac_sbr_temporal_energy_audit

AAC_SBR_TEMPORAL_ENVELOPE_AUDIT_SRC := src/decoders/modern/aac/sauti_aac_sbr_temporal_envelope_audit.c
AAC_SBR_TEMPORAL_ENVELOPE_AUDIT_DEPS := $(sort $(AAC_SBR_TEMPORAL_ENERGY_AUDIT_DEPS) $(AAC_SBR_TEMPORAL_ENVELOPE_AUDIT_SRC))

$(OBJDIR)/test_aac_sbr_temporal_envelope_audit: tests/test_aac_sbr_temporal_envelope_audit.c include/sauti_aac_sbr_temporal_envelope_audit.h $(AAC_SBR_TEMPORAL_ENVELOPE_AUDIT_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_temporal_envelope_audit.c $(AAC_SBR_TEMPORAL_ENVELOPE_AUDIT_DEPS) -lm -no-pie

.PHONY: test-aac-sbr-temporal-envelope-audit
test-aac-sbr-temporal-envelope-audit: $(OBJDIR)/test_aac_sbr_temporal_envelope_audit
	./$(OBJDIR)/test_aac_sbr_temporal_envelope_audit

AAC_SBR_ADTS_QMF_DEPS := $(sort $(AAC_HE_ADTS_QMF_SRC) $(AAC_SBR_ADTS_MONO_DEPS) $(AAC_SBR_QMF_FRAME_DEPS))
AAC_HE_ADTS_PS_LINK = $(sort src/decoders/modern/aac/sauti_aac_he_ps_delay.c src/decoders/modern/aac/sauti_aac_he_ps_bridge.c src/decoders/modern/aac/sauti_aac_ps_render.c src/decoders/modern/aac/sauti_aac_ps_map.c src/decoders/modern/aac/sauti_aac_ps_matrix.c src/decoders/modern/aac/sauti_aac_ps_phase.c src/decoders/modern/aac/sauti_aac_ps_pcm_stereo.c src/decoders/modern/aac/sauti_aac_ps_stereo.c src/decoders/modern/aac/sauti_aac_ps_hybrid_synth.c src/decoders/modern/aac/sauti_aac_ps_qmf_bank.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c src/decoders/modern/aac/sauti_aac_ps_qmf_high.c src/decoders/modern/aac/sauti_aac_ps_decorrelator.c src/decoders/modern/aac/sauti_aac_ps_decorrelate.c src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) $(AAC_PS_MIX_SRC) $(AAC_PS_MIX_ASM) $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) src/decoders/modern/aac/sauti_aac_he_adts_ps.c $(AAC_SBR_ADTS_MONO_DEPS) $(AAC_SBR_QMF_FRAME_DEPS))
$(OBJDIR)/test_aac_he_adts_ps: tests/test_aac_he_adts_ps.c tests/test_aac_sbr_adts_mono.c include/sauti_aac_he_adts_ps.h include/sauti_aac_he_ps_delay.h $(AAC_HE_ADTS_PS_LINK) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_he_adts_ps.c $(AAC_HE_ADTS_PS_LINK) -lm -no-pie

.PHONY: test-aac-he-adts-ps
test-aac-he-adts-ps: $(OBJDIR)/test_aac_he_adts_ps
	./$(OBJDIR)/test_aac_he_adts_ps

$(OBJDIR)/sauti_he_ps_decode: tools/sauti_he_ps_decode.c include/sauti_aac_he_adts_ps.h $(AAC_HE_ADTS_PS_LINK) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tools/sauti_he_ps_decode.c $(AAC_HE_ADTS_PS_LINK) -lm -no-pie

# Diagnostyka HE-AAC na prawdziwych plikach: ile ramek SBR wymaga obwiedni "temporalnej" (poza biezaca ramka), ile ma PS (make he-frame-stats FILE=plik.aac).
$(OBJDIR)/sauti_he_frame_stats: tools/sauti_he_frame_stats.c include/sauti_aac_he_adts_mono.h $(AAC_HE_ADTS_PS_LINK) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tools/sauti_he_frame_stats.c $(AAC_HE_ADTS_PS_LINK) -lm -no-pie

.PHONY: he-frame-stats
he-frame-stats: $(OBJDIR)/sauti_he_frame_stats
	@if [ -z "$(FILE)" ]; then echo "podaj FILE=plik.aac"; exit 2; fi; ./$(OBJDIR)/sauti_he_frame_stats "$(FILE)"

.PHONY: probe-he-ps test-aac-ps5-compare-help
probe-he-ps: $(OBJDIR)/sauti_he_ps_decode

AAC_PS5_HF_GAIN_SRC := src/decoders/modern/aac/sauti_aac_ps5_hf_gain.c
AAC_PS5_HF_ENVELOPE_SRC := src/decoders/modern/aac/sauti_aac_ps5_hf_envelope.c
AAC_PS5_CROSS_SRC := src/decoders/modern/aac/sauti_aac_ps5_cross.c
AAC_PS5_TEMPORAL_LINK := $(sort $(AAC_HE_ADTS_PS_LINK) $(AAC_PS5_HF_GAIN_SRC) $(AAC_PS5_HF_ENVELOPE_SRC) $(AAC_PS5_CROSS_SRC) $(AAC_SBR_LIMITER_BANDS_SRC) $(AAC_SBR_HF_LIMITER_SRC))
$(OBJDIR)/sauti_ps5_temporal_audit: tools/sauti_ps5_temporal_audit.c include/sauti_aac_he_adts_mono.h include/sauti_aac_sbr_qmf_frame.h $(AAC_PS5_TEMPORAL_LINK) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tools/sauti_ps5_temporal_audit.c $(AAC_PS5_TEMPORAL_LINK) -lm -no-pie

.PHONY: probe-he-ps-temporal
probe-he-ps-temporal: $(OBJDIR)/sauti_ps5_temporal_audit

$(OBJDIR)/test_aac_ps5_hf_gain: tests/test_aac_ps5_hf_gain.c include/sauti_aac_ps5_hf_gain.h $(AAC_PS5_HF_GAIN_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_ps5_hf_gain.c $(AAC_PS5_HF_GAIN_SRC) -lm

$(OBJDIR)/test_aac_ps5_hf_envelope: tests/test_aac_ps5_hf_envelope.c include/sauti_aac_ps5_hf_envelope.h $(AAC_PS5_HF_ENVELOPE_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_ps5_hf_envelope.c $(AAC_PS5_HF_ENVELOPE_SRC) -lm
$(OBJDIR)/test_aac_ps5_cross: tests/test_aac_ps5_cross.c include/sauti_aac_ps5_cross.h $(AAC_PS5_CROSS_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_ps5_cross.c $(AAC_PS5_CROSS_SRC) -lm
$(OBJDIR)/test_aac_ps_owned_geometry: tests/test_aac_ps_owned_geometry.c include/sauti_aac_sbr_qmf_frame.h $(AAC_SBR_QMF_FRAME_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_ps_owned_geometry.c $(AAC_SBR_QMF_FRAME_DEPS) -lm -no-pie
.PHONY: test-aac-ps-owned-geometry
test-aac-ps-owned-geometry: $(OBJDIR)/test_aac_ps_owned_geometry
	./$(OBJDIR)/test_aac_ps_owned_geometry

.PHONY: test-aac-ps5-cross
test-aac-ps5-cross: $(OBJDIR)/test_aac_ps5_cross
	./$(OBJDIR)/test_aac_ps5_cross

.PHONY: test-aac-ps5-hf-envelope
test-aac-ps5-hf-envelope: $(OBJDIR)/test_aac_ps5_hf_envelope
	./$(OBJDIR)/test_aac_ps5_hf_envelope

.PHONY: test-aac-ps5-hf-gain
test-aac-ps5-hf-gain: $(OBJDIR)/test_aac_ps5_hf_gain
	./$(OBJDIR)/test_aac_ps5_hf_gain

.PHONY: test-aac-ps-tonal-spectrum
test-aac-ps-tonal-spectrum:
	python3 tools/sauti_ps_tonal_spectrum.py --self-test

.PHONY: test-aac-ps5-temporal
test-aac-ps5-temporal: test-aac-ps-owned-geometry test-aac-ps5-cross test-aac-ps5-hf-envelope test-aac-ps5-hf-gain test-aac-ps-tonal-spectrum $(OBJDIR)/sauti_ps5_temporal_audit $(OBJDIR)/test_aac_he_adts_ps
	python3 tools/test_sauti_ps5_temporal.py ./$(OBJDIR)/test_aac_he_adts_ps ./$(OBJDIR)/sauti_ps5_temporal_audit

.PHONY: test-aac-ps5-native
test-aac-ps5-native: $(OBJDIR)/sauti_he_ps_decode $(OBJDIR)/test_aac_he_adts_ps
	python3 tools/test_sauti_ps5_native.py ./$(OBJDIR)/test_aac_he_adts_ps ./$(OBJDIR)/sauti_he_ps_decode
test-aac-ps5-compare-help: probe-he-ps
	@echo "Run: python3 tools/sauti_ps5_compare.py INPUT.aac --native ./$(OBJDIR)/sauti_he_ps_decode --rate 48000"


AAC_SBR_ADTS_CPE_DEPS := $(sort $(AAC_HE_ADTS_CPE_QMF_SRC) $(AAC_HE_ADTS_CPE_SRC) src/decoders/modern/aac/sauti_aac_cpe.c $(AAC_LC_MULTI_SRC) $(AAC_SBR_ADTS_QMF_DEPS))
$(OBJDIR)/test_aac_sbr_adts_cpe: tests/test_aac_sbr_adts_cpe.c tests/test_aac_sbr_payload.c include/sauti_aac_he_adts_cpe.h include/sauti_decoder_aac_native.h src/sauti_decoder.c src/decoders/modern/aac/sauti_decoder_aac_native.c $(AAC_NATIVE_ADAPTER_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_adts_cpe.c src/sauti_decoder.c $(AAC_NATIVE_ADAPTER_DEPS) -lm -no-pie

.PHONY: test-aac-sbr-adts-cpe
test-aac-sbr-adts-cpe: $(OBJDIR)/test_aac_sbr_adts_cpe
	./$(OBJDIR)/test_aac_sbr_adts_cpe

$(OBJDIR)/test_aac_adts_crc: tests/test_aac_adts_crc.c include/sauti_aac_adts_crc.h $(AAC_ADTS_CRC_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_adts_crc.c $(AAC_ADTS_CRC_SRC) -no-pie

.PHONY: test-aac-adts-crc
test-aac-adts-crc: $(OBJDIR)/test_aac_adts_crc
	./$(OBJDIR)/test_aac_adts_crc

$(OBJDIR)/test_aac_sbr_adts_qmf: tests/test_aac_sbr_adts_qmf.c tests/test_aac_sbr_adts_mono.c include/sauti_aac_he_adts_qmf.h include/sauti_decoder_aac_native.h src/sauti_decoder.c $(AAC_NATIVE_ADAPTER_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_adts_qmf.c src/sauti_decoder.c $(AAC_NATIVE_ADAPTER_DEPS) -lm -no-pie

.PHONY: test-aac-sbr-adts-qmf
test-aac-sbr-adts-qmf: $(OBJDIR)/test_aac_sbr_adts_qmf
	./$(OBJDIR)/test_aac_sbr_adts_qmf

$(OBJDIR)/sauti_he_aac_probe: tools/sauti_he_aac_probe.c include/sauti_aac_he_adts_qmf.h $(AAC_SBR_ADTS_QMF_DEPS) $(AAC_SBR_TEMPORAL_QMF_SRC) $(AAC_SBR_TEMPORAL_POWER_SRC) $(AAC_SBR_TEMPORAL_GAIN_SRC) $(AAC_SBR_TEMPORAL_APPLY_SRC) $(AAC_SBR_TEMPORAL_NOISE_SRC) $(AAC_SBR_TEMPORAL_MIX_SRC) $(AAC_SBR_TEMPORAL_SMOOTH_SRC) $(AAC_SBR_TEMPORAL_PCM_PREVIEW_SRC) $(AAC_SBR_TEMPORAL_ENERGY_AUDIT_SRC) $(AAC_SBR_TEMPORAL_ENVELOPE_AUDIT_SRC) $(AAC_SBR_LPC_ESTIMATE_SRC) $(AAC_SBR_TEMPORAL_HARMONIC_PREVIEW_SRC) $(AAC_SBR_TEMPORAL_HF_UNITS_SRC) $(AAC_SBR_TEMPORAL_ENERGY_UNITS_SRC) $(AAC_SBR_GAIN_PARTITION_SRC) $(AAC_SBR_GAIN_LIMIT_PARTITION_SRC) $(AAC_SBR_TEMPORAL_REFERENCE_GAIN_SRC) $(AAC_SBR_TEMPORAL_GAIN_BRIDGE_SRC) $(AAC_SBR_HF_PHASE_PROBE_SRC) $(AAC_SBR_TEMPORAL_SOURCE6_SRC) $(AAC_SBR_HF_BAND_AUDIT_SRC) $(AAC_SBR_HF_PATCH_ROUTE_AUDIT_SRC) src/decoders/modern/aac/sauti_aac_sbr_low_source_audit.c | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tools/sauti_he_aac_probe.c $(AAC_SBR_ADTS_QMF_DEPS) $(AAC_SBR_TEMPORAL_QMF_SRC) $(AAC_SBR_TEMPORAL_POWER_SRC) $(AAC_SBR_TEMPORAL_GAIN_SRC) $(AAC_SBR_TEMPORAL_APPLY_SRC) $(AAC_SBR_TEMPORAL_NOISE_SRC) $(AAC_SBR_TEMPORAL_MIX_SRC) $(AAC_SBR_TEMPORAL_SMOOTH_SRC) $(AAC_SBR_TEMPORAL_PCM_PREVIEW_SRC) $(AAC_SBR_TEMPORAL_ENERGY_AUDIT_SRC) $(AAC_SBR_TEMPORAL_ENVELOPE_AUDIT_SRC) $(AAC_SBR_LPC_ESTIMATE_SRC) $(AAC_SBR_TEMPORAL_HARMONIC_PREVIEW_SRC) $(AAC_SBR_TEMPORAL_HF_UNITS_SRC) $(AAC_SBR_TEMPORAL_ENERGY_UNITS_SRC) $(AAC_SBR_GAIN_PARTITION_SRC) $(AAC_SBR_GAIN_LIMIT_PARTITION_SRC) $(AAC_SBR_TEMPORAL_REFERENCE_GAIN_SRC) $(AAC_SBR_TEMPORAL_GAIN_BRIDGE_SRC) $(AAC_SBR_HF_PHASE_PROBE_SRC) $(AAC_SBR_TEMPORAL_SOURCE6_SRC) $(AAC_SBR_HF_BAND_AUDIT_SRC) $(AAC_SBR_HF_PATCH_ROUTE_AUDIT_SRC) src/decoders/modern/aac/sauti_aac_sbr_low_source_audit.c -lm -no-pie

.PHONY: test-aac-he-stem-ablation
test-aac-he-stem-ablation:
	python3 tools/sauti_he_aac_stem_ablation.py --self-test

.PHONY: test-aac-he-preview-compare
test-aac-he-preview-compare:
	python3 tools/sauti_he_aac_compare_pcm.py --self-test

# SBR-82: mono HE-AAC through the standard-structured channel decoder (tool, not a product).
AAC_SBR_CHANNEL_CLI_SRC := $(OBJDIR)/sauti_aac_dequant.o $(OBJDIR)/sauti_aac_dsp.o $(OBJDIR)/sauti_aac_headers.o $(OBJDIR)/sauti_aac_huffman.o $(OBJDIR)/sauti_aac_imdct.o $(OBJDIR)/sauti_aac_sbr_qmf_synth_mod_asm.o $(OBJDIR)/sauti_aac_sbr_qmf_synth_mod_avx2.o $(OBJDIR)/sauti_aac_sbr_qmf_synth_window_asm.o $(OBJDIR)/sauti_aac_sbr_qmf_synth_window_avx2.o $(OBJDIR)/sauti_aac_spectral.o src/decoders/modern/aac/sauti_aac_adts_crc.c src/decoders/modern/aac/sauti_aac_bands.c src/decoders/modern/aac/sauti_aac_dequant.c src/decoders/modern/aac/sauti_aac_he_adts_mono.c src/decoders/modern/aac/sauti_aac_he_adts_qmf.c src/decoders/modern/aac/sauti_aac_huffman.c src/decoders/modern/aac/sauti_aac_imdct.c src/decoders/modern/aac/sauti_aac_lc_frame.c src/decoders/modern/aac/sauti_aac_lc_sections.c src/decoders/modern/aac/sauti_aac_lc_synth.c src/decoders/modern/aac/sauti_aac_pns.c src/decoders/modern/aac/sauti_aac_sbr_channel.c src/decoders/modern/aac/sauti_usac_pvc.c src/decoders/modern/aac/sauti_usac_pvc_tables.c src/decoders/modern/aac/sauti_usac_hbe.c src/decoders/modern/aac/sauti_usac_hbe_tables.c src/decoders/modern/aac/sauti_usac_mps.c src/decoders/modern/aac/sauti_aac_eld_sac.c src/decoders/modern/aac/sauti_unidrc.c src/decoders/modern/aac/sauti_unidrc_run.c src/decoders/modern/aac/sauti_unidrc_tables.c src/decoders/modern/aac/sauti_aac_sbr_mpsld.c src/decoders/modern/aac/sauti_aac_sbr_mpsld_table.c src/decoders/modern/aac/sauti_aac_eld_sac_dec.c src/decoders/modern/aac/sauti_aac_eld_sac_tables.c src/decoders/modern/aac/sauti_usac_mps_tables.c src/decoders/modern/aac/sauti_usac_mps_dec.c src/decoders/modern/aac/sauti_aac_sbr_control.c src/decoders/modern/aac/sauti_aac_sbr_crc.c src/decoders/modern/aac/sauti_aac_sbr_energy.c src/decoders/modern/aac/sauti_aac_sbr_gain_limit_partition.c src/decoders/modern/aac/sauti_aac_sbr_gain_partition.c src/decoders/modern/aac/sauti_aac_sbr_grid.c src/decoders/modern/aac/sauti_aac_sbr_hf.c src/decoders/modern/aac/sauti_aac_sbr_hf_band_audit.c src/decoders/modern/aac/sauti_aac_sbr_hf_joint_rom.c src/decoders/modern/aac/sauti_aac_sbr_hf_limiter.c src/decoders/modern/aac/sauti_aac_sbr_hf_patch_route_audit.c src/decoders/modern/aac/sauti_aac_sbr_hf_phase_probe.c src/decoders/modern/aac/sauti_aac_sbr_hf_predict.c src/decoders/modern/aac/sauti_aac_sbr_hf_sine.c src/decoders/modern/aac/sauti_aac_sbr_huffman.c src/decoders/modern/aac/sauti_aac_sbr_limiter_bands.c src/decoders/modern/aac/sauti_aac_sbr_limits.c src/decoders/modern/aac/sauti_aac_sbr_low_source_audit.c src/decoders/modern/aac/sauti_aac_sbr_lpc_estimate.c src/decoders/modern/aac/sauti_aac_sbr_master.c src/decoders/modern/aac/sauti_aac_sbr_master_log.c src/decoders/modern/aac/sauti_aac_sbr_noise_rom.c src/decoders/modern/aac/sauti_aac_sbr_patch.c src/decoders/modern/aac/sauti_aac_sbr_payload.c src/decoders/modern/aac/sauti_aac_sbr_qmf_analysis.c src/decoders/modern/aac/sauti_aac_sbr_qmf_frame.c src/decoders/modern/aac/sauti_aac_sbr_qmf_synth_mod.c src/decoders/modern/aac/sauti_aac_sbr_qmf_synth_window.c src/decoders/modern/aac/sauti_aac_sbr_quant.c src/decoders/modern/aac/sauti_aac_sbr_smooth.c src/decoders/modern/aac/sauti_aac_sbr_tables.c src/decoders/modern/aac/sauti_aac_sbr_temporal_apply.c src/decoders/modern/aac/sauti_aac_sbr_temporal_energy_audit.c src/decoders/modern/aac/sauti_aac_sbr_temporal_energy_units.c src/decoders/modern/aac/sauti_aac_sbr_temporal_envelope_audit.c src/decoders/modern/aac/sauti_aac_sbr_temporal_gain.c src/decoders/modern/aac/sauti_aac_sbr_temporal_gain_bridge.c src/decoders/modern/aac/sauti_aac_sbr_temporal_harmonic_preview.c src/decoders/modern/aac/sauti_aac_sbr_temporal_hf_units.c src/decoders/modern/aac/sauti_aac_sbr_temporal_mix.c src/decoders/modern/aac/sauti_aac_sbr_temporal_noise.c src/decoders/modern/aac/sauti_aac_sbr_temporal_pcm_preview.c src/decoders/modern/aac/sauti_aac_sbr_temporal_power.c src/decoders/modern/aac/sauti_aac_sbr_temporal_qmf.c src/decoders/modern/aac/sauti_aac_sbr_temporal_reference_gain.c src/decoders/modern/aac/sauti_aac_sbr_temporal_smooth.c src/decoders/modern/aac/sauti_aac_sbr_temporal_source6.c src/decoders/modern/aac/sauti_aac_scalefactors.c src/decoders/modern/aac/sauti_aac_spectral.c src/decoders/modern/aac/sauti_aac_spectral_data.c src/decoders/modern/aac/sauti_aac_syntax.c src/decoders/modern/aac/sauti_aac_tables.c src/decoders/modern/aac/sauti_aac_tns.c src/decoders/modern/aac/sauti_aac_window.c $(AAC_HE_ADTS_PS_LINK) src/decoders/modern/aac/sauti_aac_he_adts_cpe.c src/decoders/modern/aac/sauti_aac_cpe.c src/decoders/modern/aac/sauti_aac_pred.c src/decoders/modern/aac/sauti_aac_ltp.c
$(OBJDIR)/sauti_he_decode_cli: tools/sauti_he_decode_cli.c include/sauti_aac_sbr_channel.h $(AAC_SBR_CHANNEL_CLI_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tools/sauti_he_decode_cli.c $(sort $(AAC_SBR_CHANNEL_CLI_SRC)) -lm -no-pie

$(OBJDIR)/sauti_he_remix: tools/sauti_he_remix.c $(AAC_SBR_CHANNEL_CLI_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tools/sauti_he_remix.c $(sort $(AAC_SBR_CHANNEL_CLI_SRC)) -lm -no-pie

.PHONY: he-remix
he-remix: $(OBJDIR)/sauti_he_remix


# SBR-84: one PS stage at a time against the instrumented FFmpeg's recorded stage outputs (tool, not a product).
$(OBJDIR)/sauti_ps_stage_probe: tools/sauti_ps_stage_probe.c $(sort $(AAC_SBR_CHANNEL_CLI_SRC)) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tools/sauti_ps_stage_probe.c $(sort $(AAC_SBR_CHANNEL_CLI_SRC)) -lm -no-pie

.PHONY: he-decode-cli
he-decode-cli: $(OBJDIR)/sauti_he_decode_cli

# Needs an HE-AAC file and the instrumented FFmpeg (outside test-aac-native): make test-aac-he-channel-ffmpeg HE_FILE=/path/file.aac
# Generated SBR data on a real core (needs the instrumented FFmpeg and a core stream): make test-aac-he-channel-remix CORE=/path/core.aac
.PHONY: test-aac-he-channel-remix
test-aac-he-channel-remix: $(OBJDIR)/sauti_he_remix $(OBJDIR)/sauti_he_decode_cli
	bash scripts/test_he_channel_remix_vs_ffmpeg.sh "$(CORE)" $(or $(HE_FRAMES),200)

.PHONY: test-aac-he-channel-ffmpeg
test-aac-he-channel-ffmpeg: $(OBJDIR)/sauti_he_decode_cli
	bash scripts/test_he_channel_vs_ffmpeg.sh "$(HE_FILE)" $(or $(HE_FRAMES),400)

.PHONY: probe-he-aac test-aac-he-probe-build
probe-he-aac test-aac-he-probe-build: $(OBJDIR)/sauti_he_aac_probe
	@echo "HE-AAC real ADTS probe ready: ./$(OBJDIR)/sauti_he_aac_probe FILE.aac [MAX_FRAMES]"

# SBR-78: LPC/chirp sondy kontra instrumentowany FFmpeg na prawdziwym pliku HE-AAC z filtrowaniem odwrotnym.  Poza test-aac-native (wymaga
# zewnetrznego FFmpeg i pliku): make test-aac-he-lpc-ffmpeg HE_FILE=/sciezka/plik.aac [HE_FRAMES=400] [SAUTI_FFMPEG_INSTR=.../ffmpeg]
.PHONY: test-aac-he-lpc-ffmpeg
test-aac-he-lpc-ffmpeg: $(OBJDIR)/sauti_he_aac_probe
	bash scripts/test_he_lpc_vs_ffmpeg.sh "$(HE_FILE)" $(or $(HE_FRAMES),400)

.PHONY: test-aac-he-probe-smoke
test-aac-he-probe-smoke: $(OBJDIR)/sauti_he_aac_probe $(OBJDIR)/test_aac_sbr_adts_qmf
	@set -eu; fixture=$(mktemp --suffix=.aac); \
	trap 'rm -f "$fixture"' EXIT; \
	./$(OBJDIR)/test_aac_sbr_adts_qmf --emit "$fixture"; \
	./$(OBJDIR)/sauti_he_aac_probe "$fixture" 2

$(OBJDIR)/test_aac_lc_sections: tests/test_aac_lc_sections.c include/sauti_aac_lc_sections.h include/sauti_aac_syntax.h $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_HEADERS_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_lc_sections.c $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_HEADERS_ASM) -no-pie

.PHONY: test-aac-lc-sections
test-aac-lc-sections: $(OBJDIR)/test_aac_lc_sections
	./$(OBJDIR)/test_aac_lc_sections

$(AAC_HUFFMAN_ASM): src/decoders/modern/aac/sauti_aac_huffman.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(OBJDIR)/test_aac_huffman: tests/test_aac_huffman.c include/sauti_aac_huffman.h $(AAC_HUFFMAN_SRC) $(AAC_HUFFMAN_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_huffman.c $(AAC_HUFFMAN_SRC) $(AAC_HUFFMAN_ASM) -no-pie

.PHONY: test-aac-huffman
test-aac-huffman: $(OBJDIR)/test_aac_huffman
	./$(OBJDIR)/test_aac_huffman

$(AAC_SPECTRAL_ASM): src/decoders/modern/aac/sauti_aac_spectral.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(OBJDIR)/test_aac_spectral: tests/test_aac_spectral.c include/sauti_aac_spectral.h $(AAC_SPECTRAL_SRC) $(AAC_SPECTRAL_ASM) $(AAC_HUFFMAN_SRC) $(AAC_HUFFMAN_ASM) $(AAC_HEADERS_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_spectral.c $(AAC_SPECTRAL_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_ASM) $(AAC_HUFFMAN_ASM) $(AAC_HEADERS_ASM) -no-pie

.PHONY: test-aac-spectral
test-aac-spectral: $(OBJDIR)/test_aac_spectral
	./$(OBJDIR)/test_aac_spectral

$(OBJDIR)/test_aac_tables: tests/test_aac_tables.c include/sauti_aac_tables.h $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_HEADERS_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_tables.c $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_HEADERS_ASM) -no-pie

.PHONY: test-aac-tables
test-aac-tables: $(OBJDIR)/test_aac_tables
	./$(OBJDIR)/test_aac_tables

$(OBJDIR)/test_aac_scalefactors: tests/test_aac_scalefactors.c include/sauti_aac_scalefactors.h $(AAC_SCALEFACTORS_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_HUFFMAN_ASM) $(AAC_HEADERS_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_scalefactors.c $(AAC_SCALEFACTORS_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_HUFFMAN_ASM) $(AAC_HEADERS_ASM) -no-pie

.PHONY: test-aac-scalefactors
test-aac-scalefactors: $(OBJDIR)/test_aac_scalefactors
	./$(OBJDIR)/test_aac_scalefactors

$(OBJDIR)/test_aac_spectral_data: tests/test_aac_spectral_data.c include/sauti_aac_spectral_data.h $(AAC_SPECTRAL_DATA_SRC) $(AAC_BANDS_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_HEADERS_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_spectral_data.c $(AAC_SPECTRAL_DATA_SRC) $(AAC_BANDS_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_HEADERS_ASM) -no-pie

.PHONY: test-aac-spectral-data
test-aac-spectral-data: $(OBJDIR)/test_aac_spectral_data
	./$(OBJDIR)/test_aac_spectral_data

$(AAC_DEQUANT_ASM): src/decoders/modern/aac/sauti_aac_dequant.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(OBJDIR)/test_aac_dequant: tests/test_aac_dequant.c include/sauti_aac_dequant.h $(AAC_DEQUANT_SRC) $(AAC_DEQUANT_ASM) $(AAC_BANDS_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_dequant.c $(AAC_DEQUANT_SRC) $(AAC_BANDS_SRC) $(AAC_DEQUANT_ASM) -lm -no-pie

.PHONY: test-aac-dequant
test-aac-dequant: $(OBJDIR)/test_aac_dequant
	./$(OBJDIR)/test_aac_dequant

$(AAC_IMDCT_ASM): src/decoders/modern/aac/sauti_aac_imdct.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)

$(OBJDIR)/test_aac_imdct: tests/test_aac_imdct.c include/sauti_aac_imdct.h $(AAC_IMDCT_SRC) $(AAC_IMDCT_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_imdct.c $(AAC_IMDCT_SRC) $(AAC_IMDCT_ASM) -lm -no-pie

.PHONY: test-aac-imdct
test-aac-imdct: $(OBJDIR)/test_aac_imdct
	./$(OBJDIR)/test_aac_imdct

$(OBJDIR)/test_aac_window: tests/test_aac_window.c include/sauti_aac_window.h $(AAC_WINDOW_SRC) $(AAC_DSP_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_window.c $(AAC_WINDOW_SRC) $(AAC_DSP_ASM) -lm -no-pie

.PHONY: test-aac-window
test-aac-window: $(OBJDIR)/test_aac_window
	./$(OBJDIR)/test_aac_window

$(OBJDIR)/test_aac_lc_synth: tests/test_aac_lc_synth.c include/sauti_aac_lc_synth.h $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_lc_synth.c $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) -lm -no-pie

.PHONY: test-aac-lc-synth
test-aac-lc-synth: $(OBJDIR)/test_aac_lc_synth
	./$(OBJDIR)/test_aac_lc_synth

$(OBJDIR)/test_aac_lc_frame: tests/test_aac_lc_frame.c include/sauti_aac_lc_frame.h $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_lc_frame.c $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) -lm -no-pie

.PHONY: test-aac-lc-frame
test-aac-lc-frame: $(OBJDIR)/test_aac_lc_frame
	./$(OBJDIR)/test_aac_lc_frame

$(OBJDIR)/test_aac_real_mono: tests/test_aac_real_mono.c include/sauti_aac_lc_frame.h $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_real_mono.c $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) -lm -no-pie

# AAC-14 real compressed fixture. Requires ffmpeg CLI but not libav*.
.PHONY: test-aac-real-mono
test-aac-real-mono: $(OBJDIR)/test_aac_real_mono
	@if command -v ffmpeg >/dev/null 2>&1; then \
	  mkdir -p build/audio-fixtures; \
	  ffmpeg -hide_banner -nostdin -loglevel error -y -f lavfi \
	    -i "anullsrc=r=44100:cl=mono:d=0.12" -c:a aac -b:a 48k \
	    -f adts build/audio-fixtures/aac_native_mono.aac && \
	  ./$(OBJDIR)/test_aac_real_mono build/audio-fixtures/aac_native_mono.aac; \
	else echo "SKIP AAC-14 real fixture: ffmpeg CLI unavailable"; fi

# AAC-15: generate a *non-silent* AAC-LC mono tone, render native PCM,
# and compare against the FFmpeg CLI reference (no libav* dependency).
.PHONY: test-aac-tone
test-aac-tone: $(OBJDIR)/test_aac_real_mono
	@if command -v ffmpeg >/dev/null 2>&1; then \
	  mkdir -p build/audio-fixtures; \
	  ffmpeg -hide_banner -nostdin -loglevel error -y -f lavfi \
	    -i "sine=frequency=440:sample_rate=44100:duration=0.45" \
	    -ac 1 -c:a aac -b:a 96k -f adts \
	    build/audio-fixtures/aac_native_tone.aac && \
	  ffmpeg -hide_banner -nostdin -loglevel error -y \
	    -i build/audio-fixtures/aac_native_tone.aac -f f32le \
	    -ac 1 -ar 44100 build/audio-fixtures/aac_ref_tone.f32 && \
	  ./$(OBJDIR)/test_aac_real_mono \
	    build/audio-fixtures/aac_native_tone.aac \
	    build/audio-fixtures/aac_native_tone.f32 \
	    build/audio-fixtures/aac_native_tone.mask && \
	  python3 tools/compare_aac_pcm.py \
	    build/audio-fixtures/aac_native_tone.f32 \
	    build/audio-fixtures/aac_ref_tone.f32 \
	    build/audio-fixtures/aac_native_tone.mask; \
	else echo "SKIP AAC tone diagnostic: ffmpeg CLI unavailable"; fi

$(OBJDIR)/bench_aac_imdct: tests/bench_aac_imdct.c include/sauti_aac_imdct.h $(AAC_IMDCT_SRC) $(AAC_IMDCT_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/bench_aac_imdct.c $(AAC_IMDCT_SRC) $(AAC_IMDCT_ASM) -lm -no-pie

.PHONY: bench-aac-imdct
bench-aac-imdct: $(OBJDIR)/bench_aac_imdct
	./$(OBJDIR)/bench_aac_imdct

# AAC-15..18 remains diagnostic, not part of the codec conformance gate.
.PHONY: inspect-aac-15-18
inspect-aac-15-18: test-aac-tone bench-aac-imdct

$(OBJDIR)/test_aac_pns: tests/test_aac_pns.c include/sauti_aac_pns.h $(AAC_PNS_SRC) $(AAC_BANDS_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_pns.c $(AAC_PNS_SRC) $(AAC_BANDS_SRC) -lm

.PHONY: test-aac-pns
test-aac-pns: $(OBJDIR)/test_aac_pns
	./$(OBJDIR)/test_aac_pns

# AAC-23: deterministic five-signal matrix; FFmpeg used only for fixture
# encoding and reference PCM generation, never for native decoding.
.PHONY: test-aac-pcm-compare
test-aac-pcm-compare:
	python3 tests/test_compare_aac_pcm.py

.PHONY: inspect-aac-lc-matrix
inspect-aac-lc-matrix: $(OBJDIR)/test_aac_real_mono test-aac-pcm-compare
	sh scripts/inspect_aac_lc_matrix.sh


$(OBJDIR)/test_aac_tns: tests/test_aac_tns.c include/sauti_aac_tns.h $(AAC_TNS_SRC) $(AAC_BANDS_SRC) $(AAC_HEADERS_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_tns.c $(AAC_TNS_SRC) $(AAC_BANDS_SRC) $(AAC_HEADERS_ASM) -lm -no-pie

.PHONY: test-aac-tns
test-aac-tns: $(OBJDIR)/test_aac_tns
	./$(OBJDIR)/test_aac_tns

# Native AAC-LC is linked only into SDL player binaries; standalone
# decoder tests use their own explicit source/object dependencies.
AAC_PLAYER_SRC = $(sort $(AAC_NATIVE_ADAPTER_DEPS) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC))
AAC_PLAYER_ASM = $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM)

AAC_CPE_SRC := src/decoders/modern/aac/sauti_aac_cpe.c src/decoders/modern/aac/sauti_aac_pred.c src/decoders/modern/aac/sauti_aac_ltp.c

$(OBJDIR)/test_aac_cpe: tests/test_aac_cpe.c include/sauti_aac_cpe.h $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_cpe.c $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) -lm -no-pie

# ASan/UBSan is applied to the C sources of the CPE decoder and to the
# independent AAC syntax/PS parser test. Existing NASM objects are linked
# unchanged, so sanitizer coverage does not extend into hand-written assembly.
AAC_SAN_FLAGS := -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
$(OBJDIR)/test_aac_cpe_san: tests/test_aac_cpe.c include/sauti_aac_cpe.h $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_cpe.c $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) -lm -no-pie

$(OBJDIR)/test_aac_syntax_san: tests/test_aac_syntax.c include/sauti_aac_syntax.h $(AAC_SYNTAX_SRC) $(AAC_HEADERS_ASM) $(AAC_DSP_ASM) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_syntax.c $(AAC_SYNTAX_SRC) $(AAC_HEADERS_ASM) $(AAC_DSP_ASM) -no-pie

$(OBJDIR)/test_aac_ps_map_san: tests/test_aac_ps_map.c include/sauti_aac_ps_map.h $(AAC_PS_MAP_SRC) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_ps_map.c $(AAC_PS_MAP_SRC)

$(OBJDIR)/test_aac_ps_hybrid_san: tests/test_aac_ps_hybrid.c include/sauti_aac_ps_hybrid.h $(AAC_PS_HYBRID_SRC) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_ps_hybrid.c $(AAC_PS_HYBRID_SRC)

$(OBJDIR)/test_aac_ps_qmf_high_san: tests/test_aac_ps_qmf_high.c include/sauti_aac_ps_qmf_high.h src/decoders/modern/aac/sauti_aac_ps_qmf_high.c $(AAC_PS_HYBRID_SRC) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_ps_qmf_high.c src/decoders/modern/aac/sauti_aac_ps_qmf_high.c $(AAC_PS_HYBRID_SRC) -lm

$(OBJDIR)/test_aac_ps_qmf_low_san: tests/test_aac_ps_qmf_low.c include/sauti_aac_ps_qmf_low.h src/decoders/modern/aac/sauti_aac_ps_qmf_low.c $(AAC_PS_HYBRID_SRC) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_ps_qmf_low.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c $(AAC_PS_HYBRID_SRC) -lm

$(OBJDIR)/test_aac_ps_qmf_bank_san: tests/test_aac_ps_qmf_bank.c include/sauti_aac_ps_qmf_bank.h src/decoders/modern/aac/sauti_aac_ps_qmf_bank.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c src/decoders/modern/aac/sauti_aac_ps_qmf_high.c $(AAC_PS_HYBRID_SRC) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_ps_qmf_bank.c src/decoders/modern/aac/sauti_aac_ps_qmf_bank.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c src/decoders/modern/aac/sauti_aac_ps_qmf_high.c $(AAC_PS_HYBRID_SRC) -lm

$(OBJDIR)/test_aac_ps_qmf_reference_san: tests/test_aac_ps_qmf_reference.c include/sauti_aac_ps_qmf_low.h src/decoders/modern/aac/sauti_aac_ps_qmf_low.c $(AAC_PS_HYBRID_SRC) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_ps_qmf_reference.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c $(AAC_PS_HYBRID_SRC) -lm

$(OBJDIR)/test_aac_ps_transient_san: tests/test_aac_ps_transient.c include/sauti_aac_ps_transient.h src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_ps_transient.c src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) -lm

$(OBJDIR)/test_aac_ps_decorrelate_san: tests/test_aac_ps_decorrelate.c include/sauti_aac_ps_decorrelate.h src/decoders/modern/aac/sauti_aac_ps_decorrelate.c $(AAC_PS_HYBRID_SRC) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_ps_decorrelate.c src/decoders/modern/aac/sauti_aac_ps_decorrelate.c $(AAC_PS_HYBRID_SRC) -lm

$(OBJDIR)/test_aac_ps_decorrelator_san: tests/test_aac_ps_decorrelator.c include/sauti_aac_ps_decorrelator.h src/decoders/modern/aac/sauti_aac_ps_decorrelator.c src/decoders/modern/aac/sauti_aac_ps_decorrelate.c src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_ps_decorrelator.c src/decoders/modern/aac/sauti_aac_ps_decorrelator.c src/decoders/modern/aac/sauti_aac_ps_decorrelate.c src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) -lm

$(OBJDIR)/test_aac_ps_stereo_san: tests/test_aac_ps_stereo.c include/sauti_aac_ps_stereo.h src/decoders/modern/aac/sauti_aac_ps_stereo.c src/decoders/modern/aac/sauti_aac_ps_qmf_bank.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c src/decoders/modern/aac/sauti_aac_ps_qmf_high.c src/decoders/modern/aac/sauti_aac_ps_decorrelator.c src/decoders/modern/aac/sauti_aac_ps_decorrelate.c src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) $(AAC_PS_MIX_SRC) $(AAC_PS_MIX_ASM) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_ps_stereo.c src/decoders/modern/aac/sauti_aac_ps_stereo.c src/decoders/modern/aac/sauti_aac_ps_qmf_bank.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c src/decoders/modern/aac/sauti_aac_ps_qmf_high.c src/decoders/modern/aac/sauti_aac_ps_decorrelator.c src/decoders/modern/aac/sauti_aac_ps_decorrelate.c src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) $(AAC_PS_MIX_SRC) $(AAC_PS_MIX_ASM) -lm -no-pie

$(OBJDIR)/test_aac_ps_hybrid_synth_san: tests/test_aac_ps_hybrid_synth.c include/sauti_aac_ps_hybrid_synth.h src/decoders/modern/aac/sauti_aac_ps_hybrid_synth.c $(AAC_PS_HYBRID_SRC) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_ps_hybrid_synth.c src/decoders/modern/aac/sauti_aac_ps_hybrid_synth.c $(AAC_PS_HYBRID_SRC) -lm

$(OBJDIR)/test_aac_ps_pcm_stereo_san: include/sauti_aac_ps_pcm_stereo.h include/sauti_aac_ps_stereo.h include/sauti_aac_ps_hybrid_synth.h include/sauti_aac_sbr_qmf_synth_mod.h tests/test_aac_ps_pcm_stereo.c src/decoders/modern/aac/sauti_aac_ps_pcm_stereo.c src/decoders/modern/aac/sauti_aac_ps_stereo.c src/decoders/modern/aac/sauti_aac_ps_hybrid_synth.c src/decoders/modern/aac/sauti_aac_ps_qmf_bank.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c src/decoders/modern/aac/sauti_aac_ps_qmf_high.c src/decoders/modern/aac/sauti_aac_ps_decorrelator.c src/decoders/modern/aac/sauti_aac_ps_decorrelate.c src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) $(AAC_PS_MIX_SRC) $(AAC_PS_MIX_ASM) $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_ps_pcm_stereo.c src/decoders/modern/aac/sauti_aac_ps_pcm_stereo.c src/decoders/modern/aac/sauti_aac_ps_stereo.c src/decoders/modern/aac/sauti_aac_ps_hybrid_synth.c src/decoders/modern/aac/sauti_aac_ps_qmf_bank.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c src/decoders/modern/aac/sauti_aac_ps_qmf_high.c src/decoders/modern/aac/sauti_aac_ps_decorrelator.c src/decoders/modern/aac/sauti_aac_ps_decorrelate.c src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) $(AAC_PS_MIX_SRC) $(AAC_PS_MIX_ASM) $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) -lm -no-pie

$(OBJDIR)/test_aac_he_ps_bridge_san: include/sauti_aac_he_ps_bridge.h include/sauti_aac_ps_render.h include/sauti_aac_ps_pcm_stereo.h tests/test_aac_he_ps_bridge.c src/decoders/modern/aac/sauti_aac_he_ps_bridge.c src/decoders/modern/aac/sauti_aac_ps_render.c src/decoders/modern/aac/sauti_aac_ps_map.c src/decoders/modern/aac/sauti_aac_ps_matrix.c src/decoders/modern/aac/sauti_aac_ps_phase.c src/decoders/modern/aac/sauti_aac_ps_pcm_stereo.c src/decoders/modern/aac/sauti_aac_ps_stereo.c src/decoders/modern/aac/sauti_aac_ps_hybrid_synth.c src/decoders/modern/aac/sauti_aac_ps_qmf_bank.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c src/decoders/modern/aac/sauti_aac_ps_qmf_high.c src/decoders/modern/aac/sauti_aac_ps_decorrelator.c src/decoders/modern/aac/sauti_aac_ps_decorrelate.c src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) $(AAC_PS_MIX_SRC) $(AAC_PS_MIX_ASM) $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_he_ps_bridge.c src/decoders/modern/aac/sauti_aac_he_ps_bridge.c src/decoders/modern/aac/sauti_aac_ps_render.c src/decoders/modern/aac/sauti_aac_ps_map.c src/decoders/modern/aac/sauti_aac_ps_matrix.c src/decoders/modern/aac/sauti_aac_ps_phase.c src/decoders/modern/aac/sauti_aac_ps_pcm_stereo.c src/decoders/modern/aac/sauti_aac_ps_stereo.c src/decoders/modern/aac/sauti_aac_ps_hybrid_synth.c src/decoders/modern/aac/sauti_aac_ps_qmf_bank.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c src/decoders/modern/aac/sauti_aac_ps_qmf_high.c src/decoders/modern/aac/sauti_aac_ps_decorrelator.c src/decoders/modern/aac/sauti_aac_ps_decorrelate.c src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) $(AAC_PS_MIX_SRC) $(AAC_PS_MIX_ASM) $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) -lm -no-pie
$(OBJDIR)/test_aac_he_ps_delay_san: include/sauti_aac_he_ps_bridge.h include/sauti_aac_ps_render.h include/sauti_aac_ps_pcm_stereo.h tests/test_aac_he_ps_delay.c include/sauti_aac_he_ps_delay.h src/decoders/modern/aac/sauti_aac_he_ps_delay.c src/decoders/modern/aac/sauti_aac_he_ps_bridge.c src/decoders/modern/aac/sauti_aac_ps_render.c src/decoders/modern/aac/sauti_aac_ps_map.c src/decoders/modern/aac/sauti_aac_ps_matrix.c src/decoders/modern/aac/sauti_aac_ps_phase.c src/decoders/modern/aac/sauti_aac_ps_pcm_stereo.c src/decoders/modern/aac/sauti_aac_ps_stereo.c src/decoders/modern/aac/sauti_aac_ps_hybrid_synth.c src/decoders/modern/aac/sauti_aac_ps_qmf_bank.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c src/decoders/modern/aac/sauti_aac_ps_qmf_high.c src/decoders/modern/aac/sauti_aac_ps_decorrelator.c src/decoders/modern/aac/sauti_aac_ps_decorrelate.c src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) $(AAC_PS_MIX_SRC) $(AAC_PS_MIX_ASM) $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_he_ps_delay.c src/decoders/modern/aac/sauti_aac_he_ps_delay.c src/decoders/modern/aac/sauti_aac_he_ps_bridge.c src/decoders/modern/aac/sauti_aac_ps_render.c src/decoders/modern/aac/sauti_aac_ps_map.c src/decoders/modern/aac/sauti_aac_ps_matrix.c src/decoders/modern/aac/sauti_aac_ps_phase.c src/decoders/modern/aac/sauti_aac_ps_pcm_stereo.c src/decoders/modern/aac/sauti_aac_ps_stereo.c src/decoders/modern/aac/sauti_aac_ps_hybrid_synth.c src/decoders/modern/aac/sauti_aac_ps_qmf_bank.c src/decoders/modern/aac/sauti_aac_ps_qmf_low.c src/decoders/modern/aac/sauti_aac_ps_qmf_high.c src/decoders/modern/aac/sauti_aac_ps_decorrelator.c src/decoders/modern/aac/sauti_aac_ps_decorrelate.c src/decoders/modern/aac/sauti_aac_ps_transient.c $(AAC_PS_HYBRID_SRC) $(AAC_PS_MIX_SRC) $(AAC_PS_MIX_ASM) $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) -lm -no-pie
$(OBJDIR)/test_aac_he_adts_ps_san: tests/test_aac_he_adts_ps.c tests/test_aac_sbr_adts_mono.c include/sauti_aac_he_adts_ps.h include/sauti_aac_he_ps_delay.h $(AAC_HE_ADTS_PS_LINK) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_he_adts_ps.c $(AAC_HE_ADTS_PS_LINK) -lm -no-pie



$(OBJDIR)/test_aac_ps_matrix_san: tests/test_aac_ps_matrix.c include/sauti_aac_ps_matrix.h $(AAC_PS_MATRIX_SRC) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_ps_matrix.c $(AAC_PS_MATRIX_SRC) -lm

$(OBJDIR)/test_aac_ps_phase_san: tests/test_aac_ps_phase.c include/sauti_aac_ps_phase.h $(AAC_PS_PHASE_SRC) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_ps_phase.c $(AAC_PS_PHASE_SRC) -lm

$(OBJDIR)/test_aac_ps_mix_san: tests/test_aac_ps_mix.c include/sauti_aac_ps_mix.h $(AAC_PS_MIX_SRC) $(AAC_PS_MIX_ASM) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_ps_mix.c $(AAC_PS_MIX_SRC) $(AAC_PS_MIX_ASM) -lm -no-pie

$(OBJDIR)/test_aac_ps_render_san: tests/test_aac_ps_render.c include/sauti_aac_ps_render.h $(AAC_PS_MAP_SRC) $(AAC_PS_HYBRID_SRC) $(AAC_PS_MATRIX_SRC) $(AAC_PS_PHASE_SRC) $(AAC_PS_RENDER_SRC) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_ps_render.c $(AAC_PS_MAP_SRC) $(AAC_PS_HYBRID_SRC) $(AAC_PS_MATRIX_SRC) $(AAC_PS_PHASE_SRC) $(AAC_PS_RENDER_SRC) -lm

$(OBJDIR)/test_aac_sbr_payload_san: tests/test_aac_sbr_payload.c include/sauti_aac_sbr_payload.h $(AAC_SBR_PAYLOAD_DEPS) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_sbr_payload.c $(AAC_SBR_PAYLOAD_DEPS) -lm -no-pie

$(OBJDIR)/test_aac_sbr_qmf_export_san: tests/test_aac_sbr_qmf_export.c include/sauti_aac_sbr_qmf_frame.h $(AAC_SBR_QMF_FRAME_DEPS) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_sbr_qmf_export.c $(AAC_SBR_QMF_FRAME_DEPS) -lm

.PHONY: test-aac-sanitize
test-aac-sanitize: $(OBJDIR)/test_aac_cpe_san $(OBJDIR)/test_aac_syntax_san $(OBJDIR)/test_aac_ps_map_san $(OBJDIR)/test_aac_ps_hybrid_san $(OBJDIR)/test_aac_ps_qmf_high_san $(OBJDIR)/test_aac_ps_qmf_low_san $(OBJDIR)/test_aac_ps_qmf_bank_san $(OBJDIR)/test_aac_ps_qmf_reference_san $(OBJDIR)/test_aac_ps_transient_san $(OBJDIR)/test_aac_ps_decorrelate_san $(OBJDIR)/test_aac_ps_decorrelator_san $(OBJDIR)/test_aac_ps_stereo_san $(OBJDIR)/test_aac_ps_hybrid_synth_san $(OBJDIR)/test_aac_ps_pcm_stereo_san $(OBJDIR)/test_aac_he_ps_bridge_san $(OBJDIR)/test_aac_he_ps_delay_san $(OBJDIR)/test_aac_he_adts_ps_san $(OBJDIR)/test_aac_ps_matrix_san $(OBJDIR)/test_aac_ps_phase_san $(OBJDIR)/test_aac_ps_mix_san $(OBJDIR)/test_aac_ps_render_san $(OBJDIR)/test_aac_sbr_payload_san $(OBJDIR)/test_aac_sbr_qmf_export_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_cpe_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_syntax_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_ps_map_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_ps_hybrid_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_ps_qmf_high_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_ps_qmf_low_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_ps_qmf_bank_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_ps_qmf_reference_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_ps_transient_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_ps_decorrelate_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_ps_decorrelator_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_ps_stereo_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_ps_hybrid_synth_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_ps_pcm_stereo_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_he_ps_bridge_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_he_ps_delay_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_he_adts_ps_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_ps_matrix_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_ps_phase_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_ps_mix_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_ps_render_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_sbr_payload_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_sbr_qmf_export_san

.PHONY: test-aac-cpe
test-aac-cpe: $(OBJDIR)/test_aac_cpe
	./$(OBJDIR)/test_aac_cpe

$(OBJDIR)/test_aac_real_stereo: tests/test_aac_real_stereo.c include/sauti_aac_cpe.h $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_real_stereo.c $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) -lm -no-pie

# Diagnostic-only stereo experiment: never used in the player or strict gate.
$(OBJDIR)/test_aac_real_stereo_pns_independent: tests/test_aac_real_stereo.c include/sauti_aac_cpe.h $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -DSAUTI_AAC_DIAG_INDEPENDENT_PNS -Iinclude -o $@ tests/test_aac_real_stereo.c $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) -lm -no-pie


# Diagnostic-only PNS ablations: do not affect default decoder or gate.
$(OBJDIR)/test_aac_real_stereo_pns_positive: tests/test_aac_real_stereo.c include/sauti_aac_cpe.h $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -DSAUTI_AAC_DIAG_PNS_POSITIVE -Iinclude -o $@ tests/test_aac_real_stereo.c $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) -lm -no-pie

$(OBJDIR)/test_aac_real_stereo_pns_zero: tests/test_aac_real_stereo.c include/sauti_aac_cpe.h $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -DSAUTI_AAC_DIAG_PNS_ZERO -Iinclude -o $@ tests/test_aac_real_stereo.c $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) -lm -no-pie


# Diagnostic-only: quantify KBD/window-shape contribution; not used by player.
$(OBJDIR)/test_aac_real_stereo_windows_sine: tests/test_aac_real_stereo.c include/sauti_aac_cpe.h $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -DSAUTI_AAC_DIAG_FORCE_SINE_WINDOWS -Iinclude -o $@ tests/test_aac_real_stereo.c $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) -lm -no-pie

# Diagnostic-only first-short rising-window shape experiment.
$(OBJDIR)/test_aac_real_stereo_short_first_current: tests/test_aac_real_stereo.c include/sauti_aac_cpe.h $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -DSAUTI_AAC_DIAG_SHORT_FIRST_CURRENT -Iinclude -o $@ tests/test_aac_real_stereo.c $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) -lm -no-pie

# Diagnostic-only exact cosine IMDCT for 128-sample AAC windows.
$(OBJDIR)/test_aac_real_stereo_short_direct_imdct: tests/test_aac_real_stereo.c include/sauti_aac_cpe.h $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -DSAUTI_AAC_DIAG_SHORT_DIRECT_IMDCT -Iinclude -o $@ tests/test_aac_real_stereo.c $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) -lm -no-pie

# Non-production short-window stereo tool ablation binaries.
$(OBJDIR)/test_aac_real_stereo_short_no_ms: tests/test_aac_real_stereo.c include/sauti_aac_cpe.h $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -DSAUTI_AAC_DIAG_SHORT_NO_MS -Iinclude -o $@ tests/test_aac_real_stereo.c $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) -lm -no-pie

$(OBJDIR)/test_aac_real_stereo_short_no_intensity: tests/test_aac_real_stereo.c include/sauti_aac_cpe.h $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -DSAUTI_AAC_DIAG_SHORT_NO_INTENSITY -Iinclude -o $@ tests/test_aac_real_stereo.c $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) -lm -no-pie


.PHONY: inspect-aac-stereo
inspect-aac-stereo: $(OBJDIR)/test_aac_real_stereo
	@if command -v ffmpeg >/dev/null 2>&1; then \
	  mkdir -p build/audio-fixtures/aac-26; \
	  ffmpeg -hide_banner -nostdin -loglevel error -y \
	    -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=0.45" \
	    -f lavfi -i "sine=frequency=880:sample_rate=44100:duration=0.45" \
	    -filter_complex "[0:a][1:a]amerge=inputs=2[a]" -map "[a]" \
	    -ac 2 -c:a aac -b:a 192k -f adts \
	    build/audio-fixtures/aac-26/native_stereo.aac && \
	  ./$(OBJDIR)/test_aac_real_stereo \
	    build/audio-fixtures/aac-26/native_stereo.aac \
	    build/audio-fixtures/aac-26/native_stereo.f32 \
	    build/audio-fixtures/aac-26/native_stereo.mask && \
	  ffmpeg -hide_banner -nostdin -loglevel error -y \
	    -i build/audio-fixtures/aac-26/native_stereo.aac \
	    -ac 2 -ar 44100 -f f32le \
	    build/audio-fixtures/aac-26/reference_stereo.f32 && \
	  python3 tools/compare_aac_stereo_pcm.py \
	    build/audio-fixtures/aac-26/native_stereo.f32 \
	    build/audio-fixtures/aac-26/reference_stereo.f32 \
	    build/audio-fixtures/aac-26/native_stereo.mask; \
	else echo "SKIP AAC-26 real stereo: ffmpeg CLI unavailable"; fi

# AAC-29: strict diagnostics are separate from the earlier exploratory
# comparators. No FFmpeg is needed for verifier unit tests.
.PHONY: test-aac-stereo-strict
test-aac-stereo-strict:
	python3 tests/test_aac_stereo_strict.py

# Fast three-case smoke test; full matrix covers 3 materials x 3 rates x
# 4 bitrates = 36 cases. Both exit nonzero on missing/incorrect PCM.
.PHONY: inspect-aac-29-smoke inspect-aac-29-full
inspect-aac-29-smoke: $(OBJDIR)/test_aac_real_stereo test-aac-stereo-strict
	python3 scripts/inspect_aac_stereo_matrix.py --mode smoke

inspect-aac-29-full: $(OBJDIR)/test_aac_real_stereo test-aac-stereo-strict
	python3 scripts/inspect_aac_stereo_matrix.py --mode full

# Control only: does NOT replace the default PNS-enabled 36-case matrix.
.PHONY: inspect-aac-29-no-pns
inspect-aac-29-no-pns: $(OBJDIR)/test_aac_real_stereo test-aac-stereo-strict
	python3 scripts/inspect_aac_stereo_matrix.py --mode full --disable-pns

.PHONY: inspect-aac-29-tools
inspect-aac-29-tools: $(OBJDIR)/test_aac_real_stereo
	python3 scripts/diagnose_aac_stereo_tools.py

AAC_NATIVE_ADAPTER_SRC := src/decoders/modern/aac/sauti_decoder_aac_native.c
$(OBJDIR)/test_decoder_aac_native: tests/test_decoder_aac_native.c include/sauti_decoder_aac_native.h $(AAC_NATIVE_ADAPTER_DEPS) src/sauti_decoder.c | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_decoder_aac_native.c src/sauti_decoder.c $(AAC_NATIVE_ADAPTER_DEPS) -lm -no-pie


# SBR-88: mutation fuzzer for the native AAC adapters under ASan/UBSan (tool, not a product): make fuzz-aac FUZZ_SEEDS="a.m4a b.aac" [FUZZ_ITER=2000 FUZZ_SEED=1]
FUZZ_ITER ?= 2000
FUZZ_SEED ?= 1
$(OBJDIR)/sauti_aac_fuzz: tools/sauti_aac_fuzz.c include/sauti_decoder_aac_native.h $(AAC_NATIVE_ADAPTER_DEPS) src/sauti_decoder.c | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iinclude -o $@ tools/sauti_aac_fuzz.c src/sauti_decoder.c $(AAC_NATIVE_ADAPTER_DEPS) -lm -no-pie

.PHONY: fuzz-aac
fuzz-aac: $(OBJDIR)/sauti_aac_fuzz
	@test -n "$(FUZZ_SEEDS)" || { echo "FUZZ_SEEDS=<files> required"; exit 2; }
	$(OBJDIR)/sauti_aac_fuzz $(FUZZ_ITER) $(FUZZ_SEED) $(FUZZ_SEEDS)

# SBR-86: sequential read vs many seeks through the native adapters (tool, not a product): make build/sauti_aac_seek_check
$(OBJDIR)/sauti_aac_seek_check: tools/sauti_aac_seek_check.c include/sauti_decoder_aac_native.h $(AAC_NATIVE_ADAPTER_DEPS) src/sauti_decoder.c | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tools/sauti_aac_seek_check.c src/sauti_decoder.c $(AAC_NATIVE_ADAPTER_DEPS) -lm -no-pie

# Narzedzie macierzy korpusu AAC (tools/aac_corpus_matrix.py): adaptery natywne -> f32 PCM; nie jest czescia produktu ani testow.
$(OBJDIR)/sauti_aac_gen: tools/sauti_aac_gen.c $(AAC_NATIVE_ADAPTER_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tools/sauti_aac_gen.c src/sauti_decoder.c $(AAC_NATIVE_ADAPTER_DEPS) -lm -no-pie
# USAC (xHE-AAC) stream generator: the repository's own encoder-side counterpart of sauti_usac.c (arithmetic encoder, FD syntax writer); test tool, not a product.
$(OBJDIR)/sauti_usac_gen: tools/sauti_usac_gen.c src/decoders/modern/aac/sauti_usac_ac.c include/sauti_usac_ac.h include/sauti_usac_tables.h $(AAC_NATIVE_ADAPTER_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tools/sauti_usac_gen.c src/sauti_decoder.c $(AAC_NATIVE_ADAPTER_DEPS) -lm -no-pie
$(OBJDIR)/sauti_er_cli: tools/sauti_er_cli.c include/sauti_aac_er.h $(AAC_NATIVE_ADAPTER_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tools/sauti_er_cli.c src/sauti_decoder.c $(AAC_NATIVE_ADAPTER_DEPS) -lm -no-pie
$(OBJDIR)/sauti_aac_adapter_cli: tools/sauti_aac_adapter_cli.c include/sauti_decoder_aac_native.h $(AAC_NATIVE_ADAPTER_DEPS) src/sauti_decoder.c | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tools/sauti_aac_adapter_cli.c src/sauti_decoder.c $(AAC_NATIVE_ADAPTER_DEPS) -lm -no-pie

.PHONY: aac-adapter-cli
aac-adapter-cli: $(OBJDIR)/sauti_aac_adapter_cli

# Tool pulse AAC-LC: test jednostkowy + ROZNICOWY z FFmpeg (wlasnorecznie zbudowane strumienie ADTS z pulse_data, tools/gen_aac_pulse_fixture.c).
$(OBJDIR)/test_aac_pulse: tests/test_aac_pulse.c include/sauti_aac_lc_frame.h $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_pulse.c $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) -lm -no-pie

$(OBJDIR)/gen_aac_pulse_fixture: tools/gen_aac_pulse_fixture.c $(AAC_BANDS_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(OBJDIR)/sauti_aac_huffman.o | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tools/gen_aac_pulse_fixture.c $(AAC_BANDS_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(OBJDIR)/sauti_aac_huffman.o -lm -no-pie

.PHONY: test-aac-pulse
test-aac-pulse: $(OBJDIR)/test_aac_pulse
	./$(OBJDIR)/test_aac_pulse

.PHONY: test-aac-pulse-diff
test-aac-pulse-diff: test-aac-pulse $(OBJDIR)/gen_aac_pulse_fixture $(OBJDIR)/sauti_aac_adapter_cli
	@if ! command -v ffmpeg >/dev/null 2>&1; then echo "SKIP AAC pulse diff: ffmpeg CLI unavailable"; exit 0; fi; \
	mkdir -p $(OBJDIR)/audio-fixtures/pulse && \
	./$(OBJDIR)/gen_aac_pulse_fixture mono $(OBJDIR)/audio-fixtures/pulse/pulse_mono.aac && \
	./$(OBJDIR)/gen_aac_pulse_fixture stereo $(OBJDIR)/audio-fixtures/pulse/pulse_stereo.aac && \
	python3 tools/aac_corpus_matrix.py --out $(OBJDIR)/audio-fixtures/pulse --file $(OBJDIR)/audio-fixtures/pulse/pulse_mono.aac --file $(OBJDIR)/audio-fixtures/pulse/pulse_stereo.aac --require-ok

# AAC-LC wielokanalowy: test jednostkowy (hermetyczny) + ROZNICOWY z FFmpeg (3.0/4.0/5.0/5.1/7.1 kodowane przez ffmpeg `aac`, per-kanalowy SNR).
$(OBJDIR)/test_aac_lc_multi: tests/test_aac_lc_multi.c include/sauti_aac_lc_multi.h $(AAC_LC_MULTI_SRC) $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_lc_multi.c $(AAC_LC_MULTI_SRC) $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) -lm -no-pie

.PHONY: test-aac-lc-multi test-aac-multi-diff
test-aac-lc-multi: $(OBJDIR)/test_aac_lc_multi
	./$(OBJDIR)/test_aac_lc_multi

test-aac-multi-diff: test-aac-lc-multi $(OBJDIR)/sauti_aac_adapter_cli
	@if ! command -v ffmpeg >/dev/null 2>&1; then echo "SKIP AAC multi diff: ffmpeg CLI unavailable"; exit 0; fi; \
	mkdir -p $(OBJDIR)/audio-fixtures/multi && \
	python3 tools/aac_corpus_matrix.py --out $(OBJDIR)/audio-fixtures/multi --channels 3,4,5,6,8 --rates 44100 --seconds 2 --signals music,transient --filter ffmpeg-aac-fast --require-ok

# AAC w MP4/M4A: test jednostkowy (pliki skladane w pamieci, ASan) + ROZNICOWY z FFmpeg na plikach MP4/M4A z argumentu FILES (np. FILES="a.m4a b.mp4"); bez ffmpeg albo plikow: pomijany.
$(OBJDIR)/test_aac_latm: tests/test_aac_latm.c include/sauti_aac_latm.h include/sauti_decoder_aac_native.h src/sauti_decoder.c $(AAC_NATIVE_ADAPTER_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iinclude -o $@ tests/test_aac_latm.c src/sauti_decoder.c $(AAC_NATIVE_ADAPTER_DEPS) -lm -no-pie

.PHONY: test-aac-latm
test-aac-latm: $(OBJDIR)/test_aac_latm
	./$(OBJDIR)/test_aac_latm

$(OBJDIR)/test_aac_adif: tests/test_aac_adif.c include/sauti_aac_adif.h include/sauti_decoder_aac_native.h src/sauti_decoder.c $(AAC_NATIVE_ADAPTER_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iinclude -o $@ tests/test_aac_adif.c src/sauti_decoder.c $(AAC_NATIVE_ADAPTER_DEPS) -lm -no-pie

.PHONY: test-aac-adif
test-aac-adif: $(OBJDIR)/test_aac_adif
	./$(OBJDIR)/test_aac_adif

# re-entrancy: several decoders at once in several threads (ThreadSanitizer); run through scripts/test_aac_threads.sh (needs sample streams)
$(OBJDIR)/test_aac_threads_tsan: tests/test_aac_threads.c include/sauti_decoder_aac_native.h src/sauti_decoder.c $(AAC_NATIVE_ADAPTER_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=thread -Iinclude -o $@ tests/test_aac_threads.c src/sauti_decoder.c $(AAC_NATIVE_ADAPTER_DEPS) -lm -lpthread -no-pie

$(OBJDIR)/test_aac_threads_asan: tests/test_aac_threads.c include/sauti_decoder_aac_native.h src/sauti_decoder.c $(AAC_NATIVE_ADAPTER_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iinclude -o $@ tests/test_aac_threads.c src/sauti_decoder.c $(AAC_NATIVE_ADAPTER_DEPS) -lm -lpthread -no-pie

$(OBJDIR)/test_aac_mp4: tests/test_aac_mp4.c include/sauti_aac_mp4.h include/sauti_decoder_aac_native.h src/sauti_decoder.c $(AAC_NATIVE_ADAPTER_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iinclude -o $@ tests/test_aac_mp4.c src/sauti_decoder.c $(AAC_NATIVE_ADAPTER_DEPS) -lm -no-pie

.PHONY: test-aac-mp4 test-aac-mp4-diff
test-aac-mp4: $(OBJDIR)/test_aac_mp4
	./$(OBJDIR)/test_aac_mp4

test-aac-mp4-diff: test-aac-mp4 $(OBJDIR)/sauti_aac_adapter_cli
	@if [ -z "$(FILES)" ] || ! command -v ffmpeg >/dev/null 2>&1; then echo "SKIP AAC MP4 diff: podaj FILES=\"a.m4a b.mp4\" i ffmpeg"; exit 0; fi; \
	mkdir -p $(OBJDIR)/audio-fixtures/mp4 && python3 tools/aac_corpus_matrix.py --out $(OBJDIR)/audio-fixtures/mp4 $(foreach f,$(FILES),--file "$(f)") --require-ok

.PHONY: test-aac-native-adapter
test-aac-native-adapter: $(OBJDIR)/test_decoder_aac_native
	./$(OBJDIR)/test_decoder_aac_native

# Runs all native AAC modules without requiring libav*/FFmpeg.
.PHONY: test-aac-sbr-hf-frame-audit
test-aac-sbr-hf-frame-audit:
	python3 -m py_compile tools/sauti_sbr61_hf_frame_audit.py
	python3 tools/sauti_sbr61_hf_frame_audit.py --self-test

.PHONY: test-aac-sbr-ffmpeg-phase-trace
test-aac-sbr-ffmpeg-phase-trace:
	python3 -m py_compile tools/sauti_sbr73_ffmpeg_phase_trace.py tools/sauti_sbr73_phase_trace_audit.py
	python3 tools/sauti_sbr73_ffmpeg_phase_trace.py --self-test
	python3 tools/sauti_sbr73_phase_trace_audit.py --self-test

.PHONY: test-aac-sbr-hf-timing-control
test-aac-sbr-hf-timing-control:
	python3 -m py_compile tools/sauti_sbr72_hf_timing_control.py
	python3 tools/sauti_sbr72_hf_timing_control.py --self-test

.PHONY: test-aac-sbr-qmf-phase-frames
test-aac-sbr-qmf-phase-frames:
	python3 -m py_compile tools/sauti_sbr71_phase_frame_audit.py
	python3 tools/sauti_sbr71_phase_frame_audit.py --self-test

.PHONY: test-aac-sbr-qmf-phase-ab
test-aac-sbr-qmf-phase-ab:
	python3 -m py_compile tools/sauti_sbr70_qmf_phase_ab.py
	python3 tools/sauti_sbr70_qmf_phase_ab.py --self-test

.PHONY: test-aac-sbr-tonal-lines-audit
test-aac-sbr-tonal-lines-audit:
	python3 -m py_compile tools/sauti_sbr68_target_lines_ab.py
	python3 tools/sauti_sbr68_target_lines_ab.py --self-test

.PHONY: test-aac-sbr-tonal-delta-audit
test-aac-sbr-tonal-delta-audit:
	python3 -m py_compile tools/sauti_sbr67_tonal_delta_ab.py
	python3 tools/sauti_sbr67_tonal_delta_ab.py --self-test

.PHONY: test-aac-sbr-tonal-ab-audit
test-aac-sbr-tonal-ab-audit:
	python3 -m py_compile tools/sauti_sbr66_tonal_ab_audit.py
	python3 tools/sauti_sbr66_tonal_ab_audit.py --self-test

.PHONY: test-aac-sbr-hf-peak-audit
test-aac-sbr-hf-peak-audit:
	python3 -m py_compile tools/sauti_sbr65_hf_peak_audit.py
	python3 tools/sauti_sbr65_hf_peak_audit.py --self-test

.PHONY: test-aac-sbr-hf-edge-audit
test-aac-sbr-hf-edge-audit:
	python3 -m py_compile tools/sauti_sbr64_hf_edge_audit.py
	python3 tools/sauti_sbr64_hf_edge_audit.py --self-test

.PHONY: test-aac-sbr-hf-spectral-audit
test-aac-sbr-hf-spectral-audit:
	python3 -m py_compile tools/sauti_sbr63_hf_spectral_audit.py
	python3 tools/sauti_sbr63_hf_spectral_audit.py --self-test

.PHONY: test-aac-sbr-hf-stem-audit
test-aac-sbr-hf-stem-audit:
	python3 -m py_compile tools/sauti_sbr62_hf_stem_audit.py
	python3 tools/sauti_sbr62_hf_stem_audit.py --self-test

.PHONY: test-aac-native
test-aac-native: test-aac-pulse test-aac-lc-multi test-aac-mp4 test-aac-latm test-aac-adif test-aac-qmf-migration test-aac-qmf-dispatch test-aac-qmf-simd test-aac-qmf-variants test-aac-sbr-ffmpeg-phase-trace test-aac-sbr-hf-timing-control test-aac-sbr-qmf-phase-frames test-aac-sbr-qmf-phase-ab test-aac-sbr-harmonic-phase2-ab test-aac-sbr-tonal-lines-audit test-aac-sbr-tonal-delta-audit test-aac-sbr-tonal-ab-audit test-aac-sbr-hf-peak-audit test-aac-sbr-hf-edge-audit test-aac-sbr-hf-spectral-audit test-aac-sbr-hf-frame-audit test-aac-sbr-hf-stem-audit test-aac-headers test-aac-syntax test-aac-ps-map test-aac-ps-hybrid test-aac-ps-qmf-high test-aac-ps-qmf-low test-aac-ps-qmf-bank test-aac-ps-qmf-reference test-aac-ps-transient test-aac-ps-decorrelate test-aac-ps-decorrelator test-aac-ps-stereo test-aac-ps-hybrid-synth test-aac-ps-pcm-stereo test-aac-he-ps-bridge test-aac-he-ps-delay test-aac-he-adts-ps test-aac-ps5-native test-aac-ps-matrix test-aac-ps-phase test-aac-ps-mix test-aac-ps-render test-aac-adts-crc test-aac-sbr-grid test-aac-sbr-master test-aac-sbr-master-log test-aac-sbr-limits test-aac-sbr-tables test-aac-sbr-control test-aac-sbr-huffman test-aac-sbr-quant test-aac-sbr-energy test-aac-sbr-coupled test-aac-sbr-patch test-aac-sbr-hf test-aac-sbr-hf-predict test-aac-sbr-lpc test-aac-sbr-lpc-estimate test-aac-sbr-hf-adjust test-aac-sbr-hf-limiter test-aac-sbr-limiter-bands test-aac-sbr-hf-noise test-aac-sbr-hf-sine test-aac-sbr-hf-joint test-aac-sbr-smooth test-aac-sbr-hf-joint-smooth test-aac-sbr-noise-rom test-aac-sbr-hf-joint-rom test-aac-sbr-qmf-synth-window test-aac-sbr-qmf-synth-mod test-aac-sbr-qmf-analysis test-aac-sbr-qmf-frame test-aac-sbr-qmf-hf-export test-aac-sbr-qmf-export test-aac-sbr-temporal-energy-units test-aac-sbr-payload test-aac-sbr-adts-mono test-aac-sbr-adts-qmf test-aac-sbr-adts-cpe test-aac-he-probe-smoke test-aac-lc-sections test-aac-huffman test-aac-spectral test-aac-tables test-aac-scalefactors test-aac-spectral-data test-aac-dequant test-aac-imdct test-aac-window test-aac-lc-synth test-aac-lc-frame test-aac-pns test-aac-tns test-aac-cpe test-aac-stereo-strict test-aac-native-adapter test-aac-sbr-gain-limit-partition test-aac-sbr-gain-partition test-aac-sbr-hf-band-audit test-aac-sbr-hf-patch-route-audit test-aac-sbr-low-source-audit test-aac-sbr-temporal-apply test-aac-sbr-temporal-energy-audit test-aac-sbr-temporal-envelope-audit test-aac-sbr-temporal-gain test-aac-sbr-temporal-gain-bridge test-aac-sbr-temporal-harmonic-preview test-aac-sbr-temporal-hf-units test-aac-sbr-temporal-mix test-aac-sbr-temporal-noise test-aac-sbr-temporal-pcm-preview test-aac-sbr-temporal-power test-aac-sbr-temporal-qmf test-aac-sbr-temporal-reference-gain test-aac-sbr-temporal-smooth test-aac-sbr-temporal-source6 test-aac-sbr-qmf-convention test-aac-sbr-qmf-modulation test-aac-sbr-qmf-shared-input test-aac-ps-owned-geometry test-aac-qmf-frame-parity test-aac-tone

$(OBJDIR)/test_decoder_aiff: tests/test_decoder_aiff.c include/sauti_decoder_aiff.h include/sauti_decoder.h src/sauti_decoder.c $(AIFF_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_decoder_aiff.c src/sauti_decoder.c $(AIFF_SRC) -lm

$(OBJDIR)/test_decoder_ffmpeg: tests/test_decoder_ffmpeg.c include/sauti_decoder_ffmpeg.h include/sauti_decoder.h src/sauti_decoder.c $(FFMPEG_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) $(FFMPEG_FLAGS) -Iinclude -o $@ tests/test_decoder_ffmpeg.c src/sauti_decoder.c $(FFMPEG_SRC) $(FFMPEG_LIBS)

.PHONY: test-aiff test-ffmpeg test-container-formats
test-aiff: $(OBJDIR)/test_decoder_aiff
	./$(OBJDIR)/test_decoder_aiff
test-ffmpeg: $(OBJDIR)/test_decoder_ffmpeg
	./$(OBJDIR)/test_decoder_ffmpeg
test-container-formats: $(OBJDIR)/test_decoder_ffmpeg $(OBJDIR)/test_aac_syntax $(OBJDIR)/test_aac_lc_sections
	sh scripts/test_container_formats.sh

$(OBJDIR)/test_decoder_voc: tests/test_decoder_voc.c include/sauti_decoder_voc.h include/sauti_decoder.h src/sauti_decoder.c $(VOC_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_decoder_voc.c src/sauti_decoder.c $(VOC_SRC)

.PHONY: test-voc
test-voc: $(OBJDIR)/test_decoder_voc
	./$(OBJDIR)/test_decoder_voc

$(OBJDIR)/test_decoder_wav: tests/test_decoder_wav.c include/sauti_decoder.h include/sauti_decoder_wav.h src/sauti_decoder.c src/decoders/modern/sauti_decoder_wav.c | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_decoder_wav.c src/sauti_decoder.c src/decoders/modern/sauti_decoder_wav.c

$(OBJDIR)/test_decoder: tests/test_decoder.c include/sauti_decoder.h src/sauti_decoder.c | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_decoder.c src/sauti_decoder.c

$(OBJDIR)/test_playback_backend: tests/test_playback_backend.c include/sauti_playback_backend.h src/sauti_playback_backend.c | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_playback_backend.c src/sauti_playback_backend.c

$(OBJDIR)/test_playback_time: tests/test_playback_time.c include/sauti_playback_time.h src/sauti_playback_time.c | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_playback_time.c src/sauti_playback_time.c -lm

$(OBJDIR)/test_audio_backend: tests/test_audio_backend.c include/sauti_audio_backend.h src/sauti_audio_backend.c | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_audio_backend.c src/sauti_audio_backend.c

$(OBJDIR)/test_record_ring: tests/test_record_ring.c include/sauti_record_ring.h src/sauti_record_ring.c | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_record_ring.c src/sauti_record_ring.c

$(OBJDIR)/test_wav: tests/test_wav.c include/sauti_wav.h src/sauti_wav.c | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_wav.c src/sauti_wav.c -lm

$(OBJDIR)/test_library: tests/test_library.c include/sauti_library.h src/sauti_library.c | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_library.c src/sauti_library.c

$(OBJDIR)/test_app_state: tests/test_app_state.c include/sauti_app_state.h src/sauti_app_state.c | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_app_state.c src/sauti_app_state.c

$(OBJDIR)/test_transport: tests/test_transport.c include/sauti_transport.h src/sauti_transport.c | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_transport.c src/sauti_transport.c

$(OBJDIR)/test_playlist: tests/test_playlist.c include/sauti_playlist.h include/sauti_transport.h include/sauti_app_state.h include/sauti_library.h include/sauti_wav.h include/sauti_record_ring.h $(PORTABLE_SRC) $(AVR_ASM) $(DVSM_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) $(FFMPEG_FLAGS) $(OPUS_FLAGS) -Iinclude -o $@ tests/test_playlist.c $(PORTABLE_SRC) $(AVR_ASM) $(DVSM_ASM) $(OPUS_LIBS) $(FFMPEG_LIBS) -lm -no-pie

$(OBJDIR)/test_fft: tests/test_fft.c include/sauti.h $(ASMOBJ)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_fft.c $(ASMOBJ) -lm -no-pie

$(OBJDIR)/test_core: tests/test_core.c include/sauti.h $(ASMOBJ)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_core.c $(ASMOBJ) -lm -no-pie

$(OBJDIR)/test_db_log: tests/test_db_log.c include/sauti.h $(ASMOBJ)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_db_log.c $(ASMOBJ) -lm -no-pie

$(OBJDIR)/test_audio_ring: tests/test_audio_ring.c include/sauti.h $(ASMOBJ)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_audio_ring.c $(ASMOBJ) -lm -no-pie

$(OBJDIR)/test_stereo_ring: tests/test_stereo_ring.c include/sauti.h $(ASMOBJ)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_stereo_ring.c $(ASMOBJ) -lm -no-pie

$(OBJDIR)/test_stereo_analysis: tests/test_stereo_analysis.c include/sauti.h $(ASMOBJ)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_stereo_analysis.c $(ASMOBJ) -lm -no-pie

$(OBJDIR)/test_stereo_bars: tests/test_stereo_bars.c include/sauti.h $(ASMOBJ)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_stereo_bars.c $(ASMOBJ) -lm -no-pie

$(OBJDIR)/test_peak_hold: tests/test_peak_hold.c include/sauti.h $(ASMOBJ)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_peak_hold.c $(ASMOBJ) -lm -no-pie

$(OBJDIR)/test_auto_gain: tests/test_auto_gain.c include/sauti.h $(ASMOBJ)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_auto_gain.c $(ASMOBJ) -lm -no-pie

$(OBJDIR)/test_frame: tests/test_frame.c include/sauti.h $(ASMOBJ)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_frame.c $(ASMOBJ) -lm -no-pie

$(OBJDIR)/sauti_demo: tests/demo_ppm.c include/sauti.h $(ASMOBJ)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/demo_ppm.c $(ASMOBJ) -no-pie

$(OBJDIR)/sauti_window: third_party/dr_libs/dr_flac.h third_party/stb/stb_vorbis.c third_party/tml/tml.h src/decoders/modern/sauti_decoder_opus.c src/decoders/modern/sauti_decoder_vorbis.c src/decoders/modern/sauti_decoder_midi.c platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c platforms/linux/sauti_mp3_sdl_player.h include/sauti.h include/sauti_playlist.h include/sauti_audio_backend.h $(PORTABLE_SRC) $(AAC_PLAYER_SRC) $(ASMOBJ) $(filter-out $(AAC_PLAYER_SRC),$(AAC_PLAYER_ASM)) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(YM_ASM) $(YM_ARCHIVE_SRC) include/sauti_ym_archive.h $(SNDH_OBJECTS) $(WAVPACK_SRC)
	$(CC) $(CFLAGS) $(FFMPEG_FLAGS) $(OPUS_FLAGS) $(ARCHIVE_FLAGS) $(XMP_FLAGS) $(WAVPACK_FLAGS) $(SNDH_CFLAGS) -Iinclude -Iplatforms/linux $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_mixer) \
	  -o $@ platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c $(PORTABLE_SRC) $(AAC_PLAYER_SRC) $(ASMOBJ) $(filter-out $(AAC_PLAYER_SRC),$(AAC_PLAYER_ASM)) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(YM_ASM) $(YM_ARCHIVE_SRC) $(SNDH_OBJECTS) $(WAVPACK_SRC) \
	  $(shell $(PKG_CONFIG) --libs sdl2 SDL2_mixer) $(OPUS_LIBS) $(FFMPEG_LIBS) $(ARCHIVE_LIBS) $(XMP_LIBS) $(WAVPACK_LIBS) $(SNDH_LINK) -lm -no-pie

$(OBJDIR)/sauti_window_pw: third_party/dr_libs/dr_flac.h third_party/stb/stb_vorbis.c third_party/tml/tml.h src/decoders/modern/sauti_decoder_opus.c src/decoders/modern/sauti_decoder_vorbis.c src/decoders/modern/sauti_decoder_midi.c platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c platforms/linux/sauti_mp3_sdl_player.h platforms/linux/pipewire_capture.c platforms/linux/pipewire_capture.h include/sauti.h include/sauti_playlist.h include/sauti_audio_backend.h $(PORTABLE_SRC) $(AAC_PLAYER_SRC) $(ASMOBJ) $(filter-out $(AAC_PLAYER_SRC),$(AAC_PLAYER_ASM)) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(YM_ASM) $(YM_ARCHIVE_SRC) include/sauti_ym_archive.h $(SNDH_OBJECTS) $(WAVPACK_SRC)
	$(CC) $(CFLAGS) $(FFMPEG_FLAGS) $(OPUS_FLAGS) $(ARCHIVE_FLAGS) $(XMP_FLAGS) $(WAVPACK_FLAGS) $(SNDH_CFLAGS) -D_GNU_SOURCE -DSAUTI_HAS_PIPEWIRE -Iinclude -Iplatforms/linux \
	  $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_mixer libpipewire-0.3) -o $@ \
	  platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c platforms/linux/pipewire_capture.c $(PORTABLE_SRC) $(AAC_PLAYER_SRC) $(ASMOBJ) $(filter-out $(AAC_PLAYER_SRC),$(AAC_PLAYER_ASM)) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(YM_ASM) $(YM_ARCHIVE_SRC) $(SNDH_OBJECTS) $(WAVPACK_SRC) \
	  $(shell $(PKG_CONFIG) --libs sdl2 SDL2_mixer libpipewire-0.3) $(OPUS_LIBS) $(FFMPEG_LIBS) $(ARCHIVE_LIBS) $(XMP_LIBS) $(WAVPACK_LIBS) $(SNDH_LINK) -lm -no-pie

$(OBJDIR)/sauti_window_pw_reference: third_party/dr_libs/dr_flac.h third_party/stb/stb_vorbis.c third_party/tml/tml.h src/decoders/modern/sauti_decoder_opus.c src/decoders/modern/sauti_decoder_vorbis.c src/decoders/modern/sauti_decoder_midi.c platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c platforms/linux/sauti_mp3_sdl_player.h platforms/linux/pipewire_capture.c platforms/linux/pipewire_capture.h include/sauti.h include/sauti_playlist.h include/sauti_audio_backend.h $(PORTABLE_SRC) $(AAC_PLAYER_SRC) $(ASMOBJ) $(filter-out $(AAC_PLAYER_SRC),$(AAC_PLAYER_ASM)) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(OBJDIR)/sauti_decoder_ym_reference_dac.o $(YM_ARCHIVE_SRC) include/sauti_ym_archive.h
	$(CC) $(CFLAGS) $(FFMPEG_FLAGS) -DSAUTI_YM_LISTEN_REFERENCE $(OPUS_FLAGS) $(ARCHIVE_FLAGS) $(XMP_FLAGS) -D_GNU_SOURCE -DSAUTI_HAS_PIPEWIRE -Iinclude -Iplatforms/linux \
	  $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_mixer libpipewire-0.3) -o $@ \
	  platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c platforms/linux/pipewire_capture.c $(PORTABLE_SRC) $(AAC_PLAYER_SRC) $(ASMOBJ) $(filter-out $(AAC_PLAYER_SRC),$(AAC_PLAYER_ASM)) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(OBJDIR)/sauti_decoder_ym_reference_dac.o $(YM_ARCHIVE_SRC) \
	  $(shell $(PKG_CONFIG) --libs sdl2 SDL2_mixer libpipewire-0.3) $(OPUS_LIBS) $(FFMPEG_LIBS) $(ARCHIVE_LIBS) $(XMP_LIBS) -lm -no-pie

.PHONY: window-pipewire-reference
window-pipewire-reference: $(OBJDIR)/sauti_window_pw_reference

$(OBJDIR)/sauti_window_pw_pre_tick: third_party/dr_libs/dr_flac.h third_party/stb/stb_vorbis.c third_party/tml/tml.h src/decoders/modern/sauti_decoder_opus.c src/decoders/modern/sauti_decoder_vorbis.c src/decoders/modern/sauti_decoder_midi.c platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c platforms/linux/sauti_mp3_sdl_player.h platforms/linux/pipewire_capture.c platforms/linux/pipewire_capture.h include/sauti.h include/sauti_playlist.h include/sauti_audio_backend.h $(PORTABLE_SRC) $(AAC_PLAYER_SRC) $(ASMOBJ) $(filter-out $(AAC_PLAYER_SRC),$(AAC_PLAYER_ASM)) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(OBJDIR)/sauti_decoder_ym_env_pre_tick.o $(YM_ARCHIVE_SRC) include/sauti_ym_archive.h
	$(CC) $(CFLAGS) $(FFMPEG_FLAGS) -DSAUTI_YM_LISTEN_REFERENCE $(OPUS_FLAGS) $(ARCHIVE_FLAGS) $(XMP_FLAGS) -D_GNU_SOURCE -DSAUTI_HAS_PIPEWIRE -Iinclude -Iplatforms/linux \
	  $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_mixer libpipewire-0.3) -o $@ \
	  platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c platforms/linux/pipewire_capture.c $(PORTABLE_SRC) $(AAC_PLAYER_SRC) $(ASMOBJ) $(filter-out $(AAC_PLAYER_SRC),$(AAC_PLAYER_ASM)) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(OBJDIR)/sauti_decoder_ym_env_pre_tick.o $(YM_ARCHIVE_SRC) \
	  $(shell $(PKG_CONFIG) --libs sdl2 SDL2_mixer libpipewire-0.3) $(OPUS_LIBS) $(FFMPEG_LIBS) $(ARCHIVE_LIBS) $(XMP_LIBS) -lm -no-pie

.PHONY: window-pipewire-pre-tick
window-pipewire-pre-tick: $(OBJDIR)/sauti_window_pw_pre_tick

window: $(OBJDIR)/sauti_window
# Optional Qt6 preferences process; does not replace ASM or SDL audio engine.
QT6_WIDGETS_AVAILABLE := $(shell $(PKG_CONFIG) --exists Qt6Widgets && echo yes)
ifeq ($(QT6_WIDGETS_AVAILABLE),yes)
$(OBJDIR)/sauti_settings_qt: platforms/linux/sauti_settings_qt.cpp | $(OBJDIR)
	$(CXX) -O2 -std=c++17 -Wall -Wextra -Iinclude $(shell $(PKG_CONFIG) --cflags Qt6Widgets) -o $@ $< $(shell $(PKG_CONFIG) --libs Qt6Widgets)
.PHONY: settings-qt
settings-qt: $(OBJDIR)/sauti_settings_qt
else
.PHONY: settings-qt
settings-qt:
	@echo "Qt6Widgets development package is unavailable; install qt6-qtbase-devel."
endif

window-pipewire: $(OBJDIR)/sauti_window_pw
ifeq ($(QT6_WIDGETS_AVAILABLE),yes)
window-pipewire: $(OBJDIR)/sauti_settings_qt
endif

run-pipewire-mic: window-pipewire
	SDL_VIDEODRIVER=wayland ./$(OBJDIR)/sauti_window_pw --source mic

run-pipewire-monitor: window-pipewire
	SDL_VIDEODRIVER=wayland ./$(OBJDIR)/sauti_window_pw --source monitor

run-wayland: window
	SDL_VIDEODRIVER=wayland ./$(OBJDIR)/sauti_window

smoke-headless: window
	SDL_VIDEODRIVER=dummy ./$(OBJDIR)/sauti_window --frames 6

smoke-headless-pipewire: window-pipewire
	SDL_VIDEODRIVER=dummy ./$(OBJDIR)/sauti_window_pw --source generator --frames 6

test: $(OBJDIR)/test_decoder $(OBJDIR)/test_decoder_wav $(OBJDIR)/test_decoder_voc $(OBJDIR)/test_decoder_aiff $(OBJDIR)/test_decoder_ffmpeg $(OBJDIR)/test_aac_headers $(OBJDIR)/test_aac_syntax $(OBJDIR)/test_aac_lc_sections $(OBJDIR)/test_aac_huffman $(OBJDIR)/test_aac_spectral $(OBJDIR)/test_decoder_mp3 $(OBJDIR)/test_pcm_output $(OBJDIR)/test_decoder_vorbis $(OBJDIR)/test_playback_backend $(OBJDIR)/test_playback_time $(OBJDIR)/test_audio_backend $(OBJDIR)/test_record_ring $(OBJDIR)/test_wav $(OBJDIR)/test_library $(OBJDIR)/test_app_state $(OBJDIR)/test_transport $(OBJDIR)/test_playlist $(OBJDIR)/test_core $(OBJDIR)/test_fft $(OBJDIR)/test_frame $(OBJDIR)/test_db_log $(OBJDIR)/test_audio_ring $(OBJDIR)/test_stereo_ring $(OBJDIR)/test_stereo_analysis $(OBJDIR)/test_stereo_bars $(OBJDIR)/test_peak_hold $(OBJDIR)/test_auto_gain $(OBJDIR)/test_decoder_midi $(OBJDIR)/test_decoder_opus $(OBJDIR)/test_decoder_flac $(OBJDIR)/test_decoder_avr $(OBJDIR)/test_decoder_dvsm $(OBJDIR)/test_decoder_ym $(OBJDIR)/test_ym_archive
	./$(OBJDIR)/test_decoder
	./$(OBJDIR)/test_decoder_wav
	./$(OBJDIR)/test_decoder_voc
	./$(OBJDIR)/test_decoder_aiff
	./$(OBJDIR)/test_decoder_ffmpeg
	./$(OBJDIR)/test_aac_headers
	./$(OBJDIR)/test_aac_syntax
	./$(OBJDIR)/test_aac_lc_sections
	./$(OBJDIR)/test_aac_huffman
	./$(OBJDIR)/test_aac_spectral
	./$(OBJDIR)/test_decoder_avr
	./$(OBJDIR)/test_decoder_dvsm
	./$(OBJDIR)/test_decoder_ym
	./$(OBJDIR)/test_ym_archive
	./$(OBJDIR)/test_decoder_mp3
	./$(OBJDIR)/test_decoder_vorbis
	./$(OBJDIR)/test_decoder_flac
	./$(OBJDIR)/test_decoder_midi
	./$(OBJDIR)/test_decoder_opus
	./$(OBJDIR)/test_pcm_output
	./$(OBJDIR)/test_playback_backend
	./$(OBJDIR)/test_playback_time
	./$(OBJDIR)/test_audio_backend
	./$(OBJDIR)/test_record_ring
	./$(OBJDIR)/test_wav
	./$(OBJDIR)/test_library
	./$(OBJDIR)/test_app_state
	./$(OBJDIR)/test_transport
	./$(OBJDIR)/test_playlist
	./$(OBJDIR)/test_core
	./$(OBJDIR)/test_fft
	./$(OBJDIR)/test_frame
	./$(OBJDIR)/test_db_log
	./$(OBJDIR)/test_audio_ring
	./$(OBJDIR)/test_stereo_ring
	./$(OBJDIR)/test_stereo_analysis
	./$(OBJDIR)/test_stereo_bars
	./$(OBJDIR)/test_peak_hold
	./$(OBJDIR)/test_auto_gain

demo: $(OBJDIR)/sauti_demo
	./$(OBJDIR)/sauti_demo $(OBJDIR)/sauti-demo.ppm

clean:
	rm -rf $(OBJDIR)

# AAC-29: strict FFmpeg/native regression.  The extended 6s smoke
# checks uninterrupted frame coverage in addition to the same PCM gate.
# Separate output directories prevent stale 0.52s fixtures from passing.
.PHONY: test-aac-29-ci test-aac-29-long
test-aac-29-ci: $(OBJDIR)/test_aac_real_stereo test-aac-stereo-strict
	python3 scripts/inspect_aac_stereo_matrix.py --mode full

test-aac-29-long: $(OBJDIR)/test_aac_real_stereo test-aac-stereo-strict
	python3 scripts/inspect_aac_stereo_matrix.py --mode smoke --duration 6 --min-frames 170 --output build/audio-fixtures/aac-29-long

# SBR-54v: independent read-only LOW QMF source audit.
$(OBJDIR)/test_aac_sbr_low_source_audit: tests/test_aac_sbr_low_source_audit.c include/sauti_aac_sbr_low_source_audit.h src/decoders/modern/aac/sauti_aac_sbr_low_source_audit.c | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_low_source_audit.c src/decoders/modern/aac/sauti_aac_sbr_low_source_audit.c -lm

.PHONY: test-aac-sbr-low-source-audit
test-aac-sbr-low-source-audit: $(OBJDIR)/test_aac_sbr_low_source_audit
	./$(OBJDIR)/test_aac_sbr_low_source_audit

# SBR-54w: independent-reference comparison gate; synthetic test only.
.PHONY: test-aac-sbr-qmf-reference-audit
test-aac-sbr-qmf-reference-audit:
	python3 tools/sauti_he_aac_qmf_reference_audit.py --self-test

# SBR-54w II: export diagnostic LOW W[i-6] QMF from SHARED core PCM.
$(OBJDIR)/sauti_qmf_low_export: tools/sauti_qmf_low_export.c include/sauti_aac_sbr_qmf_analysis.h $(AAC_SBR_QMF_ANALYSIS_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tools/sauti_qmf_low_export.c $(AAC_SBR_QMF_ANALYSIS_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) -lm

.PHONY: export-aac-sbr-low-qmf test-aac-sbr-qmf-export
export-aac-sbr-low-qmf: $(OBJDIR)/sauti_qmf_low_export
	@echo "Usage: ./$(OBJDIR)/sauti_qmf_low_export CORE.f32le OUT.qmf.f32le"

test-aac-sbr-qmf-export: $(OBJDIR)/sauti_qmf_low_export
	python3 tools/test_sauti_qmf_low_export.py ./$(OBJDIR)/sauti_qmf_low_export

# SBR-54w III: fail-closed FFmpeg source instrumentation smoke-test.
.PHONY: test-aac-sbr-ffmpeg-qmf-instrument
test-aac-sbr-ffmpeg-qmf-instrument:
	python3 tools/sauti_ffmpeg_qmf_instrument.py --self-test

# SBR-54w: deterministic shared PCM for independent QMF comparison.
.PHONY: test-aac-sbr-qmf-shared-input
test-aac-sbr-qmf-shared-input:
	python3 -m py_compile tools/sauti_sbr54w_shared_pcm.py tools/sauti_sbr54w_verify_reference.py tools/sauti_ffmpeg_qmf_instrument.py tools/sauti_ffmpeg_qmf_manifest.py
	python3 tools/sauti_ffmpeg_qmf_instrument.py --self-test

# SBR-54x: four tone-driven independent QMF transfer experiments.
.PHONY: test-aac-sbr-qmf-tones
test-aac-sbr-qmf-tones: $(OBJDIR)/sauti_qmf_low_export
	python3 -m py_compile tools/sauti_sbr54x_tone_sweep.py
	python3 tools/sauti_sbr54x_tone_sweep.py

# SBR-54y: compare existing single-tone QMF exports by time window.
.PHONY: test-aac-sbr-qmf-steady
test-aac-sbr-qmf-steady:
	python3 -m py_compile tools/sauti_sbr54y_steady_audit.py
	python3 tools/sauti_sbr54y_steady_audit.py --self-test
	python3 tools/sauti_sbr54y_steady_audit.py

# SBR-54z: first 64 global QMF slots, calibrated against stable late-slot gain.
.PHONY: test-aac-sbr-qmf-startup-slots
test-aac-sbr-qmf-startup-slots:
	python3 -m py_compile tools/sauti_sbr54z_startup_slots.py
	python3 tools/sauti_sbr54z_startup_slots.py --self-test
	python3 tools/sauti_sbr54z_startup_slots.py

# SBR-55: static, fail-closed QMF table/order audit.  Real comparison needs
# an explicitly supplied disposable FFmpeg source checkout.
.PHONY: test-aac-sbr-qmf-convention
test-aac-sbr-qmf-convention:
	python3 -m py_compile tools/sauti_sbr55_qmf_convention_audit.py
	python3 tools/sauti_sbr55_qmf_convention_audit.py --self-test

# SBR-55: profile periodic residual after a fixed complex-gain calibration.
.PHONY: test-aac-sbr-qmf-modulation
test-aac-sbr-qmf-modulation:
	python3 -m py_compile tools/sauti_sbr55_modulation_audit.py
	python3 tools/sauti_sbr55_modulation_audit.py --self-test

# SBR-55: disposable FFmpeg stage trace and matching standalone Sauti export.
$(OBJDIR)/sauti_qmf_stage_export: tools/sauti_qmf_stage_export.c include/sauti_aac_sbr_qmf_synth_window.h $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tools/sauti_qmf_stage_export.c $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) -lm

.PHONY: test-aac-sbr-qmf-stage-trace export-aac-sbr-qmf-stages
test-aac-sbr-qmf-stage-trace:
	python3 -m py_compile tools/sauti_ffmpeg_qmf_stage_trace.py
	python3 tools/sauti_ffmpeg_qmf_stage_trace.py --self-test
	python3 -m py_compile tools/sauti_sbr55_stage_audit.py
	python3 tools/sauti_sbr55_stage_audit.py x x x x --self-test
	python3 -m py_compile tools/sauti_ffmpeg_qmf_branch0_trace.py tools/sauti_sbr55_branch0_audit.py
	python3 tools/sauti_ffmpeg_qmf_branch0_trace.py --self-test
	python3 tools/sauti_sbr55_branch0_audit.py x x --self-test
export-aac-sbr-qmf-stages: $(OBJDIR)/sauti_qmf_stage_export
	@echo "Usage: ./$(OBJDIR)/sauti_qmf_stage_export CORE.f32le BRANCH0.f32le PRE.f32le POST.f32le"

# SBR-57: trace the reference 64-band synthesis input/PCM and replay it
# through Sauti's standalone synthesis implementation.  The trace patcher is
# fail-closed and operates only on an explicitly supplied disposable FFmpeg.
$(OBJDIR)/sauti_qmf_synth_export: tools/sauti_qmf_synth_export.c include/sauti_aac_sbr_qmf_synth_mod.h include/sauti_aac_sbr_qmf_synth_window.h $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tools/sauti_qmf_synth_export.c $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) -lm

.PHONY: test-aac-sbr-qmf-synth-trace export-aac-sbr-qmf-synth
test-aac-sbr-qmf-synth-trace:
	python3 -m py_compile tools/sauti_ffmpeg_qmf_synth_trace.py tools/sauti_sbr57_synth_audit.py tools/sauti_sbr57_trace_select.py
	python3 tools/sauti_ffmpeg_qmf_synth_trace.py --self-test
	python3 tools/sauti_sbr57_synth_audit.py x x --self-test
	python3 tools/sauti_sbr57_trace_select.py --self-test
export-aac-sbr-qmf-synth: $(OBJDIR)/sauti_qmf_synth_export
	@echo "Usage: ./$(OBJDIR)/sauti_qmf_synth_export FF_X.f32le SAUTI_PCM.f32le 32768"

# SBR-58: compare just the QMF complex-to-real modulation before the
# prototype-window/history stage, against a disposable FFmpeg V trace.
$(OBJDIR)/sauti_qmf_synth_mod_export: tools/sauti_qmf_synth_mod_export.c include/sauti_aac_sbr_qmf_synth_mod.h include/sauti_aac_sbr_qmf_synth_window.h $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tools/sauti_qmf_synth_mod_export.c $(AAC_SBR_QMF_SYNTH_MOD_SRC) $(AAC_SBR_QMF_SYNTH_WINDOW_SRC) -lm

.PHONY: test-aac-sbr-qmf-synth-stage-trace export-aac-sbr-qmf-synth-mod
test-aac-sbr-qmf-synth-stage-trace:
	python3 -m py_compile tools/sauti_ffmpeg_qmf_synth_stage_trace.py
	python3 tools/sauti_ffmpeg_qmf_synth_stage_trace.py --self-test
export-aac-sbr-qmf-synth-mod: $(OBJDIR)/sauti_qmf_synth_mod_export
	@echo "Usage: ./$(OBJDIR)/sauti_qmf_synth_mod_export FF_X.f32le SAUTI_V.f32le 32768"

# SBR-54z: stereo-aware independent QMF analyzer instrumentation only.
.PHONY: test-aac-sbr-qmf-stereo-select
test-aac-sbr-qmf-stereo-select:
	python3 -m py_compile tools/sauti_ffmpeg_qmf_stereo_select.py
	python3 tools/sauti_ffmpeg_qmf_stereo_select.py --self-test

# SBR-54z: non-invasive trace of independent FFmpeg QMF state addresses.
.PHONY: test-aac-sbr-qmf-call-trace
test-aac-sbr-qmf-call-trace:
	python3 -m py_compile tools/sauti_ffmpeg_qmf_call_trace.py
	python3 tools/sauti_ffmpeg_qmf_call_trace.py --self-test

# SBR-54z: strict observed-context QMF selection for disposable FFmpeg only.
.PHONY: test-aac-sbr-qmf-context-select
test-aac-sbr-qmf-context-select:
	python3 -m py_compile tools/sauti_ffmpeg_qmf_context_select.py
	python3 tools/sauti_ffmpeg_qmf_context_select.py --self-test

# MaktabaSauti public headers: C99 pedantic + C++17, struct layout and ABI constants (docs/MAKTABASAUTI_API.md)
.PHONY: test-maktabasauti-headers
test-maktabasauti-headers:
	@mkdir -p $(OBJDIR)
	$(CC) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -o $(OBJDIR)/test_maktabasauti_headers tests/test_maktabasauti_headers.c
	./$(OBJDIR)/test_maktabasauti_headers
	$(CXX) -std=c++17 -pedantic -Wall -Wextra -Werror -Iinclude -o $(OBJDIR)/test_maktabasauti_headers_cpp tests/test_maktabasauti_headers_cpp.cpp
	./$(OBJDIR)/test_maktabasauti_headers_cpp

# ---- MaktabaSauti: rdzen (src/maktabasauti) + opakowania starszych dekoderow; docs/MAKTABASAUTI_API.md ----
MKS_CORE_SRC := $(filter-out src/maktabasauti/mks_nolegacy.c,$(wildcard src/maktabasauti/*.c))
MKS_LEGACY_SRC := src/decoders/retro/atari/maktabasauti_dvs.c src/decoders/retro/amiga/maktabasauti_svx.c src/decoders/retro/amiga/maktabasauti_mod.c src/decoders/modern/aac/maktabasauti_aac_engine.c src/decoders/modern/aac/maktabasauti_aac_adts.c src/decoders/modern/aac/maktabasauti_aac_loas.c src/decoders/modern/aac/maktabasauti_aac_adif.c src/decoders/modern/aac/maktabasauti_aac_mp4.c src/decoders/modern/maktabasauti_pcm.c src/decoders/modern/maktabasauti_flac.c src/decoders/retro/pc/maktabasauti_voc.c src/decoders/modern/sauti_decoder_wav.c src/decoders/retro/pc/sauti_decoder_voc.c src/decoders/modern/sauti_decoder_aiff.c src/decoders/modern/sauti_decoder_vorbis.c \
                  src/decoders/modern/sauti_decoder_midi.c src/decoders/modern/sauti_decoder_flac.c src/decoders/modern/sauti_decoder_mp3.c src/decoders/retro/atari/sauti_decoder_avr.c \
                  src/decoders/retro/atari/sauti_decoder_dvsm.c src/decoders/retro/atari/sauti_decoder_dtm.c src/decoders/retro/atari/sauti_decoder_ym.c src/sauti_decoder.c
MOD_ASM := $(OBJDIR)/maktabasauti_mod_mix.o
$(MOD_ASM): src/decoders/retro/amiga/maktabasauti_mod_mix.asm | $(OBJDIR)
	$(call ASM_OBJ,$<)
MKS_DEPS = $(sort $(MKS_CORE_SRC) $(MKS_LEGACY_SRC) $(AAC_PLAYER_SRC) $(AAC_PLAYER_ASM) $(AVR_ASM) $(DVSM_ASM) $(YM_ASM) $(MOD_ASM))
# biblioteki zewnetrzne wlaczane do zrodel przez #include: zmiana ich pliku musi przebudowac testy (make nie widzi tego z samej listy zrodel)
MKS_THIRD := $(wildcard third_party/minimp3/*.h third_party/stb/*.c third_party/dr_libs/*.h third_party/tml/*.h)
MKS_INC := -Iinclude -Isrc/maktabasauti -Ithird_party -Ithird_party/dr_libs -Ithird_party/stb -Ithird_party/tml

# rdzen musi byc C99 bez rozszerzen (poza mostem do starszych dekoderow, ktory wlacza ich naglowki)
.PHONY: check-maktabasauti-c99
check-maktabasauti-c99:
	@mkdir -p $(OBJDIR)
	@for f in $(filter-out src/maktabasauti/mks_legacy.c,$(MKS_CORE_SRC)); do $(CC) -std=c99 -pedantic -Wall -Wextra -Werror -O2 $(MKS_INC) -c $$f -o $(OBJDIR)/c99check.o || exit 1; done
	@$(CC) -std=c11 -Wall -Wextra -Werror -O2 $(MKS_INC) -c src/maktabasauti/mks_legacy.c -o $(OBJDIR)/c99check.o
	@echo "PASS MaktabaSauti core: C99 -pedantic (bridge to legacy decoders: C11)"

# testy rdzenia (ASan + UBSan)
$(OBJDIR)/test_maktabasauti_core: tests/test_maktabasauti_core.c $(MKS_DEPS) $(MKS_THIRD) include/maktabasauti/maktabasauti.h include/maktabasauti/maktabasauti_codec.h | $(OBJDIR)
	$(CC) $(CFLAGS) -DMKS_TEST_LEGACY -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tests/test_maktabasauti_core.c $(MKS_DEPS) -lm -lpthread -no-pie
$(OBJDIR)/test_maktabasauti_regression: tests/test_maktabasauti_regression.c $(MKS_DEPS) $(MKS_THIRD) include/maktabasauti/maktabasauti.h | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tests/test_maktabasauti_regression.c $(MKS_DEPS) -lm -lpthread -no-pie
$(OBJDIR)/test_maktabasauti_threads_asan: tests/test_maktabasauti_threads.c tests/mks_probe_corpus.h $(MKS_DEPS) $(MKS_THIRD) include/maktabasauti/maktabasauti.h | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -Itests -o $@ tests/test_maktabasauti_threads.c $(MKS_DEPS) -lm -lpthread -no-pie
$(OBJDIR)/test_maktabasauti_threads_tsan: tests/test_maktabasauti_threads.c tests/mks_probe_corpus.h $(MKS_DEPS) $(MKS_THIRD) include/maktabasauti/maktabasauti.h | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=thread $(MKS_INC) -Itests -o $@ tests/test_maktabasauti_threads.c $(MKS_DEPS) -lm -lpthread -no-pie

# audyt sond (probe) i open() na dowolnych danych: bufory o dokladnym rozmiarze ze stronami ochronnymi, tylko do odczytu, zasmiecany stos (niezainicjowane zmienne);
# trzy budowy: ASan+UBSan, zwykla -O2 z -ftrivial-auto-var-init=pattern (zmienne automatyczne wypelniane wzorcem) oraz -O0 bez sanitizerow
MKS_AUDIT_EXTRA := src/decoders/modern/sauti_decoder_opus.c src/decoders/modern/sauti_decoder_ffmpeg.c
MKS_AUDIT_FIXTURES := tests/data/lc.loas tests/data/lc.adif tests/data/lc.m4a tests/data/lc_frag.m4a tests/data/he2.m4a tests/data/lc.aac tests/data/lc_mono.aac tests/data/he1.aac tests/data/he1_mono.aac tests/data/he2.aac tests/data/he1.m4a tests/data/he1.loas tests/data/he1.adif tests/data/m51.aac $(wildcard tests/data/probe_real.*)
$(OBJDIR)/test_maktabasauti_probe_audit_asan: tests/test_maktabasauti_probe_audit.c tests/mks_probe_corpus.h $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -Itests -o $@ tests/test_maktabasauti_probe_audit.c $(MKS_DEPS) $(MKS_AUDIT_EXTRA) -lm -lpthread -no-pie
$(OBJDIR)/test_maktabasauti_probe_audit_pattern: tests/test_maktabasauti_probe_audit.c tests/mks_probe_corpus.h $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O2 -g -ftrivial-auto-var-init=pattern $(MKS_INC) -Itests -o $@ tests/test_maktabasauti_probe_audit.c $(MKS_DEPS) $(MKS_AUDIT_EXTRA) -lm -lpthread -no-pie
$(OBJDIR)/test_maktabasauti_probe_audit_plain: tests/test_maktabasauti_probe_audit.c tests/mks_probe_corpus.h $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O0 -g $(MKS_INC) -Itests -o $@ tests/test_maktabasauti_probe_audit.c $(MKS_DEPS) $(MKS_AUDIT_EXTRA) -lm -lpthread -no-pie
# wrogie wejscie (io niezgodne z kontraktem), skrajne wywolania uslug hosta, deklarowane rozmiary: ASan+UBSan oraz zwykla budowa
$(OBJDIR)/test_maktabasauti_hostile_io_asan: tests/test_maktabasauti_hostile_io.c $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tests/test_maktabasauti_hostile_io.c $(MKS_DEPS) -lm -lpthread -no-pie
$(OBJDIR)/test_maktabasauti_hostile_io_plain: tests/test_maktabasauti_hostile_io.c $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O2 -g -ftrivial-auto-var-init=pattern $(MKS_INC) -o $@ tests/test_maktabasauti_hostile_io.c $(MKS_DEPS) -lm -lpthread -no-pie
.PHONY: test-maktabasauti-hostile-io
test-maktabasauti-hostile-io: $(OBJDIR)/test_maktabasauti_hostile_io_asan $(OBJDIR)/test_maktabasauti_hostile_io_plain
	ASAN_OPTIONS=allocator_may_return_null=1 ./$(OBJDIR)/test_maktabasauti_hostile_io_asan tests/data/lc.aac
	./$(OBJDIR)/test_maktabasauti_hostile_io_plain tests/data/lc.aac
# regresje bledow w bibliotekach zewnetrznych (stb_vorbis, minimp3) znalezionych przez audyt sond: ASan+UBSan oraz zwykla budowa ze wzorcem w zmiennych automatycznych
THH_SRC := src/decoders/modern/sauti_decoder_vorbis.c src/decoders/modern/sauti_decoder_mp3.c
$(OBJDIR)/test_third_party_hardening_asan: tests/test_third_party_hardening.c $(THH_SRC) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iinclude -Ithird_party -o $@ tests/test_third_party_hardening.c $(THH_SRC) -lm
$(OBJDIR)/test_third_party_hardening_plain: tests/test_third_party_hardening.c $(THH_SRC) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O2 -g -ftrivial-auto-var-init=pattern -Iinclude -Ithird_party -o $@ tests/test_third_party_hardening.c $(THH_SRC) -lm
.PHONY: test-third-party-hardening
test-third-party-hardening: $(OBJDIR)/test_third_party_hardening_asan $(OBJDIR)/test_third_party_hardening_plain
	ASAN_OPTIONS=allocator_may_return_null=1 ./$(OBJDIR)/test_third_party_hardening_asan tests/data/probe_real.ogg tests/data/probe_real.mp3
	./$(OBJDIR)/test_third_party_hardening_plain tests/data/probe_real.ogg tests/data/probe_real.mp3
# ograniczenie pracy przewijania MIDI (R2): cisza pomijana dokladnie, budzet dla grajacych glosow, stan zachowany po niepowodzeniu; przez stare i nowe API
$(OBJDIR)/test_midi_seek_bounds: tests/test_midi_seek_bounds.c $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O2 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tests/test_midi_seek_bounds.c $(MKS_DEPS) -lm -lpthread -no-pie
.PHONY: test-midi-seek-bounds
test-midi-seek-bounds: $(OBJDIR)/test_midi_seek_bounds
	./$(OBJDIR)/test_midi_seek_bounds
# strumieniowe AAC ADTS (etap 2): wejscia nieprzewijalne, czesciowe odczyty, WOULD_BLOCK, urwane/uszkodzone strumienie, pamiec niezalezna od dlugosci, seek dokladny
$(OBJDIR)/test_maktabasauti_aac_stream: tests/test_maktabasauti_aac_stream.c $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tests/test_maktabasauti_aac_stream.c $(MKS_DEPS) -lm -lpthread -no-pie
.PHONY: test-maktabasauti-aac-stream
test-maktabasauti-aac-stream: $(OBJDIR)/test_maktabasauti_aac_stream
	ASAN_OPTIONS=allocator_may_return_null=1 ./$(OBJDIR)/test_maktabasauti_aac_stream tests/data
$(OBJDIR)/test_maktabasauti_dvs_stream: tests/test_maktabasauti_dvs_stream.c $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tests/test_maktabasauti_dvs_stream.c $(MKS_DEPS) -lm -lpthread -no-pie
.PHONY: test-maktabasauti-dvs-stream
test-maktabasauti-dvs-stream: $(OBJDIR)/test_maktabasauti_dvs_stream
	ASAN_OPTIONS=allocator_may_return_null=1 ./$(OBJDIR)/test_maktabasauti_dvs_stream
$(OBJDIR)/test_maktabasauti_pcm_stream: tests/test_maktabasauti_pcm_stream.c $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tests/test_maktabasauti_pcm_stream.c $(MKS_DEPS) -lm -lpthread -no-pie
$(OBJDIR)/maktabasauti_cli: tools/maktabasauti_cli.c $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 $(MKS_INC) -o $@ tools/maktabasauti_cli.c $(MKS_DEPS) -lm -lpthread -no-pie
$(OBJDIR)/test_aac_er: tests/test_aac_er.c src/decoders/modern/aac/sauti_aac_er.c include/sauti_aac_er.h $(filter-out src/decoders/modern/aac/sauti_aac_er.c,$(AAC_NATIVE_ADAPTER_DEPS)) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iinclude -o $@ tests/test_aac_er.c src/sauti_decoder.c $(filter-out src/decoders/modern/aac/sauti_aac_er.c,$(AAC_NATIVE_ADAPTER_DEPS)) -lm -no-pie
$(OBJDIR)/test_aac_lc960: tests/test_aac_lc960.c tests/test_aac_lc960_vectors.h tests/test_aac_lc960_fingerprints.h $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tests/test_aac_lc960.c $(MKS_DEPS) -lm -lpthread -no-pie
$(OBJDIR)/test_aac_main: tests/test_aac_main.c tests/test_aac_main_vectors.h tests/test_aac_main_fingerprints.h tests/test_aac_main_mc_fingerprints.h $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tests/test_aac_main.c $(MKS_DEPS) -lm -lpthread -no-pie
$(OBJDIR)/test_aac_ltp: tests/test_aac_ltp.c tests/test_aac_ltp_vectors.h tests/test_aac_ltp_fingerprints.h tests/test_aac_ltp_mc_fingerprints.h $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tests/test_aac_ltp.c $(MKS_DEPS) -lm -lpthread -no-pie
.PHONY: test-aac-ltp
test-aac-ltp: $(OBJDIR)/test_aac_ltp
	ASAN_OPTIONS=detect_leaks=1 ./$(OBJDIR)/test_aac_ltp
$(OBJDIR)/test_aac_er_syntax: tests/test_aac_er_syntax.c tests/test_aac_er_syntax_vectors.h tests/test_aac_er_syntax_fingerprints.h $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tests/test_aac_er_syntax.c $(MKS_DEPS) -lm -lpthread -no-pie
.PHONY: test-aac-er-syntax
test-aac-er-syntax: $(OBJDIR)/test_aac_er_syntax
	ASAN_OPTIONS=detect_leaks=1 ./$(OBJDIR)/test_aac_er_syntax
.PHONY: test-aac-main
test-aac-main: $(OBJDIR)/test_aac_main
	ASAN_OPTIONS=detect_leaks=1 ./$(OBJDIR)/test_aac_main
.PHONY: test-aac-lc960
test-aac-lc960: $(OBJDIR)/test_aac_lc960
	ASAN_OPTIONS=detect_leaks=1 ./$(OBJDIR)/test_aac_lc960
.PHONY: test-aac-er
test-aac-er: $(OBJDIR)/test_aac_er
	ASAN_OPTIONS=detect_leaks=1 ./$(OBJDIR)/test_aac_er
.PHONY: test-maktabasauti-pcm-stream
test-maktabasauti-pcm-stream: $(OBJDIR)/test_maktabasauti_pcm_stream
	ASAN_OPTIONS=allocator_may_return_null=1 ./$(OBJDIR)/test_maktabasauti_pcm_stream
$(OBJDIR)/test_maktabasauti_g711: tests/test_maktabasauti_g711.c tests/g711_golden.h $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -Itests -o $@ tests/test_maktabasauti_g711.c $(MKS_DEPS) -lm -lpthread -no-pie
.PHONY: test-maktabasauti-g711
test-maktabasauti-g711: $(OBJDIR)/test_maktabasauti_g711
	ASAN_OPTIONS=allocator_may_return_null=1 ./$(OBJDIR)/test_maktabasauti_g711
$(OBJDIR)/test_maktabasauti_voc_stream: tests/test_maktabasauti_voc_stream.c $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tests/test_maktabasauti_voc_stream.c $(MKS_DEPS) -lm -lpthread -no-pie
.PHONY: test-maktabasauti-voc-stream
test-maktabasauti-voc-stream: $(OBJDIR)/test_maktabasauti_voc_stream
	ASAN_OPTIONS=allocator_may_return_null=1 ./$(OBJDIR)/test_maktabasauti_voc_stream
$(OBJDIR)/test_maktabasauti_flac_stream: tests/test_maktabasauti_flac_stream.c $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tests/test_maktabasauti_flac_stream.c $(MKS_DEPS) -lm -lpthread -no-pie
.PHONY: test-maktabasauti-flac-stream
test-maktabasauti-flac-stream: $(OBJDIR)/test_maktabasauti_flac_stream
	ASAN_OPTIONS=allocator_may_return_null=1 ./$(OBJDIR)/test_maktabasauti_flac_stream
# ---- fuzzing oparty na pokryciu (libFuzzer, wymaga clanga) i powtorka korpusu bez clanga (gcc + ASan/UBSan, zwykle CI) ----
# cele: fuzz/fuzz_{probe,open,container,hostile_io}.c; korpus poczatkowy z tests/data przez fuzz/make_seeds.py
FUZZ_TARGETS := probe open container hostile_io
CLANG ?= clang
FUZZ_SEEDS := $(OBJDIR)/fuzz_seeds
$(OBJDIR)/dump_corpus: fuzz/dump_corpus.c tests/mks_probe_corpus.h | $(OBJDIR)
	$(CC) -std=gnu11 -O1 -Itests -o $@ fuzz/dump_corpus.c
$(FUZZ_SEEDS)/.stamp: fuzz/make_seeds.py fuzz/dump_corpus.c tests/mks_probe_corpus.h $(OBJDIR)/dump_corpus $(wildcard tests/data/*)
	@rm -rf $(FUZZ_SEEDS); mkdir -p $(FUZZ_SEEDS)/_syn
	./$(OBJDIR)/dump_corpus $(FUZZ_SEEDS)/_syn
	python3 -I fuzz/make_seeds.py $(FUZZ_SEEDS) $(FUZZ_SEEDS)/_syn
	@touch $@
$(OBJDIR)/fuzz_replay_%: fuzz/fuzz_%.c fuzz/fuzz_common.h fuzz/standalone_main.c $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) -std=gnu11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -Ifuzz -o $@ fuzz/fuzz_$*.c fuzz/standalone_main.c $(MKS_DEPS) -lm -lpthread -no-pie
.PHONY: test-fuzz-replay
test-fuzz-replay: $(FUZZ_SEEDS)/.stamp $(foreach t,$(FUZZ_TARGETS),$(OBJDIR)/fuzz_replay_$(t))
	@for t in $(FUZZ_TARGETS); do echo "== fuzz replay: $$t"; ASAN_OPTIONS=allocator_may_return_null=1 ./$(OBJDIR)/fuzz_replay_$$t $(FUZZ_SEEDS)/$$t $$(ls -d fuzz/regression/$$t 2>/dev/null) || exit 1; done
$(OBJDIR)/fuzz_%: fuzz/fuzz_%.c fuzz/fuzz_common.h $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CLANG) -std=gnu11 -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -Ifuzz -o $@ fuzz/fuzz_$*.c $(MKS_DEPS) -lm -lpthread -no-pie
# MemorySanitizer (niezainicjowane odczyty, ktorych ASan nie widzi): PELNY rejestr, takze AAC/AVR/DVSM/YM, bo jadra NASM (nieinstrumentowane przez MSan) sa zastapione przez src/portable
# (te same wyniki bit w bit: test-portable-equivalence).  Wymaga clanga; zrodla w C kompiluje sam clang, zadnego obiektu z gcc/NASM.
MKS_PORTABLE_SRC := $(sort $(wildcard $(SAUTI_PORTABLE_DIR)/*_portable.c))
MKS_DEPS_C = $(sort $(filter-out %.o,$(MKS_CORE_SRC) $(MKS_LEGACY_SRC) $(AAC_PLAYER_SRC)) $(MKS_PORTABLE_SRC))   # bez obiektow NASM (listy zrodel AAC wymieniaja je obok zrodel C)
MSAN_FLAGS := -std=gnu11 -g -O1 -DSAUTI_QMF_DISABLE_ASM -ffp-contract=off -fsanitize-memory-track-origins=2 -fno-omit-frame-pointer
MSAN_TARGETS := probe open container hostile_io
$(OBJDIR)/fuzz_msan_%: fuzz/fuzz_%.c fuzz/fuzz_common.h $(MKS_DEPS_C) $(MKS_THIRD) | $(OBJDIR)
	$(CLANG) $(MSAN_FLAGS) -fsanitize=fuzzer,memory $(MKS_INC) -I$(SAUTI_PORTABLE_DIR) -Ifuzz -o $@ fuzz/fuzz_$*.c $(MKS_DEPS_C) -lm -lpthread -no-pie
# powtorzenie korpusu (bez libFuzzera) pod MSan: ziarna + wejscia regresyjne, uruchamiane w CI przy kazdej zmianie
$(OBJDIR)/fuzz_replay_msan_%: fuzz/fuzz_%.c fuzz/fuzz_common.h fuzz/standalone_main.c $(MKS_DEPS_C) $(MKS_THIRD) | $(OBJDIR)
	$(CLANG) $(MSAN_FLAGS) -fsanitize=memory $(MKS_INC) -I$(SAUTI_PORTABLE_DIR) -Ifuzz -o $@ fuzz/fuzz_$*.c fuzz/standalone_main.c $(MKS_DEPS_C) -lm -lpthread -no-pie
.PHONY: test-msan-replay
test-msan-replay: $(FUZZ_SEEDS)/.stamp $(foreach t,$(MSAN_TARGETS),$(OBJDIR)/fuzz_replay_msan_$(t))
	@for t in $(MSAN_TARGETS); do echo "== MSan replay: $$t"; ASAN_OPTIONS= MSAN_OPTIONS=allocator_may_return_null=1:halt_on_error=1 ./$(OBJDIR)/fuzz_replay_msan_$$t $(FUZZ_SEEDS)/$$t $$(ls -d fuzz/regression/$$t 2>/dev/null) || exit 1; done
# jadra w C == jadra NASM, bit w bit (wymaga NASM; ten sam plik z -DSAUTI_PORTABLE_REF_SUFFIX daje nazwy *_c)
PORTABLE_EQ_ASM := $(AAC_DEQUANT_ASM) $(AAC_DSP_ASM) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_IMDCT_ASM) $(AAC_PS_MIX_ASM) $(AAC_SPECTRAL_ASM) $(AVR_ASM) $(DVSM_ASM) $(YM_ASM)
PORTABLE_EQ_SRC := $(filter-out %qmf%,$(MKS_PORTABLE_SRC))
$(OBJDIR)/test_portable_equivalence: tests/test_portable_equivalence.c $(MKS_PORTABLE_SRC) $(SAUTI_PORTABLE_DIR)/sauti_portable.h $(PORTABLE_EQ_ASM) | $(OBJDIR)
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iinclude -I$(SAUTI_PORTABLE_DIR) -DSAUTI_PORTABLE_REF_SUFFIX -o $@ tests/test_portable_equivalence.c $(PORTABLE_EQ_SRC) $(PORTABLE_EQ_ASM) -lm -no-pie
.PHONY: test-portable-equivalence
test-portable-equivalence: $(OBJDIR)/test_portable_equivalence
	./$(OBJDIR)/test_portable_equivalence
.PHONY: fuzz-build-msan fuzz-run-msan
fuzz-build-msan: $(foreach t,$(MSAN_TARGETS),$(OBJDIR)/fuzz_msan_$(t))
fuzz-run-msan: fuzz-build-msan $(FUZZ_SEEDS)/.stamp
	@for t in $(MSAN_TARGETS); do echo "== libFuzzer + MSan: $$t ($(FUZZ_SECONDS) s)"; rm -rf $(OBJDIR)/fuzz_corpus_msan_$$t; cp -r $(FUZZ_SEEDS)/$$t $(OBJDIR)/fuzz_corpus_msan_$$t; [ -d fuzz/regression/$$t ] && cp -r fuzz/regression/$$t/. $(OBJDIR)/fuzz_corpus_msan_$$t/; \
	  ./$(OBJDIR)/fuzz_msan_$$t -max_total_time=$(FUZZ_SECONDS) -timeout=30 -rss_limit_mb=3072 -print_final_stats=1 -dict=fuzz/formats.dict -artifact_prefix=$(OBJDIR)/fuzz_artifact_msan_$${t}_ $(OBJDIR)/fuzz_corpus_msan_$$t || exit 1; done
.PHONY: fuzz-build fuzz-run
fuzz-build: $(foreach t,$(FUZZ_TARGETS),$(OBJDIR)/fuzz_$(t))
# fuzz-run: kazdy cel FUZZ_SECONDS sekund (domyslnie 60) na kopii korpusu; kazda awaria = blad (artefakt crash-* w katalogu biezacym)
FUZZ_SECONDS ?= 60
fuzz-run: fuzz-build $(FUZZ_SEEDS)/.stamp
	@for t in $(FUZZ_TARGETS); do echo "== libFuzzer: $$t ($(FUZZ_SECONDS) s)"; rm -rf $(OBJDIR)/fuzz_corpus_$$t; cp -r $(FUZZ_SEEDS)/$$t $(OBJDIR)/fuzz_corpus_$$t; [ -d fuzz/regression/$$t ] && cp -r fuzz/regression/$$t/. $(OBJDIR)/fuzz_corpus_$$t/; \
	  ASAN_OPTIONS=allocator_may_return_null=1:detect_leaks=1 ./$(OBJDIR)/fuzz_$$t -max_total_time=$(FUZZ_SECONDS) -timeout=30 -rss_limit_mb=3072 -print_final_stats=1 -dict=fuzz/formats.dict -artifact_prefix=$(OBJDIR)/fuzz_artifact_$${t}_ $(OBJDIR)/fuzz_corpus_$$t || exit 1; done

.PHONY: test-maktabasauti-probe-audit
test-maktabasauti-probe-audit: $(OBJDIR)/test_maktabasauti_probe_audit_asan $(OBJDIR)/test_maktabasauti_probe_audit_pattern $(OBJDIR)/test_maktabasauti_probe_audit_plain
	ASAN_OPTIONS=allocator_may_return_null=1 ./$(OBJDIR)/test_maktabasauti_probe_audit_asan $(MKS_AUDIT_FIXTURES)
	./$(OBJDIR)/test_maktabasauti_probe_audit_pattern $(MKS_AUDIT_FIXTURES)
	./$(OBJDIR)/test_maktabasauti_probe_audit_plain $(MKS_AUDIT_FIXTURES)

# zimny start z wielu watkow naraz (swieze procesy): wyscigi inicjalizacji leniwej widoczne tylko przy pierwszym uzyciu
$(OBJDIR)/test_maktabasauti_coldstart_asan: tests/test_maktabasauti_coldstart.c tests/mks_probe_corpus.h $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -Itests -o $@ tests/test_maktabasauti_coldstart.c $(MKS_DEPS) -lm -lpthread -no-pie
$(OBJDIR)/test_maktabasauti_coldstart_tsan: tests/test_maktabasauti_coldstart.c tests/mks_probe_corpus.h $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=thread $(MKS_INC) -Itests -o $@ tests/test_maktabasauti_coldstart.c $(MKS_DEPS) -lm -lpthread -no-pie

.PHONY: test-maktabasauti-core test-maktabasauti-regression
test-maktabasauti-core: check-maktabasauti-c99 $(OBJDIR)/test_maktabasauti_core
	./$(OBJDIR)/test_maktabasauti_core
test-maktabasauti-regression: $(OBJDIR)/test_maktabasauti_regression
	./$(OBJDIR)/test_maktabasauti_regression tests/data/lc.loas tests/data/lc_mono.loas tests/data/he2.loas tests/data/lc.adif tests/data/lc_mono.adif tests/data/lc.m4a tests/data/lc_mono.m4a tests/data/he2.m4a tests/data/lc_frag.m4a tests/data/lc.aac tests/data/lc_mono.aac tests/data/he1_mono.aac tests/data/he1.aac tests/data/he2.aac tests/data/he1.m4a tests/data/he1.loas tests/data/he1.adif tests/data/m51.aac

# rdzen bez opakowan starszych dekoderow: przenosna budowa (bez NASM, bez plikow starszych dekoderow), C99 pedantic; do CI na Windows/macOS/BSD/ARM64
.PHONY: test-maktabasauti-core-portable
test-maktabasauti-core-portable:
	@mkdir -p $(OBJDIR)
	$(CC) -std=c99 -pedantic -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iinclude -Isrc/maktabasauti -o $(OBJDIR)/test_maktabasauti_core_portable \
	  tests/test_maktabasauti_core.c $(filter-out src/maktabasauti/mks_legacy.c,$(MKS_CORE_SRC)) src/maktabasauti/mks_nolegacy.c -lm
	./$(OBJDIR)/test_maktabasauti_core_portable
$(OBJDIR)/sauti_usac_raw: tools/sauti_usac_raw.c include/sauti_usac.h $(AAC_NATIVE_ADAPTER_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tools/sauti_usac_raw.c src/sauti_decoder.c $(AAC_NATIVE_ADAPTER_DEPS) -lm -no-pie
$(OBJDIR)/test_usac: tests/test_usac.c tests/test_usac_vectors.h tests/test_usac_fingerprints.h include/sauti_usac.h $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tests/test_usac.c $(MKS_DEPS) -lm -lpthread -no-pie
.PHONY: test-usac
test-usac: $(OBJDIR)/test_usac
	ASAN_OPTIONS=detect_leaks=1 ./$(OBJDIR)/test_usac
$(OBJDIR)/test_usac_lpd: tests/test_usac_lpd.c include/sauti_usac_lpd.h include/sauti_usac_lpd_dec.h include/sauti_usac_lpd_tables.h src/decoders/modern/aac/sauti_usac_lpd.c src/decoders/modern/aac/sauti_usac_lpd_lpc.c src/decoders/modern/aac/sauti_usac_lpd_dec.c src/decoders/modern/aac/sauti_usac_lpd_tables.c | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iinclude -o $@ tests/test_usac_lpd.c src/decoders/modern/aac/sauti_usac_lpd.c src/decoders/modern/aac/sauti_usac_lpd_lpc.c src/decoders/modern/aac/sauti_usac_lpd_dec.c src/decoders/modern/aac/sauti_usac_lpd_tables.c src/decoders/modern/aac/sauti_usac_ac.c src/decoders/modern/aac/sauti_usac_tables.c -lm -no-pie
.PHONY: test-usac-lpd
test-usac-lpd: $(OBJDIR)/test_usac_lpd
	ASAN_OPTIONS=detect_leaks=1 ./$(OBJDIR)/test_usac_lpd
$(OBJDIR)/test_usac_pvc: tests/test_usac_pvc.c include/sauti_usac_pvc.h include/sauti_usac_pvc_tables.h src/decoders/modern/aac/sauti_usac_pvc.c src/decoders/modern/aac/sauti_usac_pvc_tables.c | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iinclude -o $@ tests/test_usac_pvc.c src/decoders/modern/aac/sauti_usac_pvc.c src/decoders/modern/aac/sauti_usac_pvc_tables.c -lm -no-pie
.PHONY: test-usac-pvc
$(OBJDIR)/test_usac_hbe: tests/test_usac_hbe.c include/sauti_usac_hbe.h include/sauti_usac_hbe_tables.h src/decoders/modern/aac/sauti_usac_hbe.c src/decoders/modern/aac/sauti_usac_hbe_tables.c src/decoders/modern/aac/sauti_usac_mps.c src/decoders/modern/aac/sauti_aac_eld_sac.c src/decoders/modern/aac/sauti_unidrc.c src/decoders/modern/aac/sauti_unidrc_run.c src/decoders/modern/aac/sauti_unidrc_tables.c src/decoders/modern/aac/sauti_aac_sbr_mpsld.c src/decoders/modern/aac/sauti_aac_sbr_mpsld_table.c src/decoders/modern/aac/sauti_aac_eld_sac_dec.c src/decoders/modern/aac/sauti_aac_eld_sac_tables.c src/decoders/modern/aac/sauti_usac_mps_tables.c src/decoders/modern/aac/sauti_usac_mps_dec.c src/decoders/modern/aac/sauti_aac_sbr_qmf_analysis.c src/decoders/modern/aac/sauti_aac_sbr_qmf_synth_window.c $(OBJDIR)/sauti_aac_sbr_qmf_synth_window_asm.o $(OBJDIR)/sauti_aac_sbr_qmf_synth_window_avx2.o | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iinclude -o $@ $(OBJDIR)/sauti_aac_sbr_qmf_synth_window_asm.o $(OBJDIR)/sauti_aac_sbr_qmf_synth_window_avx2.o tests/test_usac_hbe.c src/decoders/modern/aac/sauti_usac_hbe.c src/decoders/modern/aac/sauti_usac_hbe_tables.c src/decoders/modern/aac/sauti_usac_mps.c src/decoders/modern/aac/sauti_aac_eld_sac.c src/decoders/modern/aac/sauti_unidrc.c src/decoders/modern/aac/sauti_unidrc_run.c src/decoders/modern/aac/sauti_unidrc_tables.c src/decoders/modern/aac/sauti_aac_sbr_mpsld.c src/decoders/modern/aac/sauti_aac_sbr_mpsld_table.c src/decoders/modern/aac/sauti_aac_eld_sac_dec.c src/decoders/modern/aac/sauti_aac_eld_sac_tables.c src/decoders/modern/aac/sauti_usac_mps_tables.c src/decoders/modern/aac/sauti_usac_mps_dec.c src/decoders/modern/aac/sauti_aac_sbr_qmf_analysis.c src/decoders/modern/aac/sauti_aac_sbr_qmf_synth_window.c -lm -no-pie
.PHONY: test-usac-hbe
$(OBJDIR)/test_usac_mps: tests/test_usac_mps.c include/sauti_usac_mps.h include/sauti_usac_mps_dec.h include/sauti_usac_mps_tables.h src/decoders/modern/aac/sauti_usac_mps.c src/decoders/modern/aac/sauti_usac_mps_dec.c src/decoders/modern/aac/sauti_usac_mps_tables.c | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iinclude -o $@ tests/test_usac_mps.c src/decoders/modern/aac/sauti_usac_mps.c src/decoders/modern/aac/sauti_usac_mps_dec.c src/decoders/modern/aac/sauti_usac_mps_tables.c -lm -no-pie
.PHONY: test-usac-mps
test-usac-mps: $(OBJDIR)/test_usac_mps
	ASAN_OPTIONS=detect_leaks=1 ./$(OBJDIR)/test_usac_mps
test-usac-hbe: $(OBJDIR)/test_usac_hbe
	ASAN_OPTIONS=detect_leaks=1 ./$(OBJDIR)/test_usac_hbe
test-usac-pvc: $(OBJDIR)/test_usac_pvc
	ASAN_OPTIONS=detect_leaks=1 ./$(OBJDIR)/test_usac_pvc
$(OBJDIR)/test_usac_stream: tests/test_usac_stream.c tests/test_usac_vectors.h include/sauti_usac.h $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tests/test_usac_stream.c $(MKS_DEPS) -lm -lpthread -no-pie
.PHONY: test-usac-stream
test-usac-stream: $(OBJDIR)/test_usac_stream
	ASAN_OPTIONS=detect_leaks=1 ./$(OBJDIR)/test_usac_stream
$(OBJDIR)/test_unidrc: tests/test_unidrc.c include/sauti_unidrc.h include/sauti_usac.h $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tests/test_unidrc.c $(MKS_DEPS) -lm -lpthread -no-pie
.PHONY: test-unidrc
test-unidrc: $(OBJDIR)/test_unidrc
	ASAN_OPTIONS=detect_leaks=1 ./$(OBJDIR)/test_unidrc
$(OBJDIR)/test_usac_mc: tests/test_usac_mc.c tests/test_usac_mc_vectors.h tests/test_usac_mc_fingerprints.h include/sauti_usac.h $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tests/test_usac_mc.c $(MKS_DEPS) -lm -lpthread -no-pie
$(OBJDIR)/maktabasauti_mutfuzz: tools/maktabasauti_mutfuzz.c $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tools/maktabasauti_mutfuzz.c $(MKS_DEPS) -lm -lpthread -no-pie
# mutation fuzzer for the engine (ASan / UBSan): make fuzz-maktabasauti FUZZ_SEEDS="tests/data/usac_*.m4a ..." [FUZZ_ITER=2000 FUZZ_SEED=1]
.PHONY: fuzz-maktabasauti
fuzz-maktabasauti: $(OBJDIR)/maktabasauti_mutfuzz
	@test -n "$(FUZZ_SEEDS)" || { echo "FUZZ_SEEDS=<files> required"; exit 2; }
	ASAN_OPTIONS=allocator_may_return_null=1 $(OBJDIR)/maktabasauti_mutfuzz $(FUZZ_ITER) $(FUZZ_SEED) $(FUZZ_SEEDS)
$(OBJDIR)/test_usac_preroll: tests/test_usac_preroll.c tests/test_usac_pr_vectors.h include/sauti_usac.h $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tests/test_usac_preroll.c $(MKS_DEPS) -lm -lpthread -no-pie
.PHONY: test-usac-preroll
test-usac-preroll: $(OBJDIR)/test_usac_preroll
	ASAN_OPTIONS=detect_leaks=1 ./$(OBJDIR)/test_usac_preroll
$(OBJDIR)/test_he960: tests/test_he960.c tests/test_he960_fingerprints.h $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tests/test_he960.c $(MKS_DEPS) -lm -lpthread -no-pie
.PHONY: test-he960
test-he960: $(OBJDIR)/test_he960
	ASAN_OPTIONS=detect_leaks=1 ./$(OBJDIR)/test_he960
$(OBJDIR)/test_eldsbr: tests/test_eldsbr.c tests/test_eldsbr_fingerprints.h $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tests/test_eldsbr.c $(MKS_DEPS) -lm -lpthread -no-pie
.PHONY: test-eldsbr
test-eldsbr: $(OBJDIR)/test_eldsbr
	ASAN_OPTIONS=detect_leaks=1 ./$(OBJDIR)/test_eldsbr
$(OBJDIR)/test_dvsm_winrec: tests/test_dvsm_winrec.c $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tests/test_dvsm_winrec.c $(MKS_DEPS) -lm -lpthread -no-pie
.PHONY: test-dvsm-winrec
test-dvsm-winrec: $(OBJDIR)/test_dvsm_winrec
	ASAN_OPTIONS=detect_leaks=1 ./$(OBJDIR)/test_dvsm_winrec
$(OBJDIR)/test_svx: tests/test_svx.c $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tests/test_svx.c $(MKS_DEPS) -lm -lpthread -no-pie
.PHONY: test-svx
test-svx: $(OBJDIR)/test_svx
	ASAN_OPTIONS=detect_leaks=1 ./$(OBJDIR)/test_svx
$(OBJDIR)/test_usac_xaac: tests/test_usac_xaac.c tests/test_usac_xaac_fingerprints.h $(MKS_DEPS) $(MKS_THIRD) | $(OBJDIR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(MKS_INC) -o $@ tests/test_usac_xaac.c $(MKS_DEPS) -lm -lpthread -no-pie
.PHONY: test-usac-xaac
test-usac-xaac: $(OBJDIR)/test_usac_xaac
	ASAN_OPTIONS=detect_leaks=1 ./$(OBJDIR)/test_usac_xaac
.PHONY: test-usac-mc
test-usac-mc: $(OBJDIR)/test_usac_mc
	ASAN_OPTIONS=detect_leaks=1 ./$(OBJDIR)/test_usac_mc
