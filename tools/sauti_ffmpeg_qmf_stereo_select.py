#!/usr/bin/env python3
"""SBR-54z: opt-in stereo channel selection in an ALREADY patched FFmpeg.

The old diagnostic patch consumed one shared core frame per invocation
of sbr_qmf_analysis(). Stereo HE-AAC invokes it for left and right.
This upgrade selects even-numbered (left) calls; unselected right-channel
calls retain the original FFmpeg input and per-channel filter state.
STRICT: only for the locally instrumented FFmpeg source version with
exact anchored blocks. Always review diff and rebuild separately.
"""
import argparse
from pathlib import Path

MARKER="/* SBR-54w instrumented FFmpeg float QMF: diagnostic only. */"
OLD="    static unsigned long long sbr54w_frame;\n    static int sbr54w_exhausted;"
NEW="""    static unsigned long long sbr54w_frame;
    static unsigned long long sbr54w_call;
    static int sbr54w_exhausted;"""
ANCHOR="""    if (sbr54w_core_path && !sbr54w_exhausted) {"""
SELECTOR="""    /* Two QMF calls per AAC stereo frame in the verified test fixture:
     * left then right. Opt-in only; default one call per mono frame.
     * The second/right channel is decoded normally, never exported.
     * Single-threaded measurement required: static counters are not
     * safe for concurrent use or unrelated codec streams. */
    int sbr54w_selected = 1;
    const char *sbr54w_channels = getenv("SAUTI_SBR54W_CHANNELS");
    if (sbr54w_core_path && sbr54w_channels) {
        if (strcmp(sbr54w_channels, "2") != 0) {
            fprintf(stderr, "SBR-54z: supported channel count is 2 only\\n");
            abort();
        }
        sbr54w_selected = ((sbr54w_call++ & 1u) == 0u);
    }
    if (sbr54w_core_path && sbr54w_selected && !sbr54w_exhausted) {"""
UPGRADE_MARKER="/* SBR-54z stereo selection: even-numbered QMF calls only. */"


def patch(src):
    if MARKER not in src:
        raise ValueError("not an instrumented FFmpeg QMF checkout")
    if UPGRADE_MARKER in src:
        raise ValueError("stereo patch already applied")
    if src.count(OLD)!=1 or src.count(ANCHOR)!=1:
        raise ValueError("unexpected QMF patch layout; refusing change")
    if src.count("    if (sbr54w_active) {")!=1:
        raise ValueError("missing dedicated export guard")
    src=src.replace(OLD,NEW,1)
    src=src.replace(ANCHOR,"    "+UPGRADE_MARKER+"\n"+SELECTOR,1)
    return src


def self_test():
    source=MARKER+"\n"+OLD+"\n"+ANCHOR+"\n    if (sbr54w_active) {\n"
    target=patch(source)
    assert "sbr54w_call++" in target
    assert "sbr54w_selected && !sbr54w_exhausted" in target
    assert "strcmp(sbr54w_channels, \"2\")" in target
    try:
        patch(target)
    except ValueError:
        pass
    else:
        raise AssertionError("duplicate patch accepted")
    try:
        patch("wrong")
    except ValueError:
        pass
    else:
        raise AssertionError("unexpected input accepted")
    print("PASS SBR-54z stereo-channel QMF selection patch (synthetic anchors ONLY)")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ffmpeg_checkout",nargs="?")
    parser.add_argument("--dry-run",action="store_true")
    parser.add_argument("--self-test",action="store_true")
    args=parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.ffmpeg_checkout:
        parser.error("provide path to disposable, previously instrumented FFmpeg checkout")
    source=Path(args.ffmpeg_checkout)/"libavcodec/aacsbr_template.c"
    try:
        raw=source.read_text(encoding="utf-8")
        updated=patch(raw)
        if not args.dry_run:
            source.write_text(updated,encoding="utf-8")
    except (OSError,ValueError) as exc:
        parser.exit(2,f"SBR-54z stereo patch rejected: {exc}\n")
    print("SBR-54z stereo patch anchors PASS (no write)" if args.dry_run
          else f"SBR-54z stereo patch applied to {source}")


if __name__=="__main__":
    main()
