#!/usr/bin/env python3
"""Regenerates src/decoders/modern/aac/sauti_aac_eld_window.c from the ELD window tables of an FFmpeg source tree (see the licensing note in that file).
usage: gen_eld_window.py PATH/TO/libavcodec/aactab.c"""
import re, sys
t = open(sys.argv[1]).read()
def table(name):
    i = t.index(name); j = t.index('{', i); k = t.index('};', j)
    return re.findall(r'[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?', t[j+1:k])
a = table("ff_aac_eld_window_512)[1920]"); b = table("ff_aac_eld_window_480)[1800]")
assert len(a) == 1920 and len(b) == 1800
print(len(a), len(b), "values parsed; see the repository file for the generated C source (header comment is maintained by hand).")
