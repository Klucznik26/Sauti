"""Test tool (not a product): WAV / AIFF differential sweep against FFmpeg: sample formats (u8 / s16 / s24 / s32 / f32 / f64, A-law, mu-law, MS ADPCM, IMA ADPCM, AIFF big-endian and little-endian variants), 1..8 channels, 8..96 kHz.
usage (from the repository root): python3 -I tools/wav_aiff_sweep.py - needs ffmpeg and numpy; works in the current directory."""
import subprocess,numpy as np,os,sys
CLI=os.path.abspath('build/maktabasauti_cli')
rng=np.random.RandomState(3)
codecs=[('pcm_u8','wav'),('pcm_s16le','wav'),('pcm_s24le','wav'),('pcm_s32le','wav'),('pcm_f32le','wav'),('pcm_f64le','wav'),('pcm_alaw','wav'),('pcm_mulaw','wav'),('adpcm_ms','wav'),('adpcm_ima_wav','wav'),('pcm_s16be','aiff'),('pcm_s24be','aiff'),('pcm_s32be','aiff'),('pcm_u8','aiff'),('pcm_s8','aiff'),('pcm_f32be','aiff'),('pcm_f64be','aiff'),('pcm_s16le','aiff'),('adpcm_ima_qt','aiff'),('pcm_alaw','aiff'),('pcm_mulaw','aiff')]
bad=0;tot=0
for ci,(codec,fmt) in enumerate(codecs):
    for ch in (1,2,3,6,8):
        for rate in (8000,44100,96000):
            if rng.rand()>0.35: continue
            n=int(rng.randint(2000,20000))
            x=(0.8*rng.randn(n,ch)).clip(-1,1).astype('<f4')
            x.tofile('in.f32')
            f='t.'+('wav' if fmt=='wav' else 'aiff')
            r=subprocess.run(['ffmpeg','-v','error','-y','-f','f32le','-ar',str(rate),'-ac',str(ch),'-i','in.f32','-c:a',codec,f],capture_output=True,text=True)
            if r.returncode: continue
            tot+=1
            ref=subprocess.run(['ffmpeg','-v','error','-y','-i',f,'-f','f32le','ref.f32'],capture_output=True,text=True)
            if ref.returncode: continue
            u=subprocess.run([CLI,f,'o.f32'],capture_output=True,text=True)
            if u.returncode or not os.path.exists('o.f32'):
                bad+=1;print('FAIL',codec,fmt,ch,rate,(u.stdout+u.stderr).strip()[-70:]);continue
            a=np.fromfile('o.f32','<f4');b=np.fromfile('ref.f32','<f4')
            if len(a)!=len(b) or (len(a) and np.max(np.abs(a-b))>1e-6):
                bad+=1;print('MISMATCH',codec,fmt,ch,rate,len(a)//ch,len(b)//ch,float(np.max(np.abs(a[:min(len(a),len(b))]-b[:min(len(a),len(b))]))) if min(len(a),len(b)) else None)
            os.remove('o.f32')
print('files',tot,'bad',bad)
