#!/usr/bin/env python3
"""16-mic array geometry design tool for SongBeam (2x PCMD3180).

Evaluates candidate layouts and optimizes mic x-positions to minimize the
worst peak sidelobe of the delay-and-sum array factor over the songbird band,
for a set of steering angles. Positions are for the azimuth (x) axis; build
the board as a 2-row stagger (alternate mics between rows ~31.5 mm apart,
SongBeam4.0 style) so dense x-projections remain physically buildable.

Result of the shipped run (seed=3, aperture<=400mm, min x-gap 5mm,
cost = worst PSL over steer {0, 25 deg}, band 2-8 kHz):
  x (mm) = +/-2.9, +/-24.1, +/-51.1, +/-72.2, +/-94.6, +/-118.0, +/-142.6, +/-171.2
  PSL -16.9 dB for steer <= 25 deg (uniform lambda/2: -13.1 dB), BW@4kHz ~12 deg.
"""
import numpy as np

C = 343.0
FREQS = np.arange(2000, 8001, 750)          # evaluation band, Hz
TH = np.radians(np.linspace(-90, 90, 1081)) # azimuth grid
APERTURE_MAX = 0.40                          # m
MIN_XGAP = 0.005                             # m, x-projection (rows relax physical gap)
STEERS = [0.0, np.radians(25)]               # angles the array must stay clean at
N = 16


def psl(x, steer):
    """Worst peak sidelobe (dB) over FREQS for a steered delay-and-sum beam."""
    worst = -99.0
    for f in FREQS:
        k = 2 * np.pi * f / C
        af = np.abs(np.exp(1j * k * np.outer(np.sin(TH) - np.sin(steer), x)).sum(1)) / len(x)
        afdb = 20 * np.log10(af + 1e-12)
        i0 = np.argmin(np.abs(TH - steer))
        i = i0
        while i - 1 >= 0 and af[i - 1] < af[i]: i -= 1
        while i - 1 >= 0 and not af[i] < af[i - 1]: i -= 1
        j = i0
        while j + 1 < len(af) and af[j + 1] < af[j]: j += 1
        while j + 1 < len(af) and not af[j] < af[j + 1]: j += 1
        worst = max(worst, max(afdb[:i].max(initial=-99), afdb[j:].max(initial=-99)))
    return worst


def cost(x):
    return max(psl(x, s) for s in STEERS)


def optimize(iters=2500, seed=3):
    rng = np.random.default_rng(seed)
    x = (np.arange(N) - (N - 1) / 2) * C / (2 * FREQS.max())  # start: uniform lambda/2
    best = cost(x)
    for _ in range(iters):
        xn = x.copy()
        i = rng.integers(0, N // 2)
        s = rng.normal(0, 0.003)
        xn[i] += s
        xn[N - 1 - i] -= s                                     # keep symmetric
        xn = np.sort(xn)
        if xn.max() - xn.min() > APERTURE_MAX or np.diff(xn).min() < MIN_XGAP:
            continue
        cn = cost(xn)
        if cn < best:
            x, best = xn, cn
    return x, best


if __name__ == "__main__":
    uni = (np.arange(N) - (N - 1) / 2) * C / (2 * FREQS.max())
    print("uniform lambda/2 baseline:")
    for s in (0, 15, 30, 45):
        print(f"  steer {s:2d} deg: PSL {psl(uni, np.radians(s)):6.1f} dB")
    x, best = optimize()
    print(f"\noptimized (aperture {1000*(x.max()-x.min()):.0f} mm):")
    for s in (0, 15, 30, 45):
        print(f"  steer {s:2d} deg: PSL {psl(x, np.radians(s)):6.1f} dB")
    print("positions (mm):", np.round(np.sort(x) * 1000, 1))
