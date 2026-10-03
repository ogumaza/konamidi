#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Compare two renders of the same music, such as output from driver_emu.py and FluidSynth.

    compare_audio.py REFERENCE.wav CANDIDATE.wav

The driver mixes in mono, so both renders are mixed down to mono, and the candidate is resampled to the reference's
rate with ffmpeg. The script reports the correlation of the loudness envelopes in windows of a frame (1/60 s), the lag
at which they correlate best, the correlation of the average spectra in dB up to 6 kHz, and the level ratio between
the reference and the candidate.
"""
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

import numpy as np

# Samples per window for the spectra.
SPECTRUM_WINDOW = 2048
# The highest frequency the spectra compare, in Hz. The driver's mix rate of 13379 Hz has nothing above 6.7 kHz.
SPECTRUM_LIMIT = 6000


def load(path):
    """Returns a WAV file's samples mixed down to mono, as floats, and its rate."""
    with wave.open(str(path)) as w:
        data = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').reshape(-1, w.getnchannels())
        return data.astype(np.float64).mean(axis=1) / 32768, w.getframerate()


def resampled(path, rate):
    with tempfile.TemporaryDirectory() as folder:
        out = Path(folder) / 'resampled.wav'
        subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', str(path), '-ac', '1', '-ar', str(rate), str(out)],
                       check=True)
        return load(out)[0]


def envelope(x, window):
    """Returns the RMS level of each window of x."""
    n = len(x) // window * window
    return np.sqrt((x[:n].reshape(-1, window) ** 2).mean(axis=1))


def average_spectrum(x):
    win = np.hanning(SPECTRUM_WINDOW)
    step = SPECTRUM_WINDOW // 2
    frames = np.array([x[i:i + SPECTRUM_WINDOW] * win for i in range(0, len(x) - SPECTRUM_WINDOW + 1, step)])
    return np.abs(np.fft.rfft(frames, axis=1)).mean(axis=0)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)

    ref, rate = load(sys.argv[1])
    cand = resampled(sys.argv[2], rate)
    n = min(len(ref), len(cand))
    ref, cand = ref[:n], cand[:n]
    ea, eb = envelope(ref, rate // 60), envelope(cand, rate // 60)
    if ea.std() == 0 or eb.std() == 0:
        print('silent in one render')
        return

    # The correlation of the envelopes at lags of up to 6 frames either way.
    lags = {}
    for lag in range(-6, 7):
        a = ea[max(0, lag):len(ea) + min(0, lag)]
        b = eb[max(0, -lag):len(eb) - max(0, lag)]
        lags[lag] = np.corrcoef(a, b)[0, 1]

    top = int(SPECTRUM_LIMIT / (rate / SPECTRUM_WINDOW))
    sa = 20 * np.log10(average_spectrum(ref)[2:top] + 1e-6)
    sb = 20 * np.log10(average_spectrum(cand)[2:top] + 1e-6)
    print('envelope %.3f (best lag %+d frames)  spectrum %.3f  level ratio %.3f' % (
        lags[0], max(lags, key=lags.get), np.corrcoef(sa, sb)[0, 1], np.sum(ea * eb) / np.sum(eb * eb)))


if __name__ == '__main__':
    main()
