#!/usr/bin/env python3
"""SBR-54z diagnostic: trace FFmpeg's ACTUAL QMF call order and state addresses.

Patch only already-instrumented disposable FFmpeg checkout. Records first
96 calls in stderr with independent invocation index, selected flag, x
(history storage) pointer, W pointer, buf_idx and history head/tail.
No modification of decode logic, channel selection, data or normal output.
"""
import argparse
from pathlib import Path

BASE = "/* SBR-54z stereo selection: even-numbered QMF calls only. */"
TRACE = "/* SBR-54z QMF call/state diagnostic, read-only. */"
ANCHOR = "    if (sbr54w_core_path && sbr54w_selected && !sbr54w_exhausted) {"
ADD = """    /* SBR-54z QMF call/state diagnostic, read-only. */
    static unsigned long long sbr54z_trace_call;
    if (sbr54w_core_path && sbr54z_trace_call < 96) {
        fprintf(stderr,
                "SBR54Z_CALL=%llu selected=%d x=%p W=%p buf_idx=%d "
                "history_head=%g history_tail=%g\\n",
                sbr54z_trace_call, sbr54w_selected, (void*)x, (void*)W,
                buf_idx, (double)x[0], (double)x[287]);
    }
    if (sbr54w_core_path)
        sbr54z_trace_call++;
"""

def patch(source):
    if BASE not in source or source.count(ANCHOR)!=1:
        raise ValueError("expected stereo-selection patch not found")
    if TRACE in source:
        raise ValueError("trace already installed")
    return source.replace(ANCHOR, ADD + ANCHOR, 1)


def self_test():
    sample=BASE+"\n"+ANCHOR
    patched=patch(sample)
    assert TRACE in patched and "SBR54Z_CALL" in patched
    try:
        patch(patched)
    except ValueError:
        pass
    else:
        raise AssertionError("accepted duplicate")
    try:
        patch("plain FFmpeg")
    except ValueError:
        pass
    else:
        raise AssertionError("accepted uninstrumented source")
    print("PASS SBR-54z read-only FFmpeg call trace patch (synthetic anchors)")


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("checkout",nargs="?")
    p.add_argument("--self-test",action="store_true")
    p.add_argument("--dry-run",action="store_true")
    a=p.parse_args()
    if a.self_test:
        self_test()
        return
    if not a.checkout:
        p.error("required: path to disposable FFmpeg checkout")
    path=Path(a.checkout)/"libavcodec"/"aacsbr_template.c"
    try:
        source=path.read_text(encoding="utf-8")
        updated=patch(source)
        if not a.dry_run:
            path.write_text(updated,encoding="utf-8")
    except (OSError,ValueError) as exc:
        p.exit(2,f"SBR-54z trace patch rejected: {exc}\n")
    print("SBR-54z diagnostic anchors PASS (dry-run)" if a.dry_run
          else f"SBR-54z trace installed: {path}")


if __name__=="__main__":
    main()
