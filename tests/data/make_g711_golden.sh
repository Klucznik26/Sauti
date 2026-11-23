#!/usr/bin/env bash
# Regenerates tests/g711_golden.h: the 256 values of G.711 mu-law and A-law as FFmpeg decodes them (an independent oracle for tests/test_maktabasauti_g711.c).  usage: tests/data/make_g711_golden.sh
set -euo pipefail
cd "$(dirname "$0")/../.."
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-g711.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
python3 -I - "$W" <<'PY'
import subprocess, sys
import numpy as np
W = sys.argv[1]
open(W + '/all256.bin', 'wb').write(bytes(range(256)))
out = {}
for nm, fmt in (('ulaw', 'mulaw'), ('alaw', 'alaw')):
    r = subprocess.run(['ffmpeg', '-v', 'error', '-f', fmt, '-ar', '8000', '-ac', '1', '-i', W + '/all256.bin', '-f', 's16le', '-'], capture_output=True, check=True).stdout
    out[nm] = np.frombuffer(r, dtype='<i2'); assert len(out[nm]) == 256
def arr(n, v): return 'static const int16_t g711_%s_golden[256] = {\n' % n + ',\n'.join(', '.join('%d' % x for x in v[i:i + 16]) for i in range(0, 256, 16)) + '\n};\n'
open('tests/g711_golden.h', 'w').write("/* The 256 decoded values of G.711 mu-law and A-law as FFmpeg decodes them (ffmpeg -f mulaw / alaw -i 0..255 -f s16le), an oracle independent of the decoder under test (made with tests/data/make_g711_golden.sh).  Sample values, 16 bit signed. */\n" + arr('ulaw', out['ulaw']) + arr('alaw', out['alaw']))
PY
