#!/usr/bin/env python3
"""Makes tests/data/mps_phase_golden.bin: input and output of libxaac's MPEG Surround 212 upmix of stereoConfigIndex 1 (one core channel, rotation upmix) for the first six frames of the streams usac_y4..y6 (made by
make_usac_mps_gen_fixtures.sh): y4 without phase coding, y5 with phase coding (IPD / OPD) and OPD smoothing 0, y6 with OPD smoothing 1 and decorrelator 2.  Per set the phase coding flag and the decorrelator configuration, per
frame the QMF samples of the downmix that enter the upmix (bands 0..53, after the downmix gain), the CLD / ICC / IPD indices of the one parameter set, the phase mode, the OPD smoothing mode and the IPD quantiser, and per output
channel four sums of the output grid (see tests/test_usac_mps.c).  The dumps are those of an instrumented copy of libxaac (env MPS_SIG, MPS_SIG3, MPS_PARAM, MPS_PH).
usage: make_mps_phase_golden.py OUT DUMPDIR NAME:PHASE:DECORR ...   (e.g. mps_phase_golden.bin /tmp/claude-1000/mps/gy y4:0:0 y5:1:0 y6:1:2)"""
import struct, sys, numpy as np
out_path, D = sys.argv[1], sys.argv[2].rstrip('/') + '/'
cld_deq = [-150.0, -45.0, -40.0, -35.0, -30.0, -25.0, -22.0, -19.0, -16.0, -13.0, -10.0, -8.0, -6.0, -4.0, -2.0, 0.0, 2.0, 4.0, 6.0, 8.0, 10.0, 13.0, 16.0, 19.0, 22.0, 25.0, 30.0, 35.0, 40.0, 45.0, 150.0]
icc_deq = np.array([1.0000, 0.9370, 0.84118, 0.60092, 0.36764, 0.0, -0.5890, -0.9900], '<f4')
rec1 = 32 + 32 * 64 * 8 + 32 * 71 * 8 * 2; rec3 = 2 * 32 * 64 * 8; recp = 32 + 36 + 9 * 28 * 4 * 3 + 36 + 9 * 28 * 4; rech = 32 + 9 * 28 * 4 * 3 + 36 + 2 * 9 * 28 * 4
NF = 6
sets = [a.split(':') for a in sys.argv[3:]]
out = b'MPSF' + struct.pack('<i', len(sets))
for name, ph, dc in sets:
    g1 = open(D + name + '.sig', 'rb').read(); g3 = open(D + name + '.sig3', 'rb').read(); pp = open(D + name + '.par', 'rb').read(); gh = open(D + name + '.ph', 'rb').read()
    out += struct.pack('<3i', int(ph), int(dc), NF)
    for f in range(NF):
        h = struct.unpack('8i', g1[f * rec1:f * rec1 + 32]); bc = h[2]
        q = np.frombuffer(g1[f * rec1 + 32:f * rec1 + 32 + 32 * 64 * 8], '<f4').reshape(32, 64, 2)[:, :54, :]
        o = np.frombuffer(g3[f * rec3:(f + 1) * rec3], '<f4').reshape(2, 32, 64, 2); o = o[..., 0] + 1j * o[..., 1]
        P = pp[f * recp:(f + 1) * recp]; pph = struct.unpack('8i', P[:32]); slots = struct.unpack('9i', P[32:68])
        cl = np.frombuffer(P[68:68 + 9 * 28 * 4], '<f4').reshape(9, 28); ic = np.frombuffer(P[68 + 9 * 28 * 4:68 + 2 * 9 * 28 * 4], '<f4').reshape(9, 28)
        cidx = [cld_deq.index(float(v)) - 15 for v in cl[0]]; iidx = [int(np.argmin(np.abs(icc_deq - v))) for v in ic[0]]
        H = gh[f * rech:(f + 1) * rech]; hh = struct.unpack('8i', H[:32]); ipd = struct.unpack('%di' % (9 * 28), H[32 + 2 * 9 * 28 * 4:32 + 3 * 9 * 28 * 4]); coarse = struct.unpack('9i', H[32 + 3 * 9 * 28 * 4:32 + 3 * 9 * 28 * 4 + 36])
        assert hh[6] == int(ph), (hh, ph)
        out += struct.pack('<7i', bc, pph[1], pph[2], slots[0], hh[3], hh[4], coarse[0]) + struct.pack('<28i', *cidx) + struct.pack('<28i', *iidx) + struct.pack('<28i', *ipd[:28]) + q.astype('<f4').tobytes()
        for c in range(2):
            x = o[c][:, :54]; w = (np.arange(32)[:, None] * 131 + np.arange(54)[None, :] * 7 + 1) / (32 * 64.0)
            out += struct.pack('<4d', float(x.real.sum()), float(x.imag.sum()), float((np.abs(x) ** 2).sum()), float(((x.real + 2 * x.imag) * w).sum()))
open(out_path, 'wb').write(out)
