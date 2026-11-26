# Atari8 research core

Experimental independent POKEY model: four pure-tone channels, volume-only,
28/114 master-clock divisors, fast clocks on channels 1/3, and steady-state
16-bit linked periods on channels 2/4. For the latter, the low-channel
oscillator is not modeled: audible oscillator writes are rejected, while
volume-only is allowed. Slow periods are N+1 selected ticks, fast periods
are N+4 (8-bit) or N+7 (16-bit) master cycles. Counter capacity includes
65536 slow ticks and 65542 fast cycles. STIMER, initialization and AUDCTL
mode transitions are simplified; exact low-counter events,
IRQs, analog output, PCM and CPU emulation are not implemented.
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

No production build or decoder registry changes in this milestone.
