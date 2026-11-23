#!/usr/bin/env python3
"""Splices two MONO USAC streams (libxaac encoder, no eSBR, UsacSingleChannelElement) into one STEREO stream (UsacChannelPairElement, stereoConfigIndex 0): channel 0 = the first stream's channel, channel 1 = the second's.  The two
encoders switch between FD and LPD on their own, so the pair has access units where both channels are LPD, both are FD and (the case no encoder of the repository produces) one is FD and the other LPD.  Every channel stream of a
UsacChannelPairElement is the channel stream of the mono element; only the header differs: core_mode of both channels, then for two FD channels tns_active / common_window = 0 (/ tns_on_lr, tns_present_both ...) and for the others the
tns_data_present bit of an FD channel stays in front of its stream.  Because the channels stay independent, a decoder must return the mono streams' PCM in the two channels: that is the check (libxaac's decoder does, to within one least significant
bit and its start-up frames).
The access units are taken without the AudioPreRoll extension element of the first one (usacExtElementPresent = 0): the first access unit is independent anyway.  The END bit of every channel element (where its channel stream stops: the position
a decoder reaches after parsing it) cannot be found without parsing the stream; the caller passes them in a JSON file {"A": [[mode, end], ...], "B": [...]} (mode "L" / "F").
With eSBR the streams carry usac_sbr_data(): the pair's eSBR payload cannot be made from two mono payloads, so it is taken from T (a stereo stream of the same settings) access unit by access unit: the JSON then has a third key "T": {"ends": [[mode, end], ...], "sbr": [bit position where the SBR payload ends, ...]} and the payload is the bits of T from its channel streams' END to that position (the SBR payload does not depend on the core modes; the PCM of the spliced stream is then not that of the mono streams, but a decoder must agree with another decoder on it).
usage: usac_splice_pair.py DIR A B T ENDS.json N OUT   (DIR/A.es + A.txt ... as written by xaacenc; T = a stereo stream of the same settings, its configuration is used; N access units; writes OUT.asc and OUT.aus)"""
import json, re, struct, sys
def bits(b): return ''.join(format(x, '08b') for x in b)
def frombits(s):
    s += '0' * ((-len(s)) % 8); return bytes(int(s[i:i + 8], 2) for i in range(0, len(s), 8))
def read(path, n):
    es = open("%s/%s.es" % (path, n), 'rb').read(); txt = open("%s/%s.txt" % (path, n)).read()
    cfg = int(re.search(r'dec_info_init:(\d+)', txt).group(1)); sizes = [int(x) for x in re.findall(r'ia_mp4_stsz_size:(\d+)', txt)]
    p = cfg; aus = []
    for s in sizes: aus.append(es[p:p + s]); p += s
    return es[:cfg], aus
def strip0(au):                                                             # the first access unit without its AudioPreRoll extension element
    b = bits(au); assert b[0] == '1' and b[1] == '1'
    p = 2; dflt = int(b[p]); p += 1; ln = 0
    if not dflt:
        ln = int(b[p:p + 8], 2); p += 8
        if ln == 255: ln += int(b[p:p + 16], 2) - 2; p += 16
    return b[0] + '0' + b[p + ln * 8:]
def main():
    d, a, b_, t, ends_path, n, out = sys.argv[1:8]; n = int(n)
    ends = json.load(open(ends_path)); mono = {}
    for name, nm in ((a, 'A'), (b_, 'B')):
        asc, aus = read(d, name); st = [strip0(aus[0])] + [bits(x) for x in aus[1:]]
        mono[nm] = [(st[i], ends[nm][i][0], ends[nm][i][1]) for i in range(n)]
    asc2, taus = read(d, t); pair = []
    tst = [strip0(taus[0])] + [bits(x) for x in taus[1:]] if 'T' in ends else None
    for i in range(n):
        (sa, ma, ea), (sb, mb, eb) = mono['A'][i], mono['B'][i]
        assert sa[0] == sb[0]
        ca, cb = sa[2:ea], sb[2:eb]                                         # the channel elements: core_mode + the rest
        cm = ('1' if ma == 'L' else '0') + ('1' if mb == 'L' else '0')
        if ma == 'F' and mb == 'F':                                         # two FD channels: no common window, TNS signalled in the header
            ta, tb = ca[1], cb[1]
            hdr = '00' if (ta == '0' and tb == '0') else '1' + '0' + '0' + ('1' if (ta == '1' and tb == '1') else '0' + tb)
            body = ca[2:] + cb[2:]
        else:
            hdr = ''; body = ca[1:] + cb[1:]
        sbr = tst[i][ends['T']['ends'][i][1]:ends['T']['sbr'][i]] if tst else ''
        pair.append(sa[0] + '0' + cm + hdr + body + sbr + '0')                    # usacExtElementPresent = 0 of the element that follows the channel element
    open(out + '.asc', 'wb').write(asc2); open(out + '.aus', 'wb').write(b''.join(struct.pack('>I', len(x)) + x for x in (frombits(s) for s in pair)))
    print('%d access units: %d both FD, %d mixed, %d both LPD' % (n, sum(1 for i in range(n) if ends['A'][i][0] == ends['B'][i][0] == 'F'), sum(1 for i in range(n) if ends['A'][i][0] != ends['B'][i][0]), sum(1 for i in range(n) if ends['A'][i][0] == ends['B'][i][0] == 'L')))
main()
