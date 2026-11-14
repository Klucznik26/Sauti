#!/usr/bin/env python3
"""SBR-73: opt-in f_indexsine trace in a DISPOSABLE FFmpeg float checkout.

Patches only libavcodec/aacsbr.c; never touches installed FFmpeg or Sauti.
The resulting FFmpeg MUST be treated as a developer-only instrumented binary.
It logs raw channel-local HF assembly slots: call, envelope, time-slot index,
phase BEFORE increment, kx, m_max, sinusoidal amplitude at QMF k18 and k42.
The call number is NOT assumed to equal Sauti's frame number.
"""
import argparse
import re
from pathlib import Path

MARKER = "/* SBR-73 FFmpeg f_indexsine read-only trace. */"
INDEX_ANCHOR = re.compile(r"(?m)^(?P<indent>\s*)int indexsine\s*=\s*ch_data->f_indexsine\s*;\s*$")
SLOT_ANCHOR = re.compile(
    r"(?m)^[ \t]*LOCAL_ALIGNED_16\(\s*float\s*,\s*g_filt_tab\s*,\s*\[48\]\s*\)\s*;\s*$")
FUNCTION = "static void sbr_hf_assemble("

FUNCTION_ADD = r'''    /* SBR-73 FFmpeg f_indexsine read-only trace. */
    static FILE *sbr73_phase_log;
    static unsigned long long sbr73_assemble_calls;
    const char *sbr73_filename = getenv("SAUTI_SBR73_FFMPEG_PHASE");
    const unsigned long long sbr73_call = sbr73_assemble_calls++;
    if (sbr73_filename && !sbr73_phase_log) {
        sbr73_phase_log = fopen(sbr73_filename, "wb");
        if (!sbr73_phase_log) { perror(sbr73_filename); abort(); }
        if (fprintf(sbr73_phase_log, "call,env,slot,indexsine,kx,mmax,sine18,sine42\n") < 0)
            abort();
    }
'''

SLOT_ADD = r'''            /* SBR-73: record the oscillator BEFORE this slot's increment.
             * Do not modify any envelope, noise/gain or indexsine state. */
            if (sbr73_phase_log) {
                const float sbr73_s18 =
                    (kx <= 18 && 18 < kx + m_max) ?
                    sbr->s_m[e][18 - kx] : 0.0f;
                const float sbr73_s42 =
                    (kx <= 42 && 42 < kx + m_max) ?
                    sbr->s_m[e][42 - kx] : 0.0f;
                if (fprintf(sbr73_phase_log,
                            "%llu,%d,%d,%d,%d,%d,%.9g,%.9g\n",
                            sbr73_call, e, i, indexsine, kx, m_max,
                            (double)sbr73_s18, (double)sbr73_s42) < 0 ||
                    fflush(sbr73_phase_log) != 0)
                    abort();
            }
'''


def patch(text):
    if MARKER in text:
        raise ValueError("SBR-73 already instrumented")
    if text.count(FUNCTION) != 1:
        raise ValueError("expected exactly one float sbr_hf_assemble")
    if len(SLOT_ANCHOR.findall(text)) != 1:
        raise ValueError("expected exactly one HF assembly slot anchor")
    if len(INDEX_ANCHOR.findall(text)) != 1:
        raise ValueError("expected exactly one channel-local f_indexsine anchor")
    fn_start = text.index(FUNCTION)
    fn_end = text.find("\n}\n", fn_start)
    if fn_end < 0:
        raise ValueError("cannot find end of float HF assembly function")
    section = text[fn_start:fn_end]
    if len(SLOT_ANCHOR.findall(section)) != 1 or not INDEX_ANCHOR.search(section):
        raise ValueError("FFmpeg aacsbr.c layout changed; no patch applied")
    if "indexsine = (indexsine + 1) & 3;" not in section:
        raise ValueError("unknown FFmpeg oscillator clock")
    section = INDEX_ANCHOR.sub(
        lambda m: m.group(0) + "\n" + FUNCTION_ADD,
        section, count=1)
    section = SLOT_ANCHOR.sub(lambda m: SLOT_ADD + m.group(0), section, count=1)
    return ("#include <stdio.h>\n#include <stdlib.h>\n" +
            text[:fn_start] + section + text[fn_end:])


def self_test():
    sample = """#include <math.h>
static void sbr_hf_assemble(float Y1[38][64][2],
                            const float X_high[64][40][2],
                            SpectralBandReplication *sbr, SBRData *ch_data,
                            const int e_a[2])
{
    int indexsine = ch_data->f_indexsine;
    for (e = 0; e < ch_data->bs_num_env; e++) {
        for (i = 2 * ch_data->t_env[e]; i < 2 * ch_data->t_env[e+1]; i++) {
        LOCAL_ALIGNED_16(float, g_filt_tab, [48]);
        indexsine = (indexsine + 1) & 3;
        }
    }
}
"""
    result = patch(sample)
    assert result.count(MARKER) == 1
    assert result.count("SBR-73: record the oscillator") == 1
    assert "SAUTI_SBR73_FFMPEG_PHASE" in result
    assert "sbr->s_m[e][18 - kx]" in result
    assert "sbr->s_m[e][42 - kx]" in result
    assert result.count("indexsine = (indexsine + 1) & 3;") == 1
    for invalid in (result, sample.replace("f_indexsine", "bad_index"),
                    sample.replace("LOCAL_ALIGNED_16(float, g_filt_tab, [48]);", "unrecognized slot")):
        try:
            patch(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError("unsafe/duplicate FFmpeg patch accepted")
    print("PASS SBR-73 FFmpeg indexsine tracer: exact source anchors, fail-closed")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("ffmpeg_checkout", nargs="?")
    p.add_argument("--self-test", action="store_true")
    p.add_argument("--dry-run", action="store_true")
    args = p.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not args.ffmpeg_checkout:
        p.error("supply path of disposable FFmpeg source checkout")
    source = Path(args.ffmpeg_checkout) / "libavcodec" / "aacsbr.c"
    try:
        original = source.read_text(encoding="utf-8")
        updated = patch(original)
        if not args.dry_run:
            source.write_text(updated, encoding="utf-8")
    except (OSError, ValueError) as exc:
        p.exit(2, f"SBR-73 refused FFmpeg instrumentation: {exc}\n")
    print("PASS SBR-73 FFmpeg source anchors" if args.dry_run else
          f"SBR-73 instrumented ONLY disposable FFmpeg source: {source}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
