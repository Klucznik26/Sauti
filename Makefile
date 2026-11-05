# Stage 6: x86-64 NASM engine, host tests and optional Linux SDL2/Wayland.
NASM ?= nasm
CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Werror
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
AAC_SBR_QMF_SYNTH_WINDOW_SRC := src/decoders/modern/aac/sauti_aac_sbr_qmf_synth_window.c
AAC_SBR_QMF_SYNTH_MOD_SRC := src/decoders/modern/aac/sauti_aac_sbr_qmf_synth_mod.c
AAC_SBR_QMF_ANALYSIS_SRC := src/decoders/modern/aac/sauti_aac_sbr_qmf_analysis.c
AAC_SBR_QMF_FRAME_SRC := src/decoders/modern/aac/sauti_aac_sbr_qmf_frame.c
AAC_SBR_PAYLOAD_SRC := src/decoders/modern/aac/sauti_aac_sbr_payload.c
AAC_HE_ADTS_MONO_SRC := src/decoders/modern/aac/sauti_aac_he_adts_mono.c
AAC_HE_ADTS_QMF_SRC := src/decoders/modern/aac/sauti_aac_he_adts_qmf.c
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
AAC_LC_FRAME_SRC := src/decoders/modern/aac/sauti_aac_lc_frame.c
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
	$(NASM) -f elf64 -o $@ $<

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
	$(NASM) -f elf64 -o $@ $<

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
	$(NASM) -f elf64 -o $@ $<

$(OBJDIR)/test_decoder_avr_san: tests/test_decoder_avr.c include/sauti_decoder_avr.h include/sauti_decoder.h src/sauti_decoder.c $(AVR_SRC) $(AVR_ASM) | $(OBJDIR)
	$(CC) $(YM_SAN_FLAGS) -Iinclude -o $@ tests/test_decoder_avr.c src/sauti_decoder.c $(AVR_SRC) $(AVR_ASM) -no-pie

.PHONY: test-avr
test-avr: $(OBJDIR)/test_decoder_avr $(OBJDIR)/test_decoder_avr_san
	./$(OBJDIR)/test_decoder_avr
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_decoder_avr_san

$(OBJDIR)/test_decoder_avr: tests/test_decoder_avr.c include/sauti_decoder_avr.h include/sauti_decoder.h src/sauti_decoder.c $(AVR_SRC) $(AVR_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_decoder_avr.c src/sauti_decoder.c $(AVR_SRC) $(AVR_ASM) -no-pie

$(OBJDIR)/sauti_core.o: src/sauti_core.asm | $(OBJDIR)
	$(NASM) -f elf64 -o $@ $<

$(OBJDIR)/sauti_fft.o: src/sauti_fft.asm | $(OBJDIR)
	$(NASM) -f elf64 -o $@ $<

$(OBJDIR)/sauti_spectrum.o: src/sauti_spectrum.asm | $(OBJDIR)
	$(NASM) -f elf64 -o $@ $<

$(OBJDIR)/sauti_frame.o: src/sauti_frame.asm | $(OBJDIR)
	$(NASM) -f elf64 -o $@ $<

$(OBJDIR)/sauti_db_log.o: src/sauti_db_log.asm | $(OBJDIR)
	$(NASM) -f elf64 -o $@ $<

$(OBJDIR)/sauti_audio_ring.o: src/sauti_audio_ring.asm | $(OBJDIR)
	$(NASM) -f elf64 -o $@ $<

$(OBJDIR)/sauti_stereo_ring.o: src/sauti_stereo_ring.asm | $(OBJDIR)
	$(NASM) -f elf64 -o $@ $<

$(OBJDIR)/sauti_stereo_analysis.o: src/sauti_stereo_analysis.asm | $(OBJDIR)
	$(NASM) -f elf64 -o $@ $<

$(OBJDIR)/sauti_stereo_bars.o: src/sauti_stereo_bars.asm | $(OBJDIR)
	$(NASM) -f elf64 -o $@ $<

$(OBJDIR)/sauti_peak_hold.o: src/sauti_peak_hold.asm | $(OBJDIR)
	$(NASM) -f elf64 -o $@ $<

$(OBJDIR)/sauti_levels.o: src/sauti_levels.asm | $(OBJDIR)
	$(NASM) -f elf64 -o $@ $<

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
	$(NASM) -f elf64 -o $@ $<

$(OBJDIR)/test_aac_headers: tests/test_aac_headers.c include/sauti_aac_headers.h $(AAC_CONFIG_SRC) $(AAC_HEADERS_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_headers.c $(AAC_CONFIG_SRC) $(AAC_HEADERS_ASM) -no-pie

.PHONY: test-aac-headers
test-aac-headers: $(OBJDIR)/test_aac_headers
	./$(OBJDIR)/test_aac_headers

$(AAC_DSP_ASM): src/decoders/modern/aac/sauti_aac_dsp.asm | $(OBJDIR)
	$(NASM) -f elf64 -o $@ $<

$(OBJDIR)/test_aac_syntax: tests/test_aac_syntax.c include/sauti_aac_syntax.h $(AAC_SYNTAX_SRC) $(AAC_DSP_ASM) $(AAC_HEADERS_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_syntax.c $(AAC_SYNTAX_SRC) $(AAC_HEADERS_ASM) $(AAC_DSP_ASM) -no-pie

.PHONY: test-aac-syntax
test-aac-syntax: $(OBJDIR)/test_aac_syntax
	./$(OBJDIR)/test_aac_syntax

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

AAC_SBR_PAYLOAD_DEPS := $(AAC_SBR_PAYLOAD_SRC) $(AAC_SYNTAX_SRC) $(AAC_SBR_GRID_SRC) $(AAC_SBR_CONTROL_SRC) $(AAC_SBR_QUANT_SRC) $(AAC_SBR_HUFFMAN_SRC) $(AAC_SBR_ENERGY_SRC) $(AAC_SBR_LIMITS_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_SBR_PATCH_SRC) $(AAC_HEADERS_ASM)

$(OBJDIR)/test_aac_sbr_payload: tests/test_aac_sbr_payload.c include/sauti_aac_sbr_payload.h $(AAC_SBR_PAYLOAD_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_payload.c $(AAC_SBR_PAYLOAD_DEPS) -lm -no-pie

.PHONY: test-aac-sbr-payload
test-aac-sbr-payload: $(OBJDIR)/test_aac_sbr_payload
	./$(OBJDIR)/test_aac_sbr_payload

AAC_SBR_ADTS_MONO_DEPS := $(AAC_HE_ADTS_MONO_SRC) $(AAC_SBR_PAYLOAD_SRC) $(AAC_SBR_GRID_SRC) $(AAC_SBR_CONTROL_SRC) $(AAC_SBR_QUANT_SRC) $(AAC_SBR_HUFFMAN_SRC) $(AAC_SBR_ENERGY_SRC) $(AAC_SBR_LIMITS_SRC) $(AAC_SBR_MASTER_SRC) $(AAC_SBR_MASTER_LOG_SRC) $(AAC_SBR_FREQ_TABLES_SRC) $(AAC_SBR_PATCH_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM)

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

AAC_SBR_TEMPORAL_HARMONIC_PREVIEW_SRC := src/decoders/modern/aac/sauti_aac_sbr_temporal_harmonic_preview.c
AAC_SBR_TEMPORAL_HARMONIC_PREVIEW_DEPS := $(sort $(AAC_SBR_TEMPORAL_MIX_DEPS) $(AAC_SBR_TEMPORAL_HARMONIC_PREVIEW_SRC))

$(OBJDIR)/test_aac_sbr_temporal_harmonic_preview: tests/test_aac_sbr_temporal_harmonic_preview.c include/sauti_aac_sbr_temporal_harmonic_preview.h $(AAC_SBR_TEMPORAL_HARMONIC_PREVIEW_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_temporal_harmonic_preview.c $(AAC_SBR_TEMPORAL_HARMONIC_PREVIEW_DEPS) -lm -no-pie

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

$(OBJDIR)/test_aac_sbr_adts_qmf: tests/test_aac_sbr_adts_qmf.c tests/test_aac_sbr_adts_mono.c include/sauti_aac_he_adts_qmf.h $(AAC_SBR_ADTS_QMF_DEPS) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_sbr_adts_qmf.c $(AAC_SBR_ADTS_QMF_DEPS) -lm -no-pie

.PHONY: test-aac-sbr-adts-qmf
test-aac-sbr-adts-qmf: $(OBJDIR)/test_aac_sbr_adts_qmf
	./$(OBJDIR)/test_aac_sbr_adts_qmf

$(OBJDIR)/sauti_he_aac_probe: tools/sauti_he_aac_probe.c include/sauti_aac_he_adts_qmf.h $(AAC_SBR_ADTS_QMF_DEPS) $(AAC_SBR_TEMPORAL_QMF_SRC) $(AAC_SBR_TEMPORAL_POWER_SRC) $(AAC_SBR_TEMPORAL_GAIN_SRC) $(AAC_SBR_TEMPORAL_APPLY_SRC) $(AAC_SBR_TEMPORAL_NOISE_SRC) $(AAC_SBR_TEMPORAL_MIX_SRC) $(AAC_SBR_TEMPORAL_SMOOTH_SRC) $(AAC_SBR_TEMPORAL_PCM_PREVIEW_SRC) $(AAC_SBR_TEMPORAL_ENERGY_AUDIT_SRC) $(AAC_SBR_TEMPORAL_ENVELOPE_AUDIT_SRC) $(AAC_SBR_LPC_ESTIMATE_SRC) $(AAC_SBR_TEMPORAL_HARMONIC_PREVIEW_SRC) $(AAC_SBR_TEMPORAL_HF_UNITS_SRC) $(AAC_SBR_GAIN_PARTITION_SRC) $(AAC_SBR_GAIN_LIMIT_PARTITION_SRC) $(AAC_SBR_TEMPORAL_REFERENCE_GAIN_SRC) $(AAC_SBR_TEMPORAL_GAIN_BRIDGE_SRC) $(AAC_SBR_HF_PHASE_PROBE_SRC) $(AAC_SBR_TEMPORAL_SOURCE6_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tools/sauti_he_aac_probe.c $(AAC_SBR_ADTS_QMF_DEPS) $(AAC_SBR_TEMPORAL_QMF_SRC) $(AAC_SBR_TEMPORAL_POWER_SRC) $(AAC_SBR_TEMPORAL_GAIN_SRC) $(AAC_SBR_TEMPORAL_APPLY_SRC) $(AAC_SBR_TEMPORAL_NOISE_SRC) $(AAC_SBR_TEMPORAL_MIX_SRC) $(AAC_SBR_TEMPORAL_SMOOTH_SRC) $(AAC_SBR_TEMPORAL_PCM_PREVIEW_SRC) $(AAC_SBR_TEMPORAL_ENERGY_AUDIT_SRC) $(AAC_SBR_TEMPORAL_ENVELOPE_AUDIT_SRC) $(AAC_SBR_LPC_ESTIMATE_SRC) $(AAC_SBR_TEMPORAL_HARMONIC_PREVIEW_SRC) $(AAC_SBR_TEMPORAL_HF_UNITS_SRC) $(AAC_SBR_GAIN_PARTITION_SRC) $(AAC_SBR_GAIN_LIMIT_PARTITION_SRC) $(AAC_SBR_TEMPORAL_REFERENCE_GAIN_SRC) $(AAC_SBR_TEMPORAL_GAIN_BRIDGE_SRC) $(AAC_SBR_HF_PHASE_PROBE_SRC) $(AAC_SBR_TEMPORAL_SOURCE6_SRC) -lm -no-pie

.PHONY: test-aac-he-stem-ablation
test-aac-he-stem-ablation:
	python3 tools/sauti_he_aac_stem_ablation.py --self-test

.PHONY: test-aac-he-preview-compare
test-aac-he-preview-compare:
	python3 tools/sauti_he_aac_compare_pcm.py --self-test

.PHONY: probe-he-aac test-aac-he-probe-build
probe-he-aac test-aac-he-probe-build: $(OBJDIR)/sauti_he_aac_probe
	@echo "HE-AAC real ADTS probe ready: ./$(OBJDIR)/sauti_he_aac_probe FILE.aac [MAX_FRAMES]"

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
	$(NASM) -f elf64 -o $@ $<

$(OBJDIR)/test_aac_huffman: tests/test_aac_huffman.c include/sauti_aac_huffman.h $(AAC_HUFFMAN_SRC) $(AAC_HUFFMAN_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_huffman.c $(AAC_HUFFMAN_SRC) $(AAC_HUFFMAN_ASM) -no-pie

.PHONY: test-aac-huffman
test-aac-huffman: $(OBJDIR)/test_aac_huffman
	./$(OBJDIR)/test_aac_huffman

$(AAC_SPECTRAL_ASM): src/decoders/modern/aac/sauti_aac_spectral.asm | $(OBJDIR)
	$(NASM) -f elf64 -o $@ $<

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
	$(NASM) -f elf64 -o $@ $<

$(OBJDIR)/test_aac_dequant: tests/test_aac_dequant.c include/sauti_aac_dequant.h $(AAC_DEQUANT_SRC) $(AAC_DEQUANT_ASM) $(AAC_BANDS_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_dequant.c $(AAC_DEQUANT_SRC) $(AAC_BANDS_SRC) $(AAC_DEQUANT_ASM) -lm -no-pie

.PHONY: test-aac-dequant
test-aac-dequant: $(OBJDIR)/test_aac_dequant
	./$(OBJDIR)/test_aac_dequant

$(AAC_IMDCT_ASM): src/decoders/modern/aac/sauti_aac_imdct.asm | $(OBJDIR)
	$(NASM) -f elf64 -o $@ $<

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
AAC_PLAYER_SRC = $(AAC_NATIVE_ADAPTER_SRC) $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC)
AAC_PLAYER_ASM = $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM)

AAC_CPE_SRC := src/decoders/modern/aac/sauti_aac_cpe.c

$(OBJDIR)/test_aac_cpe: tests/test_aac_cpe.c include/sauti_aac_cpe.h $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_aac_cpe.c $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) -lm -no-pie

# ASan/UBSan is applied to the C sources of the CPE decoder and test.
# Existing NASM objects are linked unchanged; sanitizer coverage does not
# extend into hand-written assembly. Use a separate binary to keep normal
# tests unaffected.
AAC_SAN_FLAGS := -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
$(OBJDIR)/test_aac_cpe_san: tests/test_aac_cpe.c include/sauti_aac_cpe.h $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) | $(OBJDIR)
	$(CC) $(AAC_SAN_FLAGS) -Iinclude -o $@ tests/test_aac_cpe.c $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) -lm -no-pie

.PHONY: test-aac-sanitize
test-aac-sanitize: $(OBJDIR)/test_aac_cpe_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OBJDIR)/test_aac_cpe_san

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
$(OBJDIR)/test_decoder_aac_native: tests/test_decoder_aac_native.c include/sauti_decoder_aac_native.h $(AAC_NATIVE_ADAPTER_SRC) src/sauti_decoder.c $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) | $(OBJDIR)
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_decoder_aac_native.c src/sauti_decoder.c $(AAC_NATIVE_ADAPTER_SRC) $(AAC_CPE_SRC) $(AAC_LC_FRAME_SRC) $(AAC_LC_SECTIONS_SRC) $(AAC_SYNTAX_SRC) $(AAC_SCALEFACTORS_SRC) $(AAC_SPECTRAL_DATA_SRC) $(AAC_TABLES_SRC) $(AAC_HUFFMAN_SRC) $(AAC_SPECTRAL_SRC) $(AAC_LC_SYNTH_SRC) $(AAC_DEQUANT_SRC) $(AAC_IMDCT_SRC) $(AAC_WINDOW_SRC) $(AAC_BANDS_SRC) $(AAC_PNS_SRC) $(AAC_TNS_SRC) $(AAC_HEADERS_ASM) $(AAC_HUFFMAN_ASM) $(AAC_SPECTRAL_ASM) $(AAC_DSP_ASM) $(AAC_IMDCT_ASM) $(AAC_DEQUANT_ASM) -lm -no-pie

.PHONY: test-aac-native-adapter
test-aac-native-adapter: $(OBJDIR)/test_decoder_aac_native
	./$(OBJDIR)/test_decoder_aac_native

# Runs all native AAC modules without requiring libav*/FFmpeg.
.PHONY: test-aac-native
test-aac-native: test-aac-headers test-aac-syntax test-aac-sbr-grid test-aac-sbr-master test-aac-sbr-master-log test-aac-sbr-limits test-aac-sbr-tables test-aac-sbr-control test-aac-sbr-huffman test-aac-sbr-quant test-aac-sbr-energy test-aac-sbr-coupled test-aac-sbr-patch test-aac-sbr-hf test-aac-sbr-hf-predict test-aac-sbr-lpc test-aac-sbr-hf-adjust test-aac-sbr-hf-limiter test-aac-sbr-limiter-bands test-aac-sbr-hf-noise test-aac-sbr-hf-sine test-aac-sbr-hf-joint test-aac-sbr-smooth test-aac-sbr-hf-joint-smooth test-aac-sbr-noise-rom test-aac-sbr-hf-joint-rom test-aac-sbr-qmf-synth-window test-aac-sbr-qmf-synth-mod test-aac-sbr-qmf-analysis test-aac-sbr-qmf-frame test-aac-sbr-payload test-aac-sbr-adts-mono test-aac-sbr-adts-qmf test-aac-he-probe-smoke test-aac-lc-sections test-aac-huffman test-aac-spectral test-aac-tables test-aac-scalefactors test-aac-spectral-data test-aac-dequant test-aac-imdct test-aac-window test-aac-lc-synth test-aac-lc-frame test-aac-pns test-aac-tns test-aac-cpe test-aac-stereo-strict test-aac-native-adapter

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

$(OBJDIR)/sauti_window: third_party/dr_libs/dr_flac.h third_party/stb/stb_vorbis.c third_party/tml/tml.h src/decoders/modern/sauti_decoder_opus.c src/decoders/modern/sauti_decoder_vorbis.c src/decoders/modern/sauti_decoder_midi.c platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c platforms/linux/sauti_mp3_sdl_player.h include/sauti.h include/sauti_playlist.h include/sauti_audio_backend.h $(PORTABLE_SRC) $(AAC_PLAYER_SRC) $(ASMOBJ) $(AAC_PLAYER_ASM) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(YM_ASM) $(YM_ARCHIVE_SRC) include/sauti_ym_archive.h $(SNDH_OBJECTS) $(WAVPACK_SRC)
	$(CC) $(CFLAGS) $(FFMPEG_FLAGS) $(OPUS_FLAGS) $(ARCHIVE_FLAGS) $(XMP_FLAGS) $(WAVPACK_FLAGS) $(SNDH_CFLAGS) -Iinclude -Iplatforms/linux $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_mixer) \
	  -o $@ platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c $(PORTABLE_SRC) $(AAC_PLAYER_SRC) $(ASMOBJ) $(AAC_PLAYER_ASM) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(YM_ASM) $(YM_ARCHIVE_SRC) $(SNDH_OBJECTS) $(WAVPACK_SRC) \
	  $(shell $(PKG_CONFIG) --libs sdl2 SDL2_mixer) $(OPUS_LIBS) $(FFMPEG_LIBS) $(ARCHIVE_LIBS) $(XMP_LIBS) $(WAVPACK_LIBS) $(SNDH_LINK) -lm -no-pie

$(OBJDIR)/sauti_window_pw: third_party/dr_libs/dr_flac.h third_party/stb/stb_vorbis.c third_party/tml/tml.h src/decoders/modern/sauti_decoder_opus.c src/decoders/modern/sauti_decoder_vorbis.c src/decoders/modern/sauti_decoder_midi.c platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c platforms/linux/sauti_mp3_sdl_player.h platforms/linux/pipewire_capture.c platforms/linux/pipewire_capture.h include/sauti.h include/sauti_playlist.h include/sauti_audio_backend.h $(PORTABLE_SRC) $(AAC_PLAYER_SRC) $(ASMOBJ) $(AAC_PLAYER_ASM) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(YM_ASM) $(YM_ARCHIVE_SRC) include/sauti_ym_archive.h $(SNDH_OBJECTS) $(WAVPACK_SRC)
	$(CC) $(CFLAGS) $(FFMPEG_FLAGS) $(OPUS_FLAGS) $(ARCHIVE_FLAGS) $(XMP_FLAGS) $(WAVPACK_FLAGS) $(SNDH_CFLAGS) -D_GNU_SOURCE -DSAUTI_HAS_PIPEWIRE -Iinclude -Iplatforms/linux \
	  $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_mixer libpipewire-0.3) -o $@ \
	  platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c platforms/linux/pipewire_capture.c $(PORTABLE_SRC) $(AAC_PLAYER_SRC) $(ASMOBJ) $(AAC_PLAYER_ASM) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(YM_ASM) $(YM_ARCHIVE_SRC) $(SNDH_OBJECTS) $(WAVPACK_SRC) \
	  $(shell $(PKG_CONFIG) --libs sdl2 SDL2_mixer libpipewire-0.3) $(OPUS_LIBS) $(FFMPEG_LIBS) $(ARCHIVE_LIBS) $(XMP_LIBS) $(WAVPACK_LIBS) $(SNDH_LINK) -lm -no-pie

$(OBJDIR)/sauti_window_pw_reference: third_party/dr_libs/dr_flac.h third_party/stb/stb_vorbis.c third_party/tml/tml.h src/decoders/modern/sauti_decoder_opus.c src/decoders/modern/sauti_decoder_vorbis.c src/decoders/modern/sauti_decoder_midi.c platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c platforms/linux/sauti_mp3_sdl_player.h platforms/linux/pipewire_capture.c platforms/linux/pipewire_capture.h include/sauti.h include/sauti_playlist.h include/sauti_audio_backend.h $(PORTABLE_SRC) $(AAC_PLAYER_SRC) $(ASMOBJ) $(AAC_PLAYER_ASM) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(OBJDIR)/sauti_decoder_ym_reference_dac.o $(YM_ARCHIVE_SRC) include/sauti_ym_archive.h
	$(CC) $(CFLAGS) $(FFMPEG_FLAGS) -DSAUTI_YM_LISTEN_REFERENCE $(OPUS_FLAGS) $(ARCHIVE_FLAGS) $(XMP_FLAGS) -D_GNU_SOURCE -DSAUTI_HAS_PIPEWIRE -Iinclude -Iplatforms/linux \
	  $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_mixer libpipewire-0.3) -o $@ \
	  platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c platforms/linux/pipewire_capture.c $(PORTABLE_SRC) $(AAC_PLAYER_SRC) $(ASMOBJ) $(AAC_PLAYER_ASM) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(OBJDIR)/sauti_decoder_ym_reference_dac.o $(YM_ARCHIVE_SRC) \
	  $(shell $(PKG_CONFIG) --libs sdl2 SDL2_mixer libpipewire-0.3) $(OPUS_LIBS) $(FFMPEG_LIBS) $(ARCHIVE_LIBS) $(XMP_LIBS) -lm -no-pie

.PHONY: window-pipewire-reference
window-pipewire-reference: $(OBJDIR)/sauti_window_pw_reference

$(OBJDIR)/sauti_window_pw_pre_tick: third_party/dr_libs/dr_flac.h third_party/stb/stb_vorbis.c third_party/tml/tml.h src/decoders/modern/sauti_decoder_opus.c src/decoders/modern/sauti_decoder_vorbis.c src/decoders/modern/sauti_decoder_midi.c platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c platforms/linux/sauti_mp3_sdl_player.h platforms/linux/pipewire_capture.c platforms/linux/pipewire_capture.h include/sauti.h include/sauti_playlist.h include/sauti_audio_backend.h $(PORTABLE_SRC) $(AAC_PLAYER_SRC) $(ASMOBJ) $(AAC_PLAYER_ASM) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(OBJDIR)/sauti_decoder_ym_env_pre_tick.o $(YM_ARCHIVE_SRC) include/sauti_ym_archive.h
	$(CC) $(CFLAGS) $(FFMPEG_FLAGS) -DSAUTI_YM_LISTEN_REFERENCE $(OPUS_FLAGS) $(ARCHIVE_FLAGS) $(XMP_FLAGS) -D_GNU_SOURCE -DSAUTI_HAS_PIPEWIRE -Iinclude -Iplatforms/linux \
	  $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_mixer libpipewire-0.3) -o $@ \
	  platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c platforms/linux/pipewire_capture.c $(PORTABLE_SRC) $(AAC_PLAYER_SRC) $(ASMOBJ) $(AAC_PLAYER_ASM) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(OBJDIR)/sauti_decoder_ym_env_pre_tick.o $(YM_ARCHIVE_SRC) \
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
