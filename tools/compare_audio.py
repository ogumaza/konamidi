#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Compare two renders of the same music (e.g. driver_emu.py against fluidsynth).

    compare_audio.py REFERENCE.wav CANDIDATE.wav

The candidate is resampled to the reference's rate with ffmpeg. For each side,
the script reports the correlation of the 1024-sample loudness envelopes (DC
removed), the best envelope lag, the correlation of the average spectra in dB
up to 8 kHz, and the reference/candidate level ratio.
"""
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

import numpy as np

# Samples per window for the loudness envelopes and the spectra, and the step between spectrum windows.
WINDOW = 1024
HOP = 256


def load(path):
    with wave.open(str(path)) as w:
        data = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').reshape(-1, w.getnchannels())
        return data.astype(np.float32) / 32768, w.getframerate()


def resampled(path, rate):
    with tempfile.TemporaryDirectory() as folder:
        out = Path(folder) / 'resampled.wav'
        subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', path, '-ar', str(rate), str(out)], check=True)
        return load(out)[0]


def envelope(x):
    return np.array([x[i:i + WINDOW].std() for i in range(0, len(x) - WINDOW + 1, WINDOW)])


def average_spectrum(x):
    win = np.hanning(WINDOW)
    frames = np.array([x[i:i + WINDOW] * win for i in range(0, len(x) - WINDOW + 1, HOP)])
    return np.abs(np.fft.rfft(frames, axis=1)).mean(axis=0)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    ref, rate = load(sys.argv[1])
    cand = resampled(sys.argv[2], rate)
    n = min(len(ref), len(cand))
    ref, cand = ref[:n], cand[:n]
    for c, side in ((0, 'left'), (1, 'right')):
        ea, eb = envelope(ref[:, c]), envelope(cand[:, c])
        if ea.std() == 0 or eb.std() == 0:
            print('%-5s silent in one render' % side)
            continue
        lags = {}
        for lag in range(-4, 5):
            a = ea[max(0, lag):len(ea) + min(0, lag)]
            b = eb[max(0, -lag):len(eb) - max(0, lag)]
            lags[lag] = np.corrcoef(a, b)[0, 1]
        kmax = int(8000 / (rate / WINDOW))
        sa = 20 * np.log10(average_spectrum(ref[:, c])[2:kmax] + 1e-6)
        sb = 20 * np.log10(average_spectrum(cand[:, c])[2:kmax] + 1e-6)
        print('%-5s envelope %.3f (best lag %+d)  spectrum %.3f  level ratio %.3f' % (
            side, lags[0], max(lags, key=lags.get), np.corrcoef(sa, sb)[0, 1], np.sum(ea * eb) / np.sum(eb * eb)))


if __name__ == '__main__':
    main()
