#!/usr/bin/env python3
"""island Dreamcast audio conversion.

Turns the WAV masters (source of truth, island1-zozu) into the headerless
formats the KOS backends load (island1-rau9):

    sfx   -> 8-bit signed mono 22050 Hz raw, padded to a 32-byte multiple
             (snd_sfx_load_raw_buf; KOS documents the 32-byte alignment)
    music -> interleaved Yamaha/AICA ADPCM stereo 44.1 kHz, no header
             (snd_stream_start_adpcm), produced by the KOS wav2adpcm tool
             with the same flags wipeout-dc's HOWTO uses: -n -i -t

Desktop never runs this — it plays the WAV masters directly. Meson invokes
it for the dreamcast build only, and only when an audio_assets checkout (or
.scratch/audio_assets placeholders) is present.

Usage:
    python3 scripts/convert_audio.py --src audio_assets --out build-dc/src/assets
    python3 scripts/convert_audio.py --src .scratch/audio_assets --out /tmp/out

WAV2ADPCM override: set the WAV2ADPCM environment variable if the tool is
not at the default KOS path.
"""

import argparse
import os
import struct
import subprocess
import sys
import wave
from array import array
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SFX_RATE = 22050
SFX_ALIGN = 32
WAV2ADPCM_DEFAULT = "/opt/toolchains/dc/kos/utils/wav2adpcm/wav2adpcm"


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


def default_src():
    if (ROOT / "audio_assets" / "audio").is_dir():
        return ROOT / "audio_assets"
    return ROOT / ".scratch" / "audio_assets"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
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
