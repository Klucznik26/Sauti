#!/usr/bin/env python3
"""Makes tests/data/mps_hr_golden.bin: input and output of libxaac's MPEG Surround 212 upmix for streams in bsHighRateMode (several parameter sets per frame, bsFramingType, EcData modes default / keep / new, smoothing of the upmix
matrices over time, bsSmoothMode 1..3) made by `sauti_usac_gen --mps-hr` (see make_usac_mps_gen_fixtures.sh, usac_y17 / y18 / y19).  Per stream: the decorrelator configuration, the number of frames, the number of residual bands and the
upmix kind; per frame: the QMF bands of the downmix that enter the upmix (bands 0..53), the residual (32 bands) when there are residual bands, the number of sets, the parameter slots, the CLD / ICC indices of EVERY set, bsSmoothTime
and the smoothing flags of every set, and for each output channel four sums of the output grid (see tests/test_usac_mps.c).  The dumps are those of an instrumented copy of libxaac (env MPS_SIG, MPS_SIG3, MPS_PARAM, MPS_RES).
usage: make_mps_hr_golden.py OUT DUMPDIR NAME:SCI:DECORR:RESBANDS:FRAMES ...   (stereoConfigIndex SCI 1 or 2; the streams must be made with GEN_HR_NOSMOOTH_INDEP=1: the first frame of libxaac runs through a start-up path)"""
import struct, sys, numpy as np
out_path, D = sys.argv[1], sys.argv[2].rstrip('/') + '/'
cld_deq = [-150.0, -45.0, -40.0, -35.0, -30.0, -25.0, -22.0, -19.0, -16.0, -13.0, -10.0, -8.0, -6.0, -4.0, -2.0, 0.0, 2.0, 4.0, 6.0, 8.0, 10.0, 13.0, 16.0, 19.0, 22.0, 25.0, 30.0, 35.0, 40.0, 45.0, 150.0]
icc_deq = np.array([1.0000, 0.9370, 0.84118, 0.60092, 0.36764, 0.0, -0.5890, -0.9900], '<f4')
rec1 = 32 + 32 * 64 * 8 + 32 * 71 * 8 * 2; rec3 = 2 * 32 * 64 * 8; recp = 32 + 36 + 9 * 28 * 4 * 3 + 36 + 9 * 28 * 4; recr = 32 + 32 * 64 * 8 + 32 * 71 * 8 * 2
sets = [a.split(':') for a in sys.argv[3:]]
out = b'MPSH' + struct.pack('<i', len(sets))
for name, sci, dc, rb, nf in sets:
    sci, dc, rb, nf = int(sci), int(dc), int(rb), int(nf)
    g1 = open(D + name + '.sig', 'rb').read(); g3 = open(D + name + '.sig3', 'rb').read(); pp = open(D + name + '.par', 'rb').read(); gr = open(D + name + '.res', 'rb').read() if rb else None
    out += struct.pack('<4i', dc, nf, rb, 1 if sci == 2 else 0)
    for f in range(nf):
        h = struct.unpack('8i', g1[f * rec1:f * rec1 + 32]); bc = h[2]
        q = np.frombuffer(g1[f * rec1 + 32:f * rec1 + 32 + 32 * 64 * 8], '<f4').reshape(32, 64, 2)[:, :54, :]
        o = np.frombuffer(g3[f * rec3:(f + 1) * rec3], '<f4').reshape(2, 32, 64, 2); o = o[..., 0] + 1j * o[..., 1]
        P = pp[f * recp:(f + 1) * recp]; pph = struct.unpack('8i', P[:32]); slots = struct.unpack('9i', P[32:68])
        cl = np.frombuffer(P[68:68 + 9 * 28 * 4], '<f4').reshape(9, 28); ic = np.frombuffer(P[68 + 9 * 28 * 4:68 + 2 * 9 * 28 * 4], '<f4').reshape(9, 28)
        st = struct.unpack('9i', P[68 + 3 * 9 * 28 * 4:68 + 3 * 9 * 28 * 4 + 36]); sd = struct.unpack('%di' % (9 * 28), P[68 + 3 * 9 * 28 * 4 + 36:])
        ns = pph[1]
        cidx = [[cld_deq.index(float(v)) - 15 for v in cl[s]] for s in range(9)]; iidx = [[int(np.argmin(np.abs(icc_deq - v))) for v in ic[s]] for s in range(9)]
        out += struct.pack('<4i', bc, ns, pph[2], pph[3]) + struct.pack('<9i', *slots) + struct.pack('<%di' % (9 * 28), *[v for s in cidx for v in s]) + struct.pack('<%di' % (9 * 28), *[v for s in iidx for v in s]) + struct.pack('<9i', *st) + struct.pack('<%di' % (9 * 28), *sd)
        out += q.astype('<f4').tobytes()
        if rb:
            r = np.frombuffer(gr[f * recr + 32:f * recr + 32 + 32 * 64 * 8], '<f4').reshape(32, 64, 2)[:, :32, :]; out += r.astype('<f4').tobytes()
        for c in range(2):
            x = o[c][:, :54]; w = (np.arange(32)[:, None] * 131 + np.arange(54)[None, :] * 7 + 1) / (32 * 64.0)
            out += struct.pack('<4d', float(x.real.sum()), float(x.imag.sum()), float((np.abs(x) ** 2).sum()), float(((x.real + 2 * x.imag) * w).sum()))
open(out_path, 'wb').write(out)
