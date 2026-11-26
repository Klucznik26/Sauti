# Atari8 research core

Experimental independent POKEY model: four pure-tone channels, volume-only,
28/114 master-clock divisors, fast clocks on channels 1/3, and steady-state
16-bit linked periods on channels 2/4. Linked low timers continue to
clock their waveforms and IRQ/filter paths: the first period is based on
the low byte, later pulses are 256 ticks apart until the pair reloads.
The fast high timer fires three modeled cycles after the final low pulse;
slow linked pulses are approximated as simultaneous on the final tick.
Slow periods are N+1 selected ticks, fast periods
are N+4 (8-bit) or N+7 (16-bit) master cycles. Counter capacity includes
65536 slow ticks and 65542 fast cycles. STIMER, initialization and AUDCTL
mode transitions and exact timer pipeline/IRQ phase are simplified;
analog output and CPU emulation are not implemented.
IRQEN/IRQST now model latched timer 1/2/4 interrupts, active-low status,
selective acknowledgment by disabling sources, and a level IRQ signal.
Timer 3 does not generate an IRQ. Timer 1 also generates IRQs when
linked to timer 2. Other IRQ sources
(serial, keyboard and break) are not implemented and enabling them is
rejected. IRQ assertions occur at modeled timer events, not yet at exact
hardware propagation times. Tests cover masking, simultaneous pending
sources, repeated reads, re-enabling, fast and linked high timers.
High-pass XOR latches on channels 1/2 are clocked by timers 3/4.
The roughly 1.5-cycle hardware propagation is rounded to two cycles;
simultaneous events use the updated lower-channel waveform. Linked
timer 3 also clocks filter 1. STIMER sets raw output
flip-flops to one, and channels 1/2 have inverted unfiltered polarity.
Volume-only overrides the filter. Tests cover both filters, polarity,
volume override, and the simplified propagation delay.
All AUDC waveform selections now have a digital model. Four LFSRs run
at master-clock speed with per-channel sample delays. The 9/17-bit modes
currently use separate registers; physical mode switching and absolute
noise phase are not modeled. SKCTL ($D20F) bits 0/1 control the modeled
initialization state: polynomial feedback is forced to one, slow clocks
stop, and fast timers keep advancing. RANDOM ($D20A read) exposes the
top eight bits of the selected long polynomial. The initial slow-clock
pulses after release are approximated at 19/78 cycles, without the
hardware timer/IRQ pipeline. Reset starts in active playback mode (3).
SKCTL bits 3–7 are rejected; keyboard, serial and potentiometer hardware
are not implemented. Tests cover initialization, slow/fast timer
behavior, RANDOM settling, and the documented 9-bit scanline sequence.
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

SAP-R experimental reader: CRLF header ending in an empty line, 9 bytes
per mono interval or 18 bytes (left then right register group) per stereo
interval. Default FASTPLAY 312 PAL / 262 NTSC, each scanline 114 cycles.
The empty-line boundary is confirmed against the public saprtools
saprplay/test.sapr file (not included or copied). Stereo group order is
a working assumption pending an external stereo fixture. Source:
https://asap.sourceforge.net/sap-format.html .
TIME is informational; payload length determines EOF, no repeat or seek.
Only one song, no executable SAP types, no arbitrary tag extensions.
Unmodeled POKEY modes are reported as unsupported, not silently rendered.
The converter is a diagnostic tool, not a registered production decoder.
No external decoder or player code is included.

NMOS 6502 milestone 1: independent CPU state, callback bus, data transfer
instructions (all official LDA/LDX/LDY/STA/STX/STY modes), transfers,
index increment/decrement, flags, conditional branches, JMP/JSR/RTS,
PHA/PHP/PLA/PLP and NOP. Each bus access counts a cycle, including
documented dummy reads for the implemented subset. Unsupported opcodes
stop explicitly. Deterministic entry is not a hardware RESET. Arithmetic and logic (ADC/SBC, ORA/AND/EOR, CMP/CPX/CPY, BIT) now include
all official addressing modes. NMOS BCD adjustment and flag behavior are
implemented; binary ADC/SBC tests exhaust 262144 input combinations and
valid BCD tests exhaust 40000 combinations, with selected NMOS flag and
invalid-digit cases. Full invalid-BCD external validation is pending.
All 151 official opcodes are now implemented, including ASL/LSR/ROL/ROR,
INC/DEC, BRK and RTI. NMOS memory RMW emits old and new bus writes.
IRQ level and NMI assertion edges are serviced at instruction boundaries;
precise interrupt polling delays, vector hijacking, hardware RESET,
undocumented opcodes and Atari bus integration remain pending.
No copied CPU code.
Reference: https://www.dusted.dk/pages/c64/C64-programming/files/6502%20Instruction%20Set.html
Tests use original synthetic instructions and bus traces. An external
functional runner accepts the unmodified 64 KiB Klaus Dormann NMOS
6502_functional_test.bin from the separate test checkout (entry $0400,
success trap $3469 for the distributed binary). External test data is
not bundled. See https://github.com/Klaus2m5/6502_65C02_functional_tests .

External validation reported by the local tester for commit 5656498:
PASS: Klaus Dormann 6502 functional test, 30646177 instructions, 96241367 cycles.
This confirms functional coverage, not exact interrupt timing.

Undocumented NMOS milestone: 84 additional opcode encodings (235/256
implemented), including SLO/RLA/SRE/RRA/DCP/ISC (42), LAX/SAX (10),
alternate NOPs (27), ANC/ALR/AXS and alternate SBC (5). All addressing
forms are covered by synthetic tests, including RMW bus writes and
indexed dummy reads. Decimal compound operations reuse ADC/SBC.
Remaining: 12 JAM encodings (currently explicit unsupported stop), ARR,
unstable immediate/load/store variants, LAS; no claim of hardware lockup
or unstable silicon behavior. Reference: https://cc65.github.io/doc/cpus.html
