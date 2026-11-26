"""Test tool (not a product): FLAC differential sweep.  Random signals (noise, tones, silence, square, low bits, impulses, chirps) of 1..8 channels, 8..192 kHz, 16 / 24 bit, compression levels 0..12 and several block sizes
are encoded with FFmpeg (libFLAC-compatible) and decoded with build/maktabasauti_cli; FLAC is lossless, so the PCM must equal the source bit for bit.  usage (from the repository root):
python3 -I tools/flac_sweep.py [N files, default 40, at most 70]  - needs ffmpeg and numpy; works in the current directory (in.raw, t.flac, o.f32)."""
import subprocess,numpy as np,itertools,os,sys
CLI=os.path.abspath('build/maktabasauti_cli')
rng=np.random.RandomState(7)
def sig(kind,n,ch,bits):
    full=2**(bits-1)-1
    t=np.arange(n)
    if kind=='noise': x=rng.randint(-full-1,full+1,(n,ch))
    elif kind=='tone': x=np.stack([(0.7*full*np.sin(2*np.pi*(300+170*c)*t/44100+c)).astype(np.int64) for c in range(ch)],1)
    elif kind=='silence': x=np.zeros((n,ch),dtype=np.int64)
    elif kind=='square': x=np.stack([np.where((t//(50+c))%2,full,-full-1) for c in range(ch)],1)
    elif kind=='lowbits': x=(rng.randint(-8,8,(n,ch))*(1<<(bits//2)))
    elif kind=='impulse':
        x=np.zeros((n,ch),dtype=np.int64);x[::997]=full;x[5::1013]=-full-1
    elif kind=='chirp': x=np.stack([(0.9*full*np.sin(2*np.pi*(50+ (t**1.5)*1e-3*(c+1))*t/44100)).astype(np.int64) for c in range(ch)],1)
    return np.clip(x,-full-1,full).astype(np.int64)
bad=0;total=0
cfgs=[]
for ch in (1,2,3,4,5,6,8):
    for rate in (8000,22050,44100,96000,192000):
        for bits in (16,24):
            cfgs.append((ch,rate,bits))
rng2=np.random.RandomState(1);rng2.shuffle(cfgs)
for ci,(ch,rate,bits) in enumerate(cfgs[:int(sys.argv[1]) if len(sys.argv)>1 else 40]):
    kind=['noise','tone','silence','square','lowbits','impulse','chirp'][ci%7]
    n=int(rng.randint(3000,40000))
    x=sig(kind,n,ch,bits)
    sf='s16le' if bits==16 else 's32le'
    xx=(x if bits==16 else x*256)
    dt='<i2' if bits==16 else '<i4'
    xx.astype(dt).tofile('in.raw')
    comp=int(rng.randint(0,13));bs=int(rng.choice([0,256,1152,4096,16384]))
    cmd=['ffmpeg','-v','error','-y','-f',sf,'-ar',str(rate),'-ac',str(ch),'-i','in.raw','-c:a','flac','-compression_level',str(comp)]
    if bits==24: cmd+=['-sample_fmt','s32']
    if bs: cmd+=['-frame_size',str(bs)]
    cmd+=['t.flac']
    r=subprocess.run(cmd,capture_output=True,text=True)
    if r.returncode:print('ffmpeg fail',ch,rate,bits,r.stderr[-80:]);continue
    r=subprocess.run([CLI,'t.flac','o.f32'],capture_output=True,text=True)
    total+=1
    if r.returncode or not os.path.exists('o.f32'):
        bad+=1;print('FAIL decode',kind,ch,rate,bits,comp,bs,(r.stdout+r.stderr).strip()[-80:]);continue
    y=np.fromfile('o.f32','<f4').reshape(-1,ch)
    ref=(x.astype(np.float64)/2**(bits-1)).astype(np.float32)
    if y.shape!=ref.shape or not np.array_equal(y,ref):
        bad+=1;print('MISMATCH',kind,ch,rate,bits,comp,bs,y.shape,ref.shape,float(np.max(np.abs(y[:min(len(y),len(ref))]-ref[:min(len(y),len(ref))]))) if len(y) else None)
    os.remove('o.f32')
print('files',total,'bad',bad)
