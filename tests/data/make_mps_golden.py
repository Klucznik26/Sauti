#!/usr/bin/env python3
"""Makes tests/data/mps_golden.bin: input and output of libxaac's MPEG Surround 212 upmix (ixheaacd_mps_apply) for the first six frames of one of its own streams (a stream of make_usac_mps_fixtures.sh: the file holds the frames the instrumented decoder printed, from its reset state).  The instrumented COPY of libxaac writes per frame (env MPS_SIG, MPS_SIG3, MPS_PARAM; fwrite blocks in
ixheaacd_mps_apply / ixheaacd_mps_temp_process / ixheaacd_mps_frame_decode of ixheaacd_mps_dec.c, ixheaacd_mps_temp_process.c): the QMF samples that enter the upmix (after the downmix gain), the two QMF grids that
leave it, and the dequantised parameters.  The file keeps the input of QMF bands 0..53, the CLD / ICC indices of the one parameter set and, per output channel, four sums of the output grid (test_usac_mps.c, test_golden).
The first frame of the reference runs through a start-up path (the pre-mix matrix ramps in) that this decoder does not reproduce: the test checks frames 1..5.
usage: make_mps_golden.py   (the dump files are those written by the instrumented libxaac: g1.bin = MPS_SIG, g3.bin = MPS_SIG3, p1.bin = MPS_PARAM, in /tmp/claude-1000/mps)"""
import struct, numpy as np
D = '/tmp/claude-1000/mps/'
g1 = open(D + 'g1.bin', 'rb').read(); g3 = open(D + 'g3.bin', 'rb').read(); pp = open(D + 'p1.bin', 'rb').read()
rec1 = 32 + 32 * 64 * 8 + 32 * 71 * 8 * 2; rec3 = 2 * 32 * 64 * 8; recp = 32 + 36 + 9 * 28 * 4 * 3 + 36 + 9 * 28 * 4
cld_deq = [-150.0, -45.0, -40.0, -35.0, -30.0, -25.0, -22.0, -19.0, -16.0, -13.0, -10.0, -8.0, -6.0, -4.0, -2.0, 0.0, 2.0, 4.0, 6.0, 8.0, 10.0, 13.0, 16.0, 19.0, 22.0, 25.0, 30.0, 35.0, 40.0, 45.0, 150.0]
icc_deq = np.array([1.0000, 0.9370, 0.84118, 0.60092, 0.36764, 0.0, -0.5890, -0.9900], '<f4')
out = b'MPSG' + struct.pack('<i', 6)
for f in range(6):
    h = struct.unpack('8i', g1[f * rec1:f * rec1 + 32]); bc = h[2]
    q = np.frombuffer(g1[f * rec1 + 32:f * rec1 + 32 + 32 * 64 * 8], '<f4').reshape(32, 64, 2)[:, :54, :]
    o = np.frombuffer(g3[f * rec3:(f + 1) * rec3], '<f4').reshape(2, 32, 64, 2); o = o[..., 0] + 1j * o[..., 1]
    P = pp[f * recp:(f + 1) * recp]; ph = struct.unpack('8i', P[:32]); slots = struct.unpack('9i', P[32:68])
    cl = np.frombuffer(P[68:68 + 9 * 28 * 4], '<f4').reshape(9, 28); ic = np.frombuffer(P[68 + 9 * 28 * 4:68 + 2 * 9 * 28 * 4], '<f4').reshape(9, 28)
    cidx = [cld_deq.index(float(v)) - 15 for v in cl[0]]; iidx = [int(np.argmin(np.abs(icc_deq - v))) for v in ic[0]]
    out += struct.pack('<4i', bc, ph[1], ph[2], slots[0]) + struct.pack('<28i', *cidx) + struct.pack('<28i', *iidx) + q.astype('<f4').tobytes()
    for c in range(2):
        x = o[c][:, :54]; w = (np.arange(32)[:, None] * 131 + np.arange(54)[None, :] * 7 + 1) / (32 * 64.0)
        out += struct.pack('<4d', float(x.real.sum()), float(x.imag.sum()), float((np.abs(x) ** 2).sum()), float(((x.real + 2 * x.imag) * w).sum()))
import sys
open(sys.argv[1] if len(sys.argv) > 1 else 'mps_golden.bin', 'wb').write(out)
