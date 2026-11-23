#!/usr/bin/env bash
# Regenerates tests/data/usac_r1..r4.m4a: short EXCERPTS (whole access units, cut at random access points, which carry AudioPreRoll) of the public xHE-AAC demo files of Fraunhofer IIS
# (https://www2.iis.fraunhofer.de/AAC/xhe-aac-abr.html, the "Sintel" files).  Sintel (c) Blender Foundation | durian.blender.org, licensed CC BY 3.0 (https://creativecommons.org/licenses/by/3.0/);
# the encoded streams are the demo content of Fraunhofer IIS.  They are REAL streams of a production encoder, the only ones of the repository, and each pins a feature that no generated or libxaac-encoded stream has:
#   r1  Sintel_24kbps AUs 0 - 59:    eSBR 8:3 (768 core), LPD, MPS212 stereoConfigIndex 1 with 20 parameter bands, IPD / OPD, TSD, bs_sbr_preprocessing (from AU 26), the stale IPD quantiser flag of a kept set
#   r2  Sintel_24kbps AUs 230 - 259: the first LPD -> FD transition (LONG_STOP without FAC, TCX 1024) at ccfl 768
#   r3  Sintel_24kbps AUs 2436 - 2471: an LPD -> FD transition into EIGHT SHORT WINDOWS at ccfl 768 (AU 2462)
#   r4  Sintel_64kbps AUs 0 - 59:    stereoConfigIndex 3 (eSBR AFTER the MPS212 upmix)
# usage: tests/data/make_usac_real_excerpts.sh DIR   (DIR holds xHE-AAC_demo_content_for_adaptive_bitrate__Sintel_{24,64}kbps_rap5s.mp4, see ~/Sauti-tools/real-files/README.txt)
set -euo pipefail
cd "$(dirname "$0")/../.."
D="${1:?usage: make_usac_real_excerpts.sh DIR}"
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-excerpts.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
cut_au() {   # name source first count
  python3 -I tools/mp4_dump_aus.py "$D/xHE-AAC_demo_content_for_adaptive_bitrate__Sintel_$2kbps_rap5s.mp4" "$W/$1.full" >/dev/null
  python3 -I - "$W" "$1" "$3" "$4" <<'PY'
import struct, sys
W, n, first, count = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
raw = open("%s/%s.full.aus" % (W, n), 'rb').read(); p = 0; aus = []
while p + 4 <= len(raw):
    l = struct.unpack('>I', raw[p:p + 4])[0]; aus.append(raw[p + 4:p + 4 + l]); p += 4 + l
assert aus[first][0] & 0x80, "the excerpt must start at an independent access unit"
open("%s/%s.asc" % (W, n), 'wb').write(open("%s/%s.full.asc" % (W, n), 'rb').read())
open("%s/%s.aus" % (W, n), 'wb').write(b''.join(struct.pack('>I', len(a)) + a for a in aus[first:first + count]))
PY
  python3 -I tools/mux_raw_aus_mp4.py "$W/$1" "tests/data/usac_$1.m4a" 2048
}
cut_au r1 24 0 60
cut_au r2 24 230 30
cut_au r3 24 2436 36
cut_au r4 64 0 60
