"""Generates the *placeholder* sounds in content/sounds/.

Synthesised stand-ins (no recordings) so the engine's audio can be built and
tested: footsteps on dirt and grass, wind, rustling leaves, birds and night
insects. Final sound comes from a sound designer or field recordings; replace
the .wav files (or add .flac/.mp3 with the same names) and the game uses them.

Usage:  python tools/make_placeholder_sounds.py
Needs:  numpy
"""
import math
import os
import wave

import numpy as np

OUT = os.path.join(os.path.dirname(__file__), "..", "content", "sounds")
RATE = 32000


def save(name, x, peak_db=-3.0):
    os.makedirs(OUT, exist_ok=True)
    x = np.asarray(x, dtype=np.float64)
    x = x / (np.max(np.abs(x)) + 1e-9) * (10 ** (peak_db / 20))
    data = (np.clip(x, -1, 1) * 32767).astype("<i2").tobytes()
    with wave.open(os.path.join(OUT, name), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(data)
    print(f"wrote {name} ({len(x) / RATE:.2f} s)")


def t_axis(seconds):
    return np.arange(int(seconds * RATE)) / RATE


def one_pole_lowpass(x, cutoff):
    a = math.exp(-2 * math.pi * cutoff / RATE)
    y = np.empty_like(x)
    acc = 0.0
    for i, v in enumerate(x):
        acc = (1 - a) * v + a * acc
        y[i] = acc
    return y


def bandpass(x, lo, hi):
    return one_pole_lowpass(x, hi) - one_pole_lowpass(x, lo)


def seamless(x, fade_seconds):
    """Make a loop: crossfade the extra tail into the start."""
    f = int(fade_seconds * RATE)
    body, tail = x[:-f].copy(), x[-f:]
    ramp = np.linspace(0, 1, f)
    body[:f] = body[:f] * ramp + tail * (1 - ramp)
    return body


def footstep(rng, surface):
    t = t_axis(0.22)
    thump_f = rng.uniform(65, 95)
    thump = np.sin(2 * math.pi * thump_f * t) * np.exp(-t / 0.035)
    noise = rng.standard_normal(len(t))
    if surface == "dirt":
        grit = bandpass(noise, 600, 3200) * np.exp(-t / 0.05) * 1.4
        scuff = bandpass(rng.standard_normal(len(t)), 1500, 5000) * np.exp(-np.maximum(t - 0.05, 0) / 0.03) * (t > 0.05) * 0.4
        return thump * 0.9 + grit + scuff
    swish = bandpass(noise, 1800, 7000) * np.exp(-t / 0.09) * 1.1
    return thump * 0.5 + swish


def wind(rng, seconds=12.0):
    n = rng.standard_normal(int((seconds + 2) * RATE))
    low = one_pole_lowpass(n, 350) * 6
    mid = bandpass(n, 300, 1200) * 1.5
    t = t_axis(seconds + 2)
    # Slow gusts; periods divide the loop length so the loop is seamless.
    gust = 0.65 + 0.2 * np.sin(2 * math.pi * t / (seconds / 2)) + 0.15 * np.sin(2 * math.pi * t / (seconds / 5) + 1.3)
    return seamless((low + mid) * gust, 2.0)


def leaves(rng, seconds=10.0):
    n = rng.standard_normal(int((seconds + 1.5) * RATE))
    rustle = bandpass(n, 2000, 8000)
    flutter = one_pole_lowpass(np.abs(rng.standard_normal(len(n))), 6) * 3
    return seamless(rustle * flutter, 1.5)


def bird(rng):
    notes = rng.integers(2, 6)
    parts = []
    for _ in range(notes):
        d = rng.uniform(0.05, 0.13)
        t = t_axis(d)
        f0, f1 = rng.uniform(2600, 4200), rng.uniform(3000, 5200)
        freq = f0 + (f1 - f0) * (t / d) ** rng.uniform(0.5, 2.0)
        phase = 2 * math.pi * np.cumsum(freq) / RATE
        env = np.sin(math.pi * t / d) ** 2
        parts.append((np.sin(phase) + 0.15 * np.sin(2 * phase)) * env)
        parts.append(np.zeros(int(rng.uniform(0.03, 0.09) * RATE)))
    return np.concatenate(parts + [np.zeros(int(0.05 * RATE))])


def insects(rng, seconds=6.0):
    t = t_axis(seconds)
    out = np.zeros_like(t)
    for f, chirp_every, offset, level in ((4600, 0.5, 0.0, 1.0), (5100, 0.6, 0.21, 0.7), (4300, 0.75, 0.4, 0.5)):
        tone = np.sin(2 * math.pi * f * t)
        pulses = np.zeros_like(t)
        start = offset
        while start < seconds:
            for k in range(3):
                s = start + k * 0.035
                idx = (t >= s) & (t < s + 0.02)
                pulses[idx] = np.sin(math.pi * (t[idx] - s) / 0.02)
            start += chirp_every
        out += tone * pulses * level
    hum = bandpass(rng.standard_normal(len(t)), 200, 900) * 0.05
    return out + hum


if __name__ == "__main__":
    rng = np.random.default_rng(2026)
    for i in range(4):
        save(f"footstep_dirt_{i + 1}.wav", footstep(rng, "dirt"))
        save(f"footstep_grass_{i + 1}.wav", footstep(rng, "grass"))
    save("wind_loop.wav", wind(rng), peak_db=-6)
    save("leaves_loop.wav", leaves(rng), peak_db=-6)
    for i in range(4):
        save(f"bird_{i + 1}.wav", bird(rng), peak_db=-6)
    save("insects_loop.wav", insects(rng), peak_db=-6)
