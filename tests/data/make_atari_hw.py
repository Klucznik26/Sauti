#!/usr/bin/env python3 -I
"""Buduje tests/data/atari_hw/: pliki AVR / SND / DVS wyprodukowane przez PRAWDZIWY program 525 ("Five to Five", WinRec 1.41) w emulatorze Hatari z syntetycznych WAV-ow
(src_s44s16.wav, src_m44m16.wav) i ich referencje .ref (int16 LE, probki przeplecione, wartosc = float * 32768).
Wzorce: AVR i SND = FFmpeg (niezalezny dekoder), DVS = dekoder samego programu 525 (import pliku DVS i eksport do WAV; 525 zapisuje WAV z rozmiarem chunku fmt = 0, dane od bajtu 44).
Uzycie: make_atari_hw.py KATALOG_Z_EKSPORTAMI   (nazwy bog44s16_fx_*.{avr,snd,dvs}, bog44m16_fxm_*.*, bog44m16_oracle_*.wav -- patrz docs/ATARI_HW_FIXTURES.md)"""
import os, subprocess, sys, shutil
import numpy as np
src, dst = sys.argv[1], os.path.join(os.path.dirname(os.path.abspath(__file__)), 'atari_hw')
os.makedirs(dst, exist_ok=True)
FX = [  # nazwa, plik zrodlowy, wzorzec
 ('avr_st16', 'bog44s16_fx_keep.avr', 'ffmpeg'), ('avr_st8', 'bog44s16_fx_st8.avr', 'ffmpeg'), ('avr_mo16', 'bog44s16_fx_mo16.avr', 'ffmpeg'), ('avr_mo8', 'bog44s16_fx_mo8.avr', 'ffmpeg'),
 ('avr_m_mo16', 'bog44m16_fxm_keep.avr', 'ffmpeg'),
 ('snd_st16', 'bog44s16_fx_keep.snd', 'ffmpeg'), ('snd_st16ulaw', 'bog44s16_fx_st16ulaw.snd', 'ffmpeg'), ('snd_st8', 'bog44s16_fx_st8.snd', 'ffmpeg'),
 ('snd_mo16', 'bog44s16_fx_mo16.snd', 'ffmpeg'), ('snd_mo16ulaw', 'bog44s16_fx_mo16ulaw.snd', 'ffmpeg'), ('snd_mo8', 'bog44s16_fx_mo8.snd', 'ffmpeg'),
 ('dvs_st16', 'bog44s16_fx_keep.dvs', 'bog44m16_oracle_keep.wav:16'), ('dvs_st16dp', 'bog44s16_fx_st16dp.dvs', 'bog44m16_oracle_st16dp.wav:16'),
 ('dvs_st8', 'bog44s16_fx_st8.dvs', 'bog44m16_oracle_st8.wav:8'), ('dvs_mo8', 'bog44m16_fxm_mo8.dvs', 'bog44m16_oracle_mo8.wav:8'),
]
for name, f, ref in FX:
    ext = f.rsplit('.', 1)[1]; shutil.copy(os.path.join(src, f), os.path.join(dst, name + '.' + ext))
    if ref == 'ffmpeg':
        raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', os.path.join(src, f), '-f', 'f32le', '-'], capture_output=True, check=True).stdout
        a = np.rint(np.frombuffer(raw, '<f4') * 32768.0).astype('<i2')
    else:
        w, bits = ref.split(':'); raw = open(os.path.join(src, w), 'rb').read()[44:]
        a = np.frombuffer(raw, '<i2') if bits == '16' else ((np.frombuffer(raw, 'u1').astype('i4') - 128) * 256).astype('<i2')
    a.astype('<i2').tofile(os.path.join(dst, name + '.ref'))
print('ok', len(FX))
