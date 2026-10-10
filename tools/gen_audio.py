#!/usr/bin/env python3
"""island placeholder audio generator.

Synthesises cheap but pleasant placeholder masters into
`.scratch/audio_assets/audio/{sfx,music}/` so the desktop backend has
something to play before the real asset checkout (island1-zozu) lands.
Nothing here is committed game content — real masters live in the external
audio_assets tree; this script just unblocks wiring and ear checks.

Usage:
    python3 tools/gen_audio.py

Outputs (all WAV, the source-of-truth format):
    sfx   mono 22050 Hz 16-bit, <= ~0.5 s (under the 65534-sample AICA cap)
    music stereo 44100 Hz 16-bit, ~8 s loops
"""

import math
import struct
import wave
from array import array
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / ".scratch" / "audio_assets"

SFX_RATE = 22050
MUSIC_RATE = 44100


def write_wav(path, rate, channels, samples):
    """samples: array('h') interleaved."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(samples.tobytes())
    print(f"  {path.relative_to(ROOT)}  {len(samples) // channels} frames")


def env_decay(i, n, attack=0.02):
    t = i / n
    a = min(1.0, t / attack) if attack > 0 else 1.0
    return a * math.exp(-3.0 * t)


def tone(rate, secs, freq, *, decay=0.02, harmonic=0.0, noise=0.0):
    n = int(rate * secs)
    out = array("h")
    for i in range(n):
        t = i / rate
        v = math.sin(2.0 * math.pi * freq * t)
        if harmonic:
            v += harmonic * math.sin(2.0 * math.pi * freq * 2.0 * t)
        if noise:
            # cheap deterministic "percussive" noise from the index
            v += noise * (((i * 1103515245 + 12345) >> 16) & 0x7FFF) / 32768.0 - 0.5
        out.append(int(max(-1.0, min(1.0, v * env_decay(i, n, decay))) * 30000))
    return out


def chord(rate, secs, freqs, amp=0.22):
    n = int(rate * secs)
    out = array("h")
    for i in range(n):
        t = i / rate
        # Gentle fade at the loop seam so looping the file does not click.
        seam = min(1.0, min(i, n - 1 - i) / (rate * 0.05))
        v = 0.0
        for k, f in enumerate(freqs):
            v += math.sin(2.0 * math.pi * f * t) / (k + 1.5)
        out.append(int(v * amp * seam * 30000))
    return out


def main():
    sfx = OUT / "audio" / "sfx"
    music = OUT / "audio" / "music"

    print("placeholder sfx:")
    write_wav(sfx / "ui_move.wav", SFX_RATE, 1, tone(SFX_RATE, 0.08, 880, decay=0.01))
    write_wav(sfx / "ui_confirm.wav", SFX_RATE, 1,
              tone(SFX_RATE, 0.16, 660, harmonic=0.4, decay=0.01))
    write_wav(sfx / "ui_back.wav", SFX_RATE, 1,
              tone(SFX_RATE, 0.12, 440, harmonic=0.3, decay=0.01))
    write_wav(sfx / "delivery.wav", SFX_RATE, 1,
              array("h", tone(SFX_RATE, 0.12, 523) + tone(SFX_RATE, 0.20, 784)))
    write_wav(sfx / "pickup.wav", SFX_RATE, 1,
              array("h", tone(SFX_RATE, 0.08, 523) + tone(SFX_RATE, 0.12, 659)))
    write_wav(sfx / "drop.wav", SFX_RATE, 1,
              tone(SFX_RATE, 0.14, 220, noise=0.6, decay=0.005))
    # Engine: exactly 1.000 s at 22050 = a whole number of samples, and the
    # waveform is seam-continuous, so the hardware loop point is clickless.
    write_wav(sfx / "engine.wav", SFX_RATE, 1,
              tone(SFX_RATE, 1.0, 90, harmonic=0.5, decay=0.0))

    print("placeholder music:")
    write_wav(music / "title.wav", MUSIC_RATE, 2,
              chord(MUSIC_RATE, 8.0, [220.0, 277.2, 329.6, 440.0]))
    write_wav(music / "gameplay.wav", MUSIC_RATE, 2,
              chord(MUSIC_RATE, 8.0, [196.0, 246.9, 293.7, 392.0]))

    print(f"done -> {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
