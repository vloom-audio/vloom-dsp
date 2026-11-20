# 0004. An eight-line feedback delay network for the reverb

- Status: accepted
- Date: 2026-11-20

## Context

The built-in reverb (`effects/Reverb`, "vloom.reverb") must sound smooth on
anything from a short room to a long hall, decay at a set RT60, run at
44.1 to 192 kHz with every buffer sized in `prepare()`, and stay cheap
enough for many instances. It must be our own MIT code, with no third-party
DSP.

## Decision

A feedback delay network (FDN) of eight delay lines:

- Input: each side through a pre-delay (up to 500 ms) and four Schroeder
  all-passes (diffusion, coefficient up to 0.7), then into alternate lines
  with alternating signs.
- Loop: eight lines, 29.7 .. 59.1 ms at size 1 (scaled 0.3 .. 2.0 by Size,
  exponentially), mixed by a normalized 8x8 Hadamard matrix (a fast
  Walsh-Hadamard transform: 24 additions, lossless). Each line has a gain
  for the decay time, `10^(-3 length / (RT60 rate))`, and a one-pole
  low-pass for damping (20 kHz at 0 % down to 1.5 kHz at 100 %).
- Each line's read point is modulated by a slow sine (0.37 .. 0.97 Hz,
  0.3 ms deep) so its resonances do not ring metallic.
- Output: two orthogonal sign patterns over the lines give left and right;
  then low cut and high cut (state variable filters), width (mid/side) and
  the dry/wet mix.
- Freeze: line gains go to 1, damping and modulation to 0, input to 0 (all
  gliding), so the tail holds without growing (the matrix is lossless and
  integer delays are read without interpolation).

## Alternatives considered

- Freeverb (8 parallel combs + 4 all-passes per side): simple and well known,
  but its combs ring metallic on percussive input, its decay time is not set
  directly, and stereo is two loosely related mono reverbs.
- Dattorro's plate (figure-of-eight tank): a lovely plate, but its delay
  lengths are tuned for 29.8 kHz and scale awkwardly with size, and its
  density is fixed by its topology.
- Convolution: realistic, but needs impulse responses (assets and licences),
  more memory, and FFT code; a later, separate effect.
- A 16-line FDN: denser, about twice the cost; eight lines with input
  diffusion and modulation are already smooth.

## Consequences

- The decay time is exact by construction for low frequencies; damping
  makes highs decay faster (measured in tests).
- Cost per stereo frame: 8 line reads with linear interpolation, the
  Hadamard transform, 8 all-passes, 4 state variable filters, 8 phasor
  updates: no transcendental functions per sample (pow and exp only in the
  control step, every 32 frames while Size or Freeze glide).
- Memory: about 125 ms per line plus 500 ms of pre-delay per side, rounded
  up to powers of two: about 2 MB at 192 kHz.
- Changing Size glides the line lengths over 200 ms in small steps (a
  gentle pitch wobble in the tail, no click).
