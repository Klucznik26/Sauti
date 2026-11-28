#!/bin/bash
# Test-fixture tool: builds the TwinVQ oracle (tools/twinvq_ref_driver.c) from the ISO/IEC 14496-5 reference software.  The software is NOT part of this repository: unpack the "Electronic inserts"
# archive of ISO/IEC 14496-26 (C050470e_Electronic_inserts, natural audio, mp4AudVm_Rewrite) somewhere and pass the directory.  Nothing of it is copied; the result is a local binary.
# usage: tools/twinvq_ref_build.sh /path/to/mp4AudVm_Rewrite OUT_DIR      -> OUT_DIR/twinvq_ref
set -e
V=$1; O=$2; HERE=$(cd "$(dirname "$0")" && pwd)
[ -d "$V/src_tf" ] && [ -n "$O" ] || { echo "usage: $0 /path/to/mp4AudVm_Rewrite OUT_DIR"; exit 2; }
mkdir -p "$O"; cd "$O"; rm -f ./*.o
FL="-w -std=gnu89 -fcommon -O1 -I$V/include -I$V/src_tf -I$V/src_frame -I$V/src_mp4 -I$V/src_lpc -I$V/src_par -DDOUBLE_WIN -DDOLBY_MDCT -DCT_SBR -DVERSION2 -DPNS -DDRC -DSRS"
for f in "$V"/src_tf/*.c "$V/src_frame/bitstream.c" "$V/src_frame/common_m4a.c"; do
  b=$(basename "$f" .c)
  case $b in dec_tf|dec_tf_dmy|sam_*|ct_*|mat_*|tf_celp_dmy|*enc*|show_adif|scal_dec_frame|tvqAUDecode) continue;; esac
  gcc $FL -c "$f" -o "$b.o" 2>/dev/null || echo "skipped $b"
done
gcc $FL -c "$HERE/twinvq_ref_stubs.c" -o stubs.o
gcc $FL -g -O0 -c "$HERE/twinvq_ref_driver.c" -o twinvq_ref_main.o
gcc -o twinvq_ref twinvq_ref_main.o $(ls ./*.o | grep -v twinvq_ref_main) -lm
echo "built $O/twinvq_ref"
