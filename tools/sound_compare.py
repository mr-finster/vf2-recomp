#!/usr/bin/env python3
"""Compares vf2run's sound with MAME's: the SCSP's own output, float32 stereo at 44.1 kHz
(vf2run --raw-audio, tools/mame_audio.sh's mame_scsp.raw).
   tools/sound_compare.py <ours.raw> <mame.raw> [seconds per line (default 1)]
Per stretch: loudness (RMS) of each, samples identical, the largest difference, the best
alignment (lag in samples, from a cross-correlation) and how alike the two are at that lag
(correlation), and how alike their spectra are.
Our SCSP departs from MAME's on purpose in two places: 0x7c0-0x7ff is its own storage, not a
mirror of the DSP's MADRS (src/machine/scsp.h, m_madrs_hi), which VF2's reverb needs. Against
MAME, run vf2run with VF2_SCSP_MADRS_MIRROR=1 (MAME's mirror): without it the two differ from
the sound program's first DSP set-up on, as intended. And its timers run a sample longer than
MAME's (src/machine/scsp.cpp, timer_samples: the music 2% slower, as the arcade's): add
VF2_SCSP_TIMER_MAME=1 (MAME's period), or the two drift apart from the music's first tick."""
import sys
import numpy as np

RATE = 44100
a = np.fromfile(sys.argv[1], dtype=np.float32).reshape(-1, 2)
b = np.fromfile(sys.argv[2], dtype=np.float32).reshape(-1, 2)
step = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
n = min(len(a), len(b))
print(f"ours {len(a)} samples ({len(a) / RATE:.2f} s), MAME's {len(b)} ({len(b) / RATE:.2f} s)")
same = np.all(a[:n] == b[:n], axis=1)
print(f"identical samples: {same.sum()} of {n} ({100.0 * same.sum() / max(n, 1):.2f}%)")
if not same.all():
    first = int(np.argmin(same))
    print(f"first difference at sample {first} ({first / RATE:.4f} s)")


def lag_and_corr(x, y, maxlag=2048):
    x = x - x.mean(); y = y - y.mean()
    if not x.any() or not y.any():
        return 0, float("nan")
    m = len(x)
    size = 1 << int(np.ceil(np.log2(2 * m)))
    c = np.fft.irfft(np.fft.rfft(x, size) * np.conj(np.fft.rfft(y, size)), size)
    c = np.concatenate((c[-maxlag:], c[:maxlag + 1]))
    k = int(np.argmax(c)) - maxlag
    norm = np.sqrt((x * x).sum() * (y * y).sum())
    return k, float(c[k + maxlag] / norm)


def spectrum(x):
    w = np.hanning(len(x))
    s = np.abs(np.fft.rfft(x * w))
    return s / (np.linalg.norm(s) or 1)


seg = int(step * RATE)
print(f"{'time':>8} {'rms ours':>9} {'rms MAME':>9} {'same %':>7} {'max diff':>9} {'lag':>6} {'corr':>7} {'spectrum':>8}")
for s0 in range(0, n, seg):
    s1 = min(s0 + seg, n)
    x = a[s0:s1].mean(axis=1); y = b[s0:s1].mean(axis=1)
    if len(x) < 1024:
        break
    ra, rb = np.sqrt((x * x).mean()), np.sqrt((y * y).mean())
    eq = 100.0 * same[s0:s1].mean()
    md = float(np.abs(a[s0:s1] - b[s0:s1]).max())
    lag, corr = lag_and_corr(x, y)
    sa, sb = spectrum(x), spectrum(y)
    spec = float((sa * sb).sum()) if ra > 0 and rb > 0 else float("nan")
    print(f"{s0 / RATE:8.2f} {ra:9.5f} {rb:9.5f} {eq:7.2f} {md:9.5f} {lag:6d} {corr:7.4f} {spec:8.4f}")
