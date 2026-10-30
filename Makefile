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

DTM_SRC := src/decoders/retro/atari/sauti_decoder_dtm.c
AVR_SRC := src/decoders/retro/atari/sauti_decoder_avr.c
DVSM_SRC := src/decoders/retro/atari/sauti_decoder_dvsm.c
PORTABLE_SRC := src/sauti_playlist.c src/sauti_transport.c src/sauti_app_state.c src/sauti_library.c src/sauti_wav.c src/sauti_record_ring.c src/sauti_audio_backend.c src/sauti_playback_time.c src/sauti_playback_backend.c src/sauti_decoder.c src/decoders/modern/sauti_decoder_wav.c src/decoders/modern/sauti_decoder_mp3.c src/decoders/modern/sauti_decoder_vorbis.c src/decoders/modern/sauti_decoder_midi.c src/decoders/modern/sauti_decoder_opus.c src/decoders/modern/sauti_decoder_flac.c src/sauti_pcm_output.c $(AVR_SRC) $(DVSM_SRC) $(DTM_SRC)
AVR_ASM := $(OBJDIR)/sauti_decoder_avr.o
DVSM_ASM := $(OBJDIR)/sauti_decoder_dvsm.o
YM_SRC := src/decoders/retro/atari/sauti_decoder_ym.c
YM_ARCHIVE_SRC := src/decoders/retro/atari/sauti_ym_archive.c
YM_ASM := $(OBJDIR)/sauti_decoder_ym.o

ASMOBJ := $(OBJDIR)/sauti_core.o $(OBJDIR)/sauti_fft.o $(OBJDIR)/sauti_spectrum.o $(OBJDIR)/sauti_frame.o $(OBJDIR)/sauti_db_log.o $(OBJDIR)/sauti_audio_ring.o $(OBJDIR)/sauti_stereo_ring.o $(OBJDIR)/sauti_stereo_analysis.o $(OBJDIR)/sauti_stereo_bars.o $(OBJDIR)/sauti_peak_hold.o $(OBJDIR)/sauti_levels.o

.PHONY: all test demo window window-pipewire run-wayland run-pipewire-mic run-pipewire-monitor smoke-headless smoke-headless-pipewire clean
all: $(OBJDIR)/test_core $(OBJDIR)/test_fft $(OBJDIR)/test_frame $(OBJDIR)/test_db_log $(OBJDIR)/test_audio_ring $(OBJDIR)/test_stereo_ring $(OBJDIR)/test_stereo_analysis $(OBJDIR)/test_stereo_bars $(OBJDIR)/test_peak_hold $(OBJDIR)/test_auto_gain $(OBJDIR)/test_playlist $(OBJDIR)/test_transport $(OBJDIR)/test_app_state $(OBJDIR)/test_library $(OBJDIR)/test_wav $(OBJDIR)/test_record_ring $(OBJDIR)/test_audio_backend $(OBJDIR)/test_playback_time $(OBJDIR)/test_playback_backend $(OBJDIR)/test_decoder $(OBJDIR)/test_decoder_wav $(OBJDIR)/test_decoder_avr $(OBJDIR)/test_decoder_dvsm $(OBJDIR)/test_decoder_ym $(OBJDIR)/test_ym_archive $(OBJDIR)/test_decoder_mp3 $(OBJDIR)/test_pcm_output $(OBJDIR)/test_decoder_vorbis $(OBJDIR)/test_decoder_midi $(OBJDIR)/test_decoder_opus $(OBJDIR)/sauti_demo

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
	$(CC) $(CFLAGS) $(OPUS_FLAGS) -Iinclude -o $@ tests/test_playlist.c $(PORTABLE_SRC) $(AVR_ASM) $(DVSM_ASM) $(OPUS_LIBS) -lm -no-pie

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

$(OBJDIR)/sauti_window: third_party/dr_libs/dr_flac.h third_party/stb/stb_vorbis.c third_party/tml/tml.h src/decoders/modern/sauti_decoder_opus.c src/decoders/modern/sauti_decoder_vorbis.c src/decoders/modern/sauti_decoder_midi.c platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c platforms/linux/sauti_mp3_sdl_player.h include/sauti.h include/sauti_playlist.h include/sauti_audio_backend.h $(PORTABLE_SRC) $(ASMOBJ) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(YM_ASM) $(YM_ARCHIVE_SRC) include/sauti_ym_archive.h $(SNDH_OBJECTS) $(WAVPACK_SRC)
	$(CC) $(CFLAGS) $(OPUS_FLAGS) $(ARCHIVE_FLAGS) $(XMP_FLAGS) $(WAVPACK_FLAGS) $(SNDH_CFLAGS) -Iinclude -Iplatforms/linux $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_mixer) \
	  -o $@ platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c $(PORTABLE_SRC) $(ASMOBJ) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(YM_ASM) $(YM_ARCHIVE_SRC) $(SNDH_OBJECTS) $(WAVPACK_SRC) \
	  $(shell $(PKG_CONFIG) --libs sdl2 SDL2_mixer) $(OPUS_LIBS) $(ARCHIVE_LIBS) $(XMP_LIBS) $(WAVPACK_LIBS) $(SNDH_LINK) -lm -no-pie

$(OBJDIR)/sauti_window_pw: third_party/dr_libs/dr_flac.h third_party/stb/stb_vorbis.c third_party/tml/tml.h src/decoders/modern/sauti_decoder_opus.c src/decoders/modern/sauti_decoder_vorbis.c src/decoders/modern/sauti_decoder_midi.c platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c platforms/linux/sauti_mp3_sdl_player.h platforms/linux/pipewire_capture.c platforms/linux/pipewire_capture.h include/sauti.h include/sauti_playlist.h include/sauti_audio_backend.h $(PORTABLE_SRC) $(ASMOBJ) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(YM_ASM) $(YM_ARCHIVE_SRC) include/sauti_ym_archive.h $(SNDH_OBJECTS) $(WAVPACK_SRC)
	$(CC) $(CFLAGS) $(OPUS_FLAGS) $(ARCHIVE_FLAGS) $(XMP_FLAGS) $(WAVPACK_FLAGS) $(SNDH_CFLAGS) -D_GNU_SOURCE -DSAUTI_HAS_PIPEWIRE -Iinclude -Iplatforms/linux \
	  $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_mixer libpipewire-0.3) -o $@ \
	  platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c platforms/linux/pipewire_capture.c $(PORTABLE_SRC) $(ASMOBJ) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(YM_ASM) $(YM_ARCHIVE_SRC) $(SNDH_OBJECTS) $(WAVPACK_SRC) \
	  $(shell $(PKG_CONFIG) --libs sdl2 SDL2_mixer libpipewire-0.3) $(OPUS_LIBS) $(ARCHIVE_LIBS) $(XMP_LIBS) $(WAVPACK_LIBS) $(SNDH_LINK) -lm -no-pie

$(OBJDIR)/sauti_window_pw_reference: third_party/dr_libs/dr_flac.h third_party/stb/stb_vorbis.c third_party/tml/tml.h src/decoders/modern/sauti_decoder_opus.c src/decoders/modern/sauti_decoder_vorbis.c src/decoders/modern/sauti_decoder_midi.c platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c platforms/linux/sauti_mp3_sdl_player.h platforms/linux/pipewire_capture.c platforms/linux/pipewire_capture.h include/sauti.h include/sauti_playlist.h include/sauti_audio_backend.h $(PORTABLE_SRC) $(ASMOBJ) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(OBJDIR)/sauti_decoder_ym_reference_dac.o $(YM_ARCHIVE_SRC) include/sauti_ym_archive.h
	$(CC) $(CFLAGS) -DSAUTI_YM_LISTEN_REFERENCE $(OPUS_FLAGS) $(ARCHIVE_FLAGS) $(XMP_FLAGS) -D_GNU_SOURCE -DSAUTI_HAS_PIPEWIRE -Iinclude -Iplatforms/linux \
	  $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_mixer libpipewire-0.3) -o $@ \
	  platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c platforms/linux/pipewire_capture.c $(PORTABLE_SRC) $(ASMOBJ) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(OBJDIR)/sauti_decoder_ym_reference_dac.o $(YM_ARCHIVE_SRC) \
	  $(shell $(PKG_CONFIG) --libs sdl2 SDL2_mixer libpipewire-0.3) $(OPUS_LIBS) $(ARCHIVE_LIBS) $(XMP_LIBS) -lm -no-pie

.PHONY: window-pipewire-reference
window-pipewire-reference: $(OBJDIR)/sauti_window_pw_reference

$(OBJDIR)/sauti_window_pw_pre_tick: third_party/dr_libs/dr_flac.h third_party/stb/stb_vorbis.c third_party/tml/tml.h src/decoders/modern/sauti_decoder_opus.c src/decoders/modern/sauti_decoder_vorbis.c src/decoders/modern/sauti_decoder_midi.c platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c platforms/linux/sauti_mp3_sdl_player.h platforms/linux/pipewire_capture.c platforms/linux/pipewire_capture.h include/sauti.h include/sauti_playlist.h include/sauti_audio_backend.h $(PORTABLE_SRC) $(ASMOBJ) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(OBJDIR)/sauti_decoder_ym_env_pre_tick.o $(YM_ARCHIVE_SRC) include/sauti_ym_archive.h
	$(CC) $(CFLAGS) -DSAUTI_YM_LISTEN_REFERENCE $(OPUS_FLAGS) $(ARCHIVE_FLAGS) $(XMP_FLAGS) -D_GNU_SOURCE -DSAUTI_HAS_PIPEWIRE -Iinclude -Iplatforms/linux \
	  $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_mixer libpipewire-0.3) -o $@ \
	  platforms/linux/sdl2_wayland.c platforms/linux/sauti_mp3_sdl_player.c platforms/linux/pipewire_capture.c $(PORTABLE_SRC) $(ASMOBJ) $(AVR_ASM) $(DVSM_ASM) $(YM_SRC) $(OBJDIR)/sauti_decoder_ym_env_pre_tick.o $(YM_ARCHIVE_SRC) \
	  $(shell $(PKG_CONFIG) --libs sdl2 SDL2_mixer libpipewire-0.3) $(OPUS_LIBS) $(ARCHIVE_LIBS) $(XMP_LIBS) -lm -no-pie

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

test: $(OBJDIR)/test_decoder $(OBJDIR)/test_decoder_wav $(OBJDIR)/test_decoder_mp3 $(OBJDIR)/test_pcm_output $(OBJDIR)/test_decoder_vorbis $(OBJDIR)/test_playback_backend $(OBJDIR)/test_playback_time $(OBJDIR)/test_audio_backend $(OBJDIR)/test_record_ring $(OBJDIR)/test_wav $(OBJDIR)/test_library $(OBJDIR)/test_app_state $(OBJDIR)/test_transport $(OBJDIR)/test_playlist $(OBJDIR)/test_core $(OBJDIR)/test_fft $(OBJDIR)/test_frame $(OBJDIR)/test_db_log $(OBJDIR)/test_audio_ring $(OBJDIR)/test_stereo_ring $(OBJDIR)/test_stereo_analysis $(OBJDIR)/test_stereo_bars $(OBJDIR)/test_peak_hold $(OBJDIR)/test_auto_gain $(OBJDIR)/test_decoder_midi $(OBJDIR)/test_decoder_opus $(OBJDIR)/test_decoder_flac $(OBJDIR)/test_decoder_avr $(OBJDIR)/test_decoder_dvsm $(OBJDIR)/test_decoder_ym
	./$(OBJDIR)/test_decoder
	./$(OBJDIR)/test_decoder_wav
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
