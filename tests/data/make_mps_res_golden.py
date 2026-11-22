#!/usr/bin/env python3
"""Makes tests/data/mps_res_golden.bin: input and output of libxaac's MPEG Surround 212 upmix WITH RESIDUAL BANDS (ixheaacd_mps_apply) for the first five frames of the streams tests/data/usac_y1..y3.m4a (made by
make_usac_mps_gen_fixtures.sh): per set the number of residual bands and the decorrelator configuration, per frame the QMF samples of the downmix (bands 0..53) and of the residual (bands 0..31) that enter the upmix
(after the downmix gain; the residual with bands 0..2 six slots ahead of the others, as the reference delivers it), the CLD / ICC indices of the one parameter set and, per output channel, four sums of the output grid
(see tests/test_usac_mps.c).  The dumps are those of an instrumented copy of libxaac (env MPS_SIG, MPS_SIG3, MPS_PARAM, MPS_RES).
usage: make_mps_res_golden.py OUT DUMPDIR NAME:RESBANDS:DECORR ...   (e.g. mps_res_golden.bin /tmp/claude-1000/mps/gy y1:8:0 y2:20:2 y3:14:1)"""
import struct, sys, numpy as np
out_path, D = sys.argv[1], sys.argv[2].rstrip('/') + '/'
cld_deq = [-150.0, -45.0, -40.0, -35.0, -30.0, -25.0, -22.0, -19.0, -16.0, -13.0, -10.0, -8.0, -6.0, -4.0, -2.0, 0.0, 2.0, 4.0, 6.0, 8.0, 10.0, 13.0, 16.0, 19.0, 22.0, 25.0, 30.0, 35.0, 40.0, 45.0, 150.0]
icc_deq = np.array([1.0000, 0.9370, 0.84118, 0.60092, 0.36764, 0.0, -0.5890, -0.9900], '<f4')
rec1 = 32 + 32 * 64 * 8 + 32 * 71 * 8 * 2; rec3 = 2 * 32 * 64 * 8; recp = 32 + 36 + 9 * 28 * 4 * 3 + 36 + 9 * 28 * 4; recr = 32 + 32 * 64 * 8 + 32 * 71 * 8 * 2
NF = 5
sets = [a.split(':') for a in sys.argv[3:]]
out = b'MPSR' + struct.pack('<i', len(sets))
for name, rb, dc in sets:
    g1 = open(D + name + '.sig', 'rb').read(); g3 = open(D + name + '.sig3', 'rb').read(); pp = open(D + name + '.par', 'rb').read(); gr = open(D + name + '.res', 'rb').read()
    out += struct.pack('<3i', int(rb), int(dc), NF)
    for f in range(NF):
        h = struct.unpack('8i', g1[f * rec1:f * rec1 + 32]); bc = h[2]
        q = np.frombuffer(g1[f * rec1 + 32:f * rec1 + 32 + 32 * 64 * 8], '<f4').reshape(32, 64, 2)[:, :54, :]
        hr = struct.unpack('8i', gr[f * recr:f * recr + 32]); assert hr[4] == int(rb) and hr[2] == 32, hr
        r = np.frombuffer(gr[f * recr + 32:f * recr + 32 + 32 * 64 * 8], '<f4').reshape(32, 64, 2)[:, :32, :]
        o = np.frombuffer(g3[f * rec3:(f + 1) * rec3], '<f4').reshape(2, 32, 64, 2); o = o[..., 0] + 1j * o[..., 1]
        P = pp[f * recp:(f + 1) * recp]; ph = struct.unpack('8i', P[:32]); slots = struct.unpack('9i', P[32:68])
        cl = np.frombuffer(P[68:68 + 9 * 28 * 4], '<f4').reshape(9, 28); ic = np.frombuffer(P[68 + 9 * 28 * 4:68 + 2 * 9 * 28 * 4], '<f4').reshape(9, 28)
        cidx = [cld_deq.index(float(v)) - 15 for v in cl[0]]; iidx = [int(np.argmin(np.abs(icc_deq - v))) for v in ic[0]]
        out += struct.pack('<4i', bc, ph[1], ph[2], slots[0]) + struct.pack('<28i', *cidx) + struct.pack('<28i', *iidx) + q.astype('<f4').tobytes() + r.astype('<f4').tobytes()
        for c in range(2):
            x = o[c][:, :54]; w = (np.arange(32)[:, None] * 131 + np.arange(54)[None, :] * 7 + 1) / (32 * 64.0)
            out += struct.pack('<4d', float(x.real.sum()), float(x.imag.sum()), float((np.abs(x) ** 2).sum()), float(((x.real + 2 * x.imag) * w).sum()))
open(out_path, 'wb').write(out)
