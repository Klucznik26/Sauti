# Atari8 research core

Experimental independent POKEY model: four pure-tone channels, volume-only,
28/114 master-clock divisors, fast clocks on channels 1/3, and steady-state
16-bit linked periods on channels 2/4. For the latter, the low-channel
oscillator is not modeled: audible oscillator writes are rejected, while
volume-only is allowed. Slow periods are N+1 selected ticks, fast periods
are N+4 (8-bit) or N+7 (16-bit) master cycles. Counter capacity includes
65536 slow ticks and 65542 fast cycles. STIMER, initialization and AUDCTL
mode transitions are simplified; exact low-counter events,
analog output and CPU emulation are not implemented.
IRQEN/IRQST now model latched timer 1/2/4 interrupts, active-low status,
selective acknowledgment by disabling sources, and a level IRQ signal.
Timer 3 does not generate an IRQ. Enabling IRQ1 with linked timers 1/2
is rejected until low-counter underflows are modeled. Other IRQ sources
(serial, keyboard and break) are not implemented and enabling them is
rejected. IRQ assertions occur at modeled timer events, not yet at exact
hardware propagation times. Tests cover masking, simultaneous pending
sources, repeated reads, re-enabling, fast and linked high timers.
High-pass XOR latches on channels 1/2 are clocked by timers 3/4.
The roughly 1.5-cycle hardware propagation is rounded to two cycles;
simultaneous events use the updated lower-channel waveform. The
combination of filter 1 and linked timers 3/4 is rejected because
linked low-timer pulses are not yet modeled. STIMER sets raw output
flip-flops to one, and channels 1/2 have inverted unfiltered polarity.
Volume-only overrides the filter. Tests cover both filters, polarity,
volume override, and the simplified propagation delay.
All AUDC waveform selections now have a digital model. Four LFSRs run
at master-clock speed with per-channel sample delays. The 9/17-bit modes
currently use separate free-running registers; hardware mode switching,
SKCTL initialization and absolute phase are not modeled. Tests check
full periods, short sequences up to cyclic rotation and waveform gating.
The sound subsystem now contains one or two POKEYs with a common
master-cycle advance and independent register, filter and timer state.
In stereo, address bit A4 selects the primary ($D200) or secondary
($D210) chip in the $D2xx page; mono mirrors all 16 registers throughout
the page. The primary chip supplies left audio and CPU IRQ; the secondary
supplies right audio and its IRQ is not routed to the CPU in the standard
stereo modification. No analog mixing or nonstandard stereo wiring is implemented. Chip limitations above apply to both instances.
The internal interface is not registered as a decoder. Tests validate
the stated subset, not hardware equivalence.

Implementation policy: original code based on hardware and format
documentation; ASAP and Altirra are external behavioral references only.
Do not incorporate third-party player routines, tables or ROM images
without a separate provenance and license review. External test music
and tools remain outside the repository until redistribution rights are
confirmed. Intended distribution remains closed-source freeware.

Technical reference: Altirra Hardware Reference Manual, POKEY chapter
(https://virtualdub.org/altirra.html). Compare subsequent milestones against
independent synthetic traces and hardware/emulator tests.

Standalone test (from repository root):

    mkdir -p build
    cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 \
      src/decoders/retro/atari8/atari8_pokey.c tests/test_atari8_pokey.c \
      -o build/test_atari8_pokey
    ./build/test_atari8_pokey

Stereo subsystem test:

    cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 \
      src/decoders/retro/atari8/atari8_pokey.c \
      src/decoders/retro/atari8/atari8_core.c tests/test_atari8_core.c \
      -o build/test_atari8_core
    ./build/test_atari8_core

Streaming PCM: atari8_pcm_feed integrates each master-cycle digital
level over the exact rational sample boundary. The rate range is
8–192 kHz, with 16-bit mono or interleaved stereo frames. A diagnostic
unipolar output is available; the listening mode includes an independent
first-order DC blocker per channel. Feed the level before advancing the
master cycle. This is a boxcar integrator, not a full anti-aliasing
resampler or a measured analog POKEY model. Tests check frame counts,
fractional cycle boundaries, stereo isolation, DC settling and generated
tones. No external source or music data is used.

    cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 \
      src/decoders/retro/atari8/atari8_pokey.c \
      src/decoders/retro/atari8/atari8_core.c \
      src/decoders/retro/atari8/atari8_pcm.c tests/test_atari8_pcm.c \
      -o build/test_atari8_pcm
    ./build/test_atari8_pcm

Build the original synthetic listening demo:

    cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 \
      src/decoders/retro/atari8/atari8_pokey.c \
      src/decoders/retro/atari8/atari8_core.c \
      src/decoders/retro/atari8/atari8_pcm.c tests/demo_atari8_wav.c \
      -o build/demo_atari8_wav
    ./build/demo_atari8_wav build/atari8-demo.wav

No production build or decoder registry changes in this milestone.
