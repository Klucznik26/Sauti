#!/usr/bin/env python3
"""SBR-54r: source-index provenance of the FFmpeg vs Sauti HF QMF path.

Source comparison (FFmpeg aacsbr_template.c and aacsbr.h):
  t_HFGen = 8; X_low[i] <- W[i - 8];
  ENVELOPE_ADJUSTMENT_OFFSET = 2;
  hf_g_filt on output Y[i] consumes X_high[i+2] from X_low[i+2].
So both direct LF and direct HF transposition source on output slot i
refer to W[i - 6], with a six-slot cross-frame carry.

Current Sauti SBR-53b: low[slot] <- QMF(current PCM[slot]); HF[slot]
is direct-transposed from low[slot] (per-slot source_for_target).

This is a *source-index model*, NOT a reference numerical QMF decoder.
It cannot establish sample-accurate end-to-end PCM lag or prove that
Sauti needs an HF-only +192/288-sample output shift.
"""
import argparse
from pathlib import Path

SLOTS_PER_FRAME = 32
FFMPEG_HFGEN_DELAY = 8
FFMPEG_ENVELOPE_ADJ = 2


def ffmpeg_analysis_index(frame, synthesis_slot):
    if frame < 0 or not 0 <= synthesis_slot < SLOTS_PER_FRAME:
        raise ValueError("invalid frame/slot")
    return (frame * SLOTS_PER_FRAME + synthesis_slot +
            FFMPEG_ENVELOPE_ADJ - FFMPEG_HFGEN_DELAY)


def sauti_analysis_index(frame, synthesis_slot):
    if frame < 0 or not 0 <= synthesis_slot < SLOTS_PER_FRAME:
        raise ValueError("invalid frame/slot")
    return frame * SLOTS_PER_FRAME + synthesis_slot


def label(global_index):
    if global_index < 0:
        return "prior stream/preroll"
    return (f"frame={global_index // SLOTS_PER_FRAME}, "
            f"analysis-slot={global_index % SLOTS_PER_FRAME}")


def self_test():
    # Verify the current checked-out Sauti source really uses the
    # same-slot direct-low HF transposition we are contrasting.
    # This is a source-structure guard, NOT a numerical FFmpeg test.
    repo = Path(__file__).resolve().parents[1]
    code = (repo / "src/decoders/modern/aac" /
            "sauti_aac_sbr_temporal_qmf.c").read_text(encoding="utf-8")
    assert "sauti_aac_sbr_hf_transpose(" in code
    assert "low+(size_t)slot*32u,32u,1u," in code
    assert FFMPEG_HFGEN_DELAY == 8
    assert FFMPEG_ENVELOPE_ADJ == 2
    for frame in range(1, 4):
        for slot in range(SLOTS_PER_FRAME):
            ff = ffmpeg_analysis_index(frame, slot)
            sa = sauti_analysis_index(frame, slot)
            assert sa - ff == 6
            if slot < 6:
                assert ff // 32 == frame - 1
                assert ff % 32 == 26 + slot
            else:
                assert ff // 32 == frame
                assert ff % 32 == slot - 6
    assert ffmpeg_analysis_index(1, 0) == 26
    assert ffmpeg_analysis_index(1, 5) == 31
    assert ffmpeg_analysis_index(1, 6) == 32
    assert ffmpeg_analysis_index(1, 31) == 57
    assert sauti_analysis_index(1, 0) == 32
    assert sauti_analysis_index(1, 31) == 63
    for frame, slot in [(-1, 0), (0, -1), (1, 32)]:
        try:
            ffmpeg_analysis_index(frame, slot)
        except ValueError:
            pass
        else:
            raise AssertionError("malformed source index accepted")
    print("SBR-54r: W[i-8], X_high[i+2] -> W[i-6] provenance PASS")
    print("SBR-54r: six prior-frame slots carried across boundary PASS")
    print("SBR-54r: current-slot Sauti 53b SOURCE and provenance PASS")
    print("PASS SBR-54r source-index model (NOT reference PCM/QMF)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--frame", type=int, default=1,
                        help="frame number (default 1, after warmup)")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    print("SBR-54r: FFmpeg W index = frame*32 + slot + 2 - 8")
    print("         Sauti W index  = frame*32 + slot")
    print("         NET SOURCE INDEX OFFSET = 6 x 32 core samples")
    print("         This does NOT imply a PCM delay or HF-only correction.")
    for slot in (0, 1, 2, 3, 4, 5, 6, 7, 25, 26, 31):
        ff=ffmpeg_analysis_index(args.frame, slot)
        sa=sauti_analysis_index(args.frame, slot)
        print(f"  synth-slot={slot:2d}: "
              f"FFmpeg <- {label(ff):<30} "
              f"Sauti <- {label(sa)}")


if __name__ == "__main__":
    main()
