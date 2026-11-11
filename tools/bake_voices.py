#!/usr/bin/env python3
"""Bakes the fighters' spoken lines into assets/voice/lines.flac and lines_index.bin.

Each fighter has a couple of intro taunts ("hi", said as the round starts) and a couple of
victory lines ("win"). Moheeb and Luis are Kyutai Pocket TTS clones of their own recordings
(the same references SMALL PARTS and KEYHOLDER's cameo use); everyone else is Kokoro 82M, in
the voice their KEYHOLDER cast entry uses.

  KOKORO=/dir/with/kokoro-v1.0.onnx,voices-v1.0.bin CACHE=/dir python3 tools/bake_voices.py

Needs ffmpeg (f32le -> FLAC, as tools/audio_rebuild.sh makes the music). Takes are cached by
engine, voice, speed and text, so a rerun only speaks what changed. If the Kyutai references or
generator are missing, Moheeb and Luis fall back to a Kokoro stand in and it says so.

The bank is the same shape the SFX bank uses (sfx.c load_sfx): lines_index.bin is "CKSI", u32
count, then per line u8 name length, name ("vee.win"), f32 arg (0), u32 start frame, u32 frames;
lines.flac is one mono 48 kHz stream, stored at half level (the mixer doubles it).
"""
import hashlib, os, shlex, struct, subprocess, tempfile
import numpy as np, soundfile as sf

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
RATE = 48000
KDIR = os.environ['KOKORO']
CACHE = os.environ.get('CACHE', os.path.join(ROOT, 'build/voice-wav'))
os.makedirs(CACHE, exist_ok=True)

KYUTAI = {
    'moheeb': os.path.expanduser(os.environ.get('MOHEEB_WAV', '~/Downloads/moheeb-voice.wav')),
    'luis': os.path.expanduser(os.environ.get('LUIS_WAV', '~/Downloads/luis-voice.wav')),
}
KYUTAI_PY = shlex.split(os.environ.get('KYUTAI_PY', 'uv run --python 3.12 --with pocket-tts python'))

# fighter -> (engine voice, speed). Kyutai fighters carry their Kokoro stand in here too.
VOICE = {
    'moheeb': ('am_michael', 1.0), 'luis': ('am_adam', 1.04),
    'vee': ('af_heart', 1.06), 'dex': ('am_michael', 1.05), 'ruth': ('bf_emma', 1.02),
    'vic': ('am_fenrir', 1.05), 'rennick': ('am_liam', 1.1), 'dobbs': ('am_onyx', 1.0),
    'champ': ('af_nova', 1.1), 'bex': ('af_nicole', 1.02), 'cal': ('am_adam', 1.0),
    'root': ('bm_george', 0.98), 'brant': ('am_eric', 1.08),
}
# what each fighter says: hi (round start) and win (victory)
SAY = {
    'moheeb': {'hi': ["Saw a problem. You're it.", "Let's do this, then I document it."],
               'win': ["Fixed. Next ticket.", "That one's going in the wiki."]},
    'luis':   {'hi': ["Measure twice. Hit once.", "I already know how this ends."],
               'win': ["Clean cut.", "One swing. That was plenty."]},
    'vee':    {'hi': ["I open at nine and I close whoever's left.", "Got the keys, got the time."],
               'win': ["Locking up. You're done.", "That's closing time."]},
    'dex':    {'hi': ["You did not check this out.", "Everything in its place. Including you."],
               'win': ["Returned, with a late fee.", "Back on the shelf."]},
    'ruth':   {'hi': ["You skipped orientation, didn't you.", "Rule one. Don't."],
               'win': ["Orientation is over.", "Sign here. You lost."]},
    'vic':    {'hi': ["Relax, chief. I have years on you.", "Watch a professional."],
               'win': ["Years of experience, chief.", "Told you so."]},
    'rennick':{'hi': ["It's a shared bike. I'm sharing.", "Relax, man, it's community property."],
               'win': ["No hard feelings. Mostly.", "I'll bring it back. Probably."]},
    'dobbs':  {'hi': ["Brought a hammer. A big one.", "This will not take long."],
               'win': ["Told you it was a hammer.", "Down you go."]},
    'champ':  {'hi': ["Ready? Fight.", "Been waiting all day for this."],
               'win': ["New high score.", "Beat that."]},
    'bex':    {'hi': ["Labeled, dated, or gone.", "You are past your date."],
               'win': ["Into the compost.", "Gone."]},
    'cal':    {'hi': ["Two bucks a pound, partner.", "It's just math."],
               'win': ["Scrap value, paid out.", "Weighed and sold."]},
    'root':   {'hi': ["Ground yourself first.", "Check your connections."],
               'win': ["Clean ground.", "Circuit's open. You're out."]},
    'brant':  {'hi': ["You sure about this?", "I did warn you."],
               'win': ["I told you how it goes.", "And that is that."]},
}

def trim(a, sr):
    nz = np.where(np.abs(a) > 0.01)[0]
    return a[max(0, nz[0] - int(.03 * sr)): min(len(a), nz[-1] + int(.08 * sr))] if len(nz) else a

def kokoro_tag(voice, speed, text):
    return hashlib.sha1(f"{voice}|{speed}|{text}".encode()).hexdigest()[:20]

REF_SECONDS, REF_RATE = 20, 24000
def file_sig(path):
    return hashlib.sha1(open(path, 'rb').read()).hexdigest()[:12] + f'-{REF_SECONDS}s' if os.path.exists(path) else None
def ref_clip(path, out):
    a, sr = sf.read(path, dtype='float32', always_2d=True)
    a = a.mean(axis=1)[: int(REF_SECONDS * sr)]
    m = int(len(a) * REF_RATE / sr)
    sf.write(out, np.interp(np.arange(m) * sr / REF_RATE, np.arange(len(a)), a).astype(np.float32), REF_RATE)
    return out
SIG = {k: file_sig(p) for k, p in KYUTAI.items()}
def kyutai_tag(speaker, text):
    return 'ky' + hashlib.sha1(f"kyutai|{speaker}|{SIG[speaker]}|{text}".encode()).hexdigest()[:18]

# the flat list of takes
takes = []   # (name, fighter, text)
for f, sits in SAY.items():
    for sit, lst in sits.items():
        for t in lst:
            takes.append((f"{f}.{sit}", f, t))

# ---- Kyutai takes for Moheeb and Luis, one batch ----
want = [(nm, f, t) for nm, f, t in takes if f in KYUTAI and SIG.get(f)
        and not os.path.exists(os.path.join(CACHE, kyutai_tag(f, t) + '.wav'))]
kyutai_ok = all(SIG.get(k) for k in KYUTAI)
if want:
    work = tempfile.mkdtemp(prefix='ck-kyutai-')
    spec = {'voices': {k: ref_clip(p, os.path.join(work, f'ref_{k}.wav')) for k, p in KYUTAI.items() if SIG.get(k)},
            'jobs': [{'voice': f, 'text': t, 'out': os.path.join(work, f'{i}.wav')} for i, (nm, f, t) in enumerate(want)]}
    import json
    json.dump(spec, open(os.path.join(work, 'jobs.json'), 'w'))
    print(f"kyutai: {len(want)} takes for {', '.join(sorted({f for _, f, _ in want}))}", flush=True)
    try:
        subprocess.run(KYUTAI_PY + [os.path.join(ROOT, 'tools/voices_kyutai.py'), os.path.join(work, 'jobs.json')], check=True)
        for i, (nm, f, t) in enumerate(want):
            a, sr = sf.read(os.path.join(work, f'{i}.wav'), dtype='float32')
            sf.write(os.path.join(CACHE, kyutai_tag(f, t) + '.wav'), trim(a, sr), sr)
    except (subprocess.CalledProcessError, FileNotFoundError, RuntimeError) as e:
        print(f"kyutai: generator failed ({e}); Moheeb and Luis use Kokoro stand ins this run", flush=True)
        kyutai_ok = False
elif not kyutai_ok:
    print(f"kyutai: no reference for {', '.join(k for k in KYUTAI if not SIG.get(k))}; using Kokoro stand ins", flush=True)

# ---- bake every take, pack ----
kokoro = None
pcm_all = []
index = []   # (name, start_frame, frames)
at = 0
stand_ins = 0
for nm, f, text in takes:
    path = os.path.join(CACHE, kyutai_tag(f, text) + '.wav') if f in KYUTAI and SIG.get(f) else None
    if not path or not os.path.exists(path):
        if f in KYUTAI:
            stand_ins += 1
        voice, speed = VOICE[f]
        path = os.path.join(CACHE, kokoro_tag(voice, speed, text) + '.wav')
        if not os.path.exists(path):
            if kokoro is None:
                from kokoro_onnx import Kokoro
                kokoro = Kokoro(os.path.join(KDIR, 'kokoro-v1.0.onnx'), os.path.join(KDIR, 'voices-v1.0.bin'))
            lang = 'en-gb' if voice.startswith('b') else 'en-us'
            samples, sr = kokoro.create(text, voice=voice, speed=speed, lang=lang)
            sf.write(path, trim(np.asarray(samples, dtype=np.float32), sr), sr)
            print(f"{nm}: {text}", flush=True)
    a, sr = sf.read(path, dtype='float32')
    m = int(len(a) * RATE / sr)
    x = np.interp(np.arange(m) * sr / RATE, np.arange(len(a)), a)
    peak = max(1e-4, float(np.max(np.abs(x))))
    x = np.clip(x * min(3.0, .9 / peak), -1, 1) * 0.5   # half level: the mixer doubles it
    pcm_all.append(x.astype('<f4'))
    index.append((nm, at, m)); at += m

os.makedirs(os.path.join(ROOT, 'assets/voice'), exist_ok=True)
raw = os.path.join(ROOT, 'build/voice-wav/lines.f32')
np.concatenate(pcm_all).tofile(raw)
subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'f32le', '-ar', str(RATE), '-ac', '1',
                '-i', raw, '-c:a', 'flac', '-sample_fmt', 's16', '-compression_level', '12',
                os.path.join(ROOT, 'assets/voice/lines.flac')], check=True)
with open(os.path.join(ROOT, 'assets/voice/lines_index.bin'), 'wb') as fh:
    fh.write(b'CKSI'); fh.write(struct.pack('<I', len(index)))
    for nm, start, frames in index:
        b = nm.encode()
        fh.write(bytes([len(b)])); fh.write(b)
        fh.write(struct.pack('<fII', 0.0, start, frames))
sz = os.path.getsize(os.path.join(ROOT, 'assets/voice/lines.flac'))
print(f"{len(index)} lines, {at / RATE:.0f} s, lines.flac {sz / 1048576:.2f} MB"
      + (f", {stand_ins} Kyutai on Kokoro stand ins" if stand_ins else ""))
