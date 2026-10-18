# SongBeam4D — SongBeam4C + TDOA delay-and-sum beamforming

Extends SongBeam4C: the four mic channels can be spatially filtered
(steered at a target coordinate) in real time, with the beamformed mono
signal written to SD as `<DeviceID>BEAM_<timestamp>.wav` alongside (or
instead of) the raw 4-channel file. With `BeamMode: 0` the sketch behaves
exactly like SongBeam4C.

## How it works

Near-field delay-and-sum: for the target point, the acoustic path length to
each mic gives a per-channel arrival delay. Each channel is passed through a
64-tap Hann-windowed-sinc fractional-delay FIR (CMSIS-DSP, q15) so that all
four channels are time-aligned for the target, then summed (each filter
carries gain 1/4, so a source at the target keeps its original level while
off-axis sources add incoherently). Offline simulation: unity gain on
target, ~-11 dB at 30-60 degrees off axis for a 2-8 kHz broadband source.

Note: with the 12 cm aperture the array is far-field beyond ~0.5 m, so the
target coordinate effectively selects a *direction* (and, since the array
is linear, a cone around the array axis) — not a range-limited spot.

## Geometry (from Hardware/SongBeam3.1 netlist and PCB)

Mics A1..A4 on a line; X positions relative to array centre:
A1 = -60 mm, A2 = -15 mm, A3 = +15 mm, A4 = +60 mm.

Wiring: I2S1 (WAV ch 1,2) <- IC5 <- mics A3, A4; I2S2 (WAV ch 3,4) <- IC1
<- mics A1, A2. Assumed L/R slot within each pair: SELECT=1V8 mic (A3, A1)
= left. So WAV channels map to mic X = [+15, +60, -60, -15] mm.

Target coordinates: origin = array centre, X along the mic line (positive
towards A4), Y = broadside (out of the board front), Z = vertical. Config
values in cm.

## config.txt additions (same key-on-one-line, value-on-next format)

```
BeamMode:
2
TargetXcm:
0
TargetYcm:
200
TargetZcm:
0
Bus2Offset:
0
```

- `BeamMode` — 0 raw 4ch only (default), 1 beam only, 2 raw + beam.
- `TargetXcm/Ycm/Zcm` — target point in cm (atoi: integers, negatives OK).
- `Bus2Offset` — samples that channels 3/4 (I2S2) lag channels 1/2 in the
  raw file (see calibration). Can be negative.

When `BeamMode > 0` the CPU clock is raised to 150 MHz during operation
(24 MHz is too slow for the FIRs); expect higher power draw than plain
recording.

## One-time calibration (do both once per board revision)

1. **Channel mapping.** Record raw 4ch, scratch next to mic A1 (the one
   60 mm left of centre). It should be loudest on WAV channel 3. If the L/R
   slots are swapped, exchange the entries within each pair of `micX[]` in
   the sketch (ch1<->ch2, ch3<->ch4).
2. **Bus offset.** The two I2S peripherals share the audio PLL but may
   start with a fixed offset of a sample or two. Clap or click from far
   broadside (Y axis, several metres, centred), record raw, and
   cross-correlate ch1 vs ch3 in the file. The lag of ch3 in samples
   (positive = ch3 later) is `Bus2Offset`. Usually 0..2 and constant for a
   given sketch build.
