#!/usr/bin/env bash
# Regenerates tests/data/usac_z1..z3.m4a: USAC stereo with MPEG Surround 212 (stereoConfigIndex 2, no residual bands) and eSBR 2:1 from SYNTHETIC signals (harmonic comb with a panning that moves, uncorrelated noise, and
# sections of mono, one side only and anti-phase), made by the encoder of libxaac (Apache License 2.0, built OUTSIDE this repository, default ~/Sauti-tools/libxaac/build/xaacenc): -mps:1 -tree_cfg:0 (212) -esbr:1
# -ccfl_idx:3 (2:1).  z1: 48 kHz 24 kbit/s, z2: 44.1 kHz 64 kbit/s, z3: 32 kHz 24 kbit/s, z5 / z6: the same signal at 48 kHz with eSBR 4:1 (-ccfl_idx:4, 32 kbit/s; z6 with residual bands patched to 8); z7 / z8: eSBR 8:3 (-ccfl_idx:2, 768 line core; z8 with residual bands patched to 8); z9: every frame LPD (-usac:2) with MPS at eSBR 2:1; z4: the stream of z1 with bsResidualBands patched to 12 (the encoder never uses residual bands and its second core channel is silent: the patch is made in the AudioSpecificConfig AND in the copy of the configuration inside the first access unit's AUDIOPREROLL payload, which is the one libxaac's decoder reads).  The encoder writes ONE parameter set per frame, no phase coding and no temporal shaping (docs USAC_MPS212_PLAN.md).
# z10: the encoder's own FD / LPD switching with MPS (stereoConfigIndex 2, 24 kbit/s): the first 40 access units are FD, LPD (10), FD (16), LPD, so both transitions occur.
# usage: tests/data/make_usac_mps_fixtures.sh   (needs python3 + numpy and the libxaac encoder; XAAC=<dir with xaacenc> to override)
set -euo pipefail
cd "$(dirname "$0")/../.."
ENC="${XAAC:-$HOME/Sauti-tools/libxaac/build}/xaacenc"
[ -x "$ENC" ] || { echo "no libxaac encoder at $ENC"; exit 1; }
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-mps.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
mk() {   # name fs bitrate [residual bands to patch into the configuration] [ccfl_idx: 3 = eSBR 2:1, 4 = 4:1] [usac mode: 0 = the encoder switches FD / LPD, 1 = FD only, 2 = LPD only] [signal: 0 = comb / noise, 1 = voiced glides, chords and noise bursts] [access units kept]
  local name=$1 fs=$2 br=$3 res=${4:-0} ccfl=${5:-3} um=${6:-1} sig=${7:-0} keep=${8:-24}
  python3 -I - "$W" "$fs" "$name" "$sig" <<'PY'
import numpy as np, struct, sys
W, fs, name, sig = sys.argv[1], int(sys.argv[2]), sys.argv[3], int(sys.argv[4])
n = fs * (8 if sig else 4); t = np.arange(n) / fs; r = np.random.default_rng(fs)
base = sum(0.1 / h * np.sin(2 * np.pi * f * h * t) for f in (180, 233, 311) for h in range(1, 14) if f * h < fs / 2) * (0.6 + 0.4 * np.sin(2 * np.pi * 0.7 * t))
noise = r.standard_normal((2, n)) * 0.05
pan = 0.5 + 0.45 * np.sin(2 * np.pi * 0.4 * t)
L = base * np.sqrt(1 - pan) + noise[0] * (0.3 + 0.7 * (np.sin(2 * np.pi * 0.9 * t) > 0)); R = base * np.sqrt(pan) * 0.8 + noise[1]
seg = np.minimum((t / 0.5).astype(int) % 6, 5)
R = np.where(seg == 3, -L, R); L = np.where(seg == 4, 0, L); R = np.where(seg == 5, 0.2 * R, R)
if sig == 1:                                                                # voiced segments (pitch glide, formant weighted harmonics), chords and noise bursts with clicks: the encoder switches between FD and LPD
    seg = (t // 1.0).astype(int); sg = np.zeros(n); r = np.random.default_rng(7)
    for k in range(8):
        m = seg == k; tt = t[m]
        if k % 3 == 0:
            f0 = 120 + 40 * np.sin(2 * np.pi * 1.3 * tt); ph = np.cumsum(f0) / fs * 2 * np.pi
            x = sum(np.sin(h * ph) * np.exp(-((h * f0 - 700) / 400) ** 2) + 0.6 * np.sin(h * ph) * np.exp(-((h * f0 - 1800) / 500) ** 2) for h in range(1, 30)) * 0.12 * (0.5 + 0.5 * np.sin(2 * np.pi * 4 * tt) ** 2)
        elif k % 3 == 1:
            x = sum(0.08 * np.sin(2 * np.pi * f * tt) for f in (261.6, 329.6, 392, 523.2, 659.3, 784))
        else:
            x = r.standard_normal(m.sum()) * 0.05 * (np.sin(2 * np.pi * 6 * tt) > 0.3); x[::3000] += 0.8
        sg[m] = x
    L = sg; R = np.roll(sg, 40) * 0.8 + 0.02 * r.standard_normal(n)
d = (np.clip(np.stack([L, R], 1), -1, 1).reshape(-1) * 32767).astype('<i2').tobytes()
open("%s/%s.wav" % (W, name), 'wb').write(b'RIFF' + struct.pack('<I', 36 + len(d)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, 2, fs, fs * 4, 4, 16) + b'data' + struct.pack('<I', len(d)) + d)
PY
  "$ENC" -ifile:"$W/$name.wav" -ofile:"$W/$name.es" -aot:42 -usac:$um -br:$br -esbr:1 -mps:1 -tree_cfg:0 -ccfl_idx:$ccfl >/dev/null
  if [ "$res" != 0 ]; then
    python3 -I - "$W" "$name" "$res" <<'PY'
import re, sys
W, n, res = sys.argv[1], sys.argv[2], int(sys.argv[3])
es = bytearray(open("%s/%s.es" % (W, n), 'rb').read()); txt = open("%s/%s.txt" % (W, n)).read()
cfg = int(re.search(r'dec_info_init:(\d+)', txt).group(1)); sizes = [int(x) for x in re.findall(r'ia_mp4_stsz_size:(\d+)', txt)]
bits = lambda b: ''.join(format(x, '08b') for x in b)
copy = bits(es[cfg:cfg + sizes[0]]).find(bits(es[:cfg])[19:19 + 120])        # the configuration without the AudioSpecificConfig header (19 bits) as the AUDIOPREROLL payload carries it
assert copy >= 0, "no copy of the configuration in the first access unit"
def put(pos):                                                               # bsResidualBands: 5 bits at bit 80 of the AudioSpecificConfig of the encoder's stereoConfigIndex 2 configuration (found by a patch-and-parse scan)
    for i in range(5):
        p = pos + i; es[p // 8] = (es[p // 8] & ~(0x80 >> (p % 8))) | (((res >> (4 - i)) & 1) << (7 - p % 8))
put(80); put(cfg * 8 + copy + (80 - 19))
open("%s/%s.es" % (W, n), 'wb').write(es)
PY
  fi
  python3 -I - "$W" "$name" "$keep" <<'PY'
import re, struct, sys
W, n, keep = sys.argv[1], sys.argv[2], int(sys.argv[3])
es = open("%s/%s.es" % (W, n), 'rb').read(); txt = open("%s/%s.txt" % (W, n)).read()
cfg = int(re.search(r'dec_info_init:(\d+)', txt).group(1)); sizes = [int(x) for x in re.findall(r'ia_mp4_stsz_size:(\d+)', txt)][:keep]
open("%s/%s.asc" % (W, n), 'wb').write(es[:cfg]); p = cfg; out = b''
for s in sizes: out += struct.pack('>I', s) + es[p:p + s]; p += s
open("%s/%s.aus" % (W, n), 'wb').write(out)
PY
  python3 -I tools/mux_raw_aus_mp4.py "$W/$name" "tests/data/usac_$name.m4a" 2048
}
mk z1 48000 24000
mk z2 44100 64000
mk z3 32000 24000
mk z4 48000 24000 12
mk z5 48000 32000 0 4
mk z6 48000 32000 8 4
mk z7 48000 32000 0 2
mk z8 48000 32000 8 2
mk z9 48000 32000 0 3 2
mk z10 48000 24000 0 3 0 1 40
