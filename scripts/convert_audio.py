#!/usr/bin/env python3
"""island audio conversion (Dreamcast + PSP).

Turns the WAV masters (source of truth, island1-zozu) into the per-console
formats the backends load:

  Dreamcast (island1-rau9):
    sfx   -> 8-bit signed mono 22050 Hz raw, padded to a 32-byte multiple
             (snd_sfx_load_raw_buf; KOS documents the 32-byte alignment)
    music -> interleaved Yamaha/AICA ADPCM stereo 44.1 kHz, no header
             (snd_stream_start_adpcm), produced by the KOS wav2adpcm tool
             with the same flags wipeout-dc's HOWTO uses: -n -i -t

  PSP (island1-ym91):
    sfx   -> mono 16-bit WAV, 44.1 kHz (downmixed from the stereo masters),
             resident in RAM (~tens of KB each). Normalizing to mono here
             keeps the PSP mixer's hot loop single-sample-per-frame; the
             masters stay untouched.
    music -> 128 kbps MP3, 44.1 kHz stereo (lame). sceMp3 decodes on the Media
             Engine; a 1-minute track is ~1 MB vs ~10 MB as WAV, and streams
             from UMD without stalling the mixer.

Desktop never runs this — it plays the WAV masters directly. Meson invokes it
for the dreamcast and psp builds only, and only when an audio_assets checkout
(or .scratch/audio_assets placeholders) is present.

Usage:
    python3 scripts/convert_audio.py --target dc   --src audio_assets --out build-dc/src/assets
    python3 scripts/convert_audio.py --target psp --src audio_assets --out build_psp/src/assets

Overrides: WAV2ADPCM (dc music tool path), LAME (psp mp3 encoder path).
"""

import argparse
import os
import shutil
import subprocess
import sys
import wave
from array import array
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SFX_RATE = 22050
SFX_ALIGN = 32
WAV2ADPCM_DEFAULT = "/opt/toolchains/dc/kos/utils/wav2adpcm/wav2adpcm"
LAME_DEFAULT = "lame"
# sceMp3 decodes on the Media Engine; 128 kbps stereo is the sweet spot for
# game music (small UMD footprint, transparent quality). 44.1 kHz matches the
# mixer output rate, so no resampling is needed on the PSP.
PSP_MP3_BITRATE = "128"
PSP_MP3_RATE = "44100"


def read_wav_mono16(path):
    """Return (rate, array('h') mono samples). Downmixes stereo."""
    with wave.open(str(path), "rb") as w:
        ch = w.getnchannels()
        width = w.getsampwidth()
        rate = w.getframerate()
        raw = w.readframes(w.getnframes())

    if width == 2:
        samples = array("h")
        samples.frombytes(raw)
        if sys.byteorder == "big":
            samples.byteswap()
    elif width == 1:  # unsigned 8-bit
        samples = array("h", [((b - 128) << 8) for b in raw])
    else:
        raise SystemExit(f"{path}: unsupported sample width {width * 8} bit")

    if ch == 2:
        mono = array("h")
        for i in range(0, len(samples) - 1, 2):
            mono.append((samples[i] + samples[i + 1]) // 2)
        samples = mono
    elif ch != 1:
        raise SystemExit(f"{path}: unsupported channel count {ch}")
    return rate, samples


def resample_linear(samples, src_rate, dst_rate):
    if src_rate == dst_rate:
        return samples
    n_out = int(len(samples) * dst_rate / src_rate)
    out = array("h")
    step = src_rate / dst_rate
    for i in range(n_out):
        pos = i * step
        i0 = int(pos)
        frac = pos - i0
        s0 = samples[i0] if i0 < len(samples) else 0
        s1 = samples[i0 + 1] if i0 + 1 < len(samples) else s0
        out.append(int(s0 + (s1 - s0) * frac))
    return out


def to_sfx_raw(src_wav, dst_raw):
    rate, samples = read_wav_mono16(src_wav)
    samples = resample_linear(samples, rate, SFX_RATE)
    if len(samples) > 65534:
        print(f"  warn: {src_wav.name} is {len(samples)} samples; "
              f"AICA caps at 65534, truncating", file=sys.stderr)
        samples = samples[:65534]

    raw8 = array("b")
    for s in samples:
        v = s >> 8
        raw8.append(-128 if v < -128 else (127 if v > 127 else v))
    pad = (-len(raw8)) % SFX_ALIGN
    raw8.extend(b"\x00" * pad)

    dst_raw.parent.mkdir(parents=True, exist_ok=True)
    dst_raw.write_bytes(raw8.tobytes())
    print(f"  {dst_raw.name}: {len(raw8)} bytes (8-bit mono {SFX_RATE} Hz)")


def to_music_adpcm(src_wav, dst_adpcm, wav2adpcm):
    if not wav2adpcm.is_file():
        raise SystemExit(f"wav2adpcm not found at {wav2adpcm} "
                         f"(set WAV2ADPCM to override)")
    dst_adpcm.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(wav2adpcm), "-n", "-i", "-t",
                    str(src_wav), str(dst_adpcm)], check=True)
    size = dst_adpcm.stat().st_size
    print(f"  {dst_adpcm.name}: {size} bytes (interleaved AICA ADPCM)")


def to_music_mp3(src_wav, dst_mp3, lame):
    """128 kbps / 44.1 kHz stereo MP3 via lame (PSP sceMp3 source)."""
    if shutil.which(lame) is None and not Path(lame).is_file():
        raise SystemExit(f"lame not found at {lame} (set LAME to override)")
    dst_mp3.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        str(lame), "--silent", "-b", PSP_MP3_BITRATE,
        "--resample", PSP_MP3_RATE, "-m", "j",
        str(src_wav), str(dst_mp3),
    ], check=True)
    size = dst_mp3.stat().st_size
    print(f"  {dst_mp3.name}: {size} bytes (MP3 {PSP_MP3_BITRATE} kbps "
          f"{PSP_MP3_RATE} Hz stereo)")


def to_sfx_wav_mono(src_wav, dst_wav):
    """PSP SFX: downmix masters (stereo or mono) to mono 16-bit WAV, resident
    in RAM. Masters stay untouched; normalizing to mono here keeps the PSP
    mixer's hot loop single-sample-per-frame. Rate is kept (44.1 kHz ==
    PSP_OUT_RATE, so the mixer's voice_step is a no-op resample)."""
    rate, samples = read_wav_mono16(src_wav)
    dst_wav.parent.mkdir(parents=True, exist_ok=True)
    if sys.byteorder == "big":
        samples = array("h", samples)
        samples.byteswap()
    with wave.open(str(dst_wav), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(samples.tobytes())
    print(f"  {dst_wav.name}: {dst_wav.stat().st_size} bytes "
          f"(mono WAV {rate} Hz, resident)")


def default_src():
    if (ROOT / "audio_assets" / "audio").is_dir():
        return ROOT / "audio_assets"
    return ROOT / ".scratch" / "audio_assets"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--target", choices=["dc", "psp"], default="dc",
                    help="console to convert for (default: dc)")
    ap.add_argument("--src", type=Path, default=None,
                    help="master tree (default: audio_assets, else "
                         ".scratch/audio_assets)")
    ap.add_argument("--out", type=Path, required=True,
                    help="output dir; audio/{sfx,music} is created under it")
    args = ap.parse_args()

    src = args.src or default_src()
    sfx_dir = src / "audio" / "sfx"
    music_dir = src / "audio" / "music"
    if not sfx_dir.is_dir() and not music_dir.is_dir():
        print(f"no audio masters under {src}; run tools/gen_audio.py for "
              f"placeholders or check out audio_assets", file=sys.stderr)
        return 1

    if args.target == "psp":
        lame = os.environ.get("LAME", LAME_DEFAULT)
        if sfx_dir.is_dir():
            for wav in sorted(sfx_dir.glob("*.wav")):
                to_sfx_wav_mono(wav, args.out / "audio" / "sfx" / f"{wav.stem}.wav")
        if music_dir.is_dir():
            for wav in sorted(music_dir.glob("*.wav")):
                to_music_mp3(wav, args.out / "audio" / "music" /
                             f"{wav.stem}.mp3", lame)
        return 0

    wav2adpcm = Path(os.environ.get("WAV2ADPCM", WAV2ADPCM_DEFAULT))
    if sfx_dir.is_dir():
        for wav in sorted(sfx_dir.glob("*.wav")):
            to_sfx_raw(wav, args.out / "audio" / "sfx" / f"{wav.stem}.raw")
    if music_dir.is_dir():
        for wav in sorted(music_dir.glob("*.wav")):
            to_music_adpcm(wav, args.out / "audio" / "music" /
                           f"{wav.stem}.adpcm", wav2adpcm)
    return 0


if __name__ == "__main__":
    sys.exit(main())
