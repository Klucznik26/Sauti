#!/usr/bin/env python3
"""SBR-54z: opt-in single-QMF-context selection for disposable instrumented FFmpeg.

The observed run showed call 0 has a distinct history pointer; calls 1..47
share one pointer and alternate W buf_idx. Thus parity selection was invalid.
In strict trace mode skip call 0 and require every subsequent QMF invocation
to use exactly the same history pointer. Fail closed on a context change.
No production Sauti changes; do not use this patch for general audio decoding.
"""
import argparse
from pathlib import Path

MARKER="/* SBR-54z stereo selection: even-numbered QMF calls only. */"
TRACE="/* SBR-54z QMF call/state diagnostic, read-only. */"
OLD='''        sbr54w_selected = ((sbr54w_call++ & 1u) == 0u);'''
NEW='''        /* Observed fixture: call 0 was a different short-lived context.
         * Subsequent calls all used one continuous x history. */
        static INTFLOAT *sbr54z_expected_history;
        if (sbr54w_call++ == 0) {
            sbr54w_selected = 0;
            fprintf(stderr, "SBR-54z: skipping one-time QMF warmup/context call\\n");
        } else {
            if (!sbr54z_expected_history)
                sbr54z_expected_history = x;
            if (x != sbr54z_expected_history) {
                fprintf(stderr,
                        "SBR-54z: QMF history context changed; refusing mixed-context export\\n");
                abort();
            }
            sbr54w_selected = 1;
        }'''
UPGRADE="SBR-54z: skipping one-time QMF warmup/context call"

def patch(text):
    if MARKER not in text or TRACE not in text:
        raise ValueError("expected stereo-selection AND trace patches not found")
    if text.count(OLD)!=1 or UPGRADE in text:
        raise ValueError("unsupported or already upgraded selector")
    return text.replace(OLD, NEW, 1)

def self_test():
    base=MARKER+"\n"+TRACE+"\n"+OLD
    updated=patch(base)
    assert "x != sbr54z_expected_history" in updated
    assert "sbr54w_selected = 0" in updated
    assert "sbr54w_selected = 1" in updated
    try:
        patch(updated)
    except ValueError:
        pass
    else:
        raise AssertionError("duplicate patch accepted")
    print("PASS SBR-54z QMF context selection patch (synthetic anchors)")

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("checkout",nargs="?")
    p.add_argument("--self-test",action="store_true")
    p.add_argument("--dry-run",action="store_true")
    args=p.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.checkout:
        p.error("disposable FFmpeg checkout path required")
    path=Path(args.checkout)/"libavcodec"/"aacsbr_template.c"
    try:
        src=path.read_text(encoding="utf-8")
        result=patch(src)
        if not args.dry_run:
            path.write_text(result,encoding="utf-8")
    except (OSError,ValueError) as exc:
        p.exit(2,f"SBR-54z context selector rejected: {exc}\n")
    print("SBR-54z context selector anchors PASS" if args.dry_run
          else f"SBR-54z context selector installed: {path}")

if __name__=="__main__":
    main()
