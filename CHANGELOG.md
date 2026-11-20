# Changelog

All notable changes to vloom-dsp are listed here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- `SidechainEffect` and `TempoAware` (`Processor.hpp`, ADR 0003): optional
  capabilities. A sidechain effect also has `processWithSidechain(channels,
  key, numChannels, numKeyChannels, frames)`; a tempo-aware processor has
  `setTempo(bpm)`. Effects without them are wrapped as before.
- `effects/Compressor` ("vloom.compressor"): threshold, ratio, attack,
  release, soft knee, peak or RMS detector, stereo link, makeup and auto
  makeup, dry/wet mix; sidechain key with a high-pass filter and Listen;
  `gainReductionDb()` for meters, `staticGainDb()` and `transferDb()` for
  curves. No latency.
- `effects/Limiter` ("vloom.limiter"): brickwall look-ahead limiter (0.5 ..
  10 ms), input gain, ceiling, release, stereo link, true peak detection
  (4x interpolation). No sample ever goes over the ceiling. Latency: the
  look-ahead, +4 frames with true peak; it changes with those parameters.
- `effects/Gate` ("vloom.gate"): threshold, hysteresis, range, attack, hold,
  release; sidechain key with a high-pass filter and Listen. No latency.
- `effects/Delay` ("vloom.delay"): stereo or ping-pong, time in ms or a note
  value synced to the host tempo (TempoAware), stereo offset, feedback with
  low and high cut, width, mix; up to 4 s. No latency.
- `effects/Reverb` ("vloom.reverb"): an eight-line feedback delay network
  (ADR 0004) with pre-delay, size, decay (RT60), diffusion, damping, freeze,
  low and high cut, width and mix. No latency.
- `effects/Saturator` ("vloom.saturator"): drive into a soft, tube, tape or
  hard curve at 2x or 4x oversampling, DC blocker, tilt tone, output, mix.
  Latency 31 frames (2x) or 36 (4x).
- `effects/Utility` ("vloom.utility"): gain, balance, stereo width, mono,
  polarity per side, swap, DC blocker; every switch fades. No latency.
- `effects/Filter` ("vloom.filter"): low-pass, high-pass, band-pass or notch,
  12 or 24 dB per octave, resonance, drive; built from state variable
  filters so cutoff and resonance can move every sample. No latency.
- Building blocks: `Svf.hpp` (trapezoidal state variable filter),
  `DelayLine`, `Oversampler` (half-band FIR, 2x/4x, whole-frame latency),
  `SidechainFilter`, `SharedValue` (a copyable lock-free value for meters),
  `ParameterValues.hpp` (defaults and clamping for id-indexed parameters).
- Tests: a check that `process()` allocates nothing (the test program
  counts calls to operator new), shared effect checks (parameter lists,
  silence, every parameter at its ends and NaN at 44.1 .. 192 kHz).
- `NoteValue.hpp`: the note lengths (4/1 .. 1/32, dotted and triplet) a
  tempo-synced "Note" parameter chooses from, with `noteSeconds()` and
  `noteHz()`.
- `effects/Chorus` ("vloom.chorus"): 1 .. 4 voices on one modulated delay
  line, delay, depth, feedback, stereo spread and width, mix; the LFO runs in
  Hz or synced to the tempo.
- `effects/Flanger` ("vloom.flanger"): a swept short delay with positive or
  negative feedback (+-95 %), stereo phase, mix; free or tempo-synced LFO.
- `effects/Phaser` ("vloom.phaser"): 2 .. 12 all-pass stages (one notch per
  two), center frequency, depth, feedback, stereo phase, mix; free or
  tempo-synced LFO. Changing the stage count fades.
- `effects/Tremolo` ("vloom.tremolo"): tremolo or auto-pan with a sine,
  triangle or rounded square LFO, depth, stereo phase; free or tempo-synced.
- `effects/DeEsser` ("vloom.deesser"): high-pass detection at a frequency
  against a threshold, up to a range of reduction, wideband or split-band
  (a dynamic high shelf), listen; `gainReductionDb()` for a meter.
- `effects/TransientShaper` ("vloom.transient"): attack and sustain boost or
  cut from envelope differences in dB, so independent of the level; speed,
  output, mix.
- `effects/Bitcrusher` ("vloom.bitcrusher"): drive, bit depth (1 .. 24,
  fractional), downsampling (1 .. 64x, fractional) with jitter, dither, an
  optional anti-alias filter, output, mix.
- None of these effects has latency. Those with a Mix pass their input
  through bit for bit at Mix 0, the tremolo at depth 0, the de-esser below
  its threshold. Chorus, flanger, phaser and tremolo take the host tempo
  through `setTempo()` (120 BPM until then); their LFOs run free.
- `Sample.hpp` (ADR 0002): `SampleBuffer`, decoded audio the host hands over
  (one or two channels, any rate, immutable); `SampleSlot`, which swaps
  samples between the host thread and the audio thread without locks, and
  frees replaced ones only on the host thread; the optional `SampleSlots`
  capability (`setSample()`, `sample()`, `sampleInfo()`, `collectGarbage()`);
  `hermite()` interpolation.
- `instruments/Sampler` ("vloom.sampler"): one sample across the keyboard.
  Root key, transpose, fine tune, key range, velocity sensitivity, start;
  forward or ping-pong loops with a crossfade; amp envelope; low-pass,
  band-pass or high-pass filter with its own envelope and key tracking;
  32 voices, or mono with legato and glide; gain and pan. Hermite resampling
  keeps the pitch whatever the rates of the sample and the host.
- `instruments/DrumSampler` ("vloom.drums"): 16 pads on keys 36 upward (each
  can be moved), each with a sample, tune and fine tune, decay, release,
  gain, pan, start, reverse, choke group (1 .. 8), one-shot or gate mode and
  velocity sensitivity; master gain; 32 voices.
- `Envelope.hpp` and `StateVariableFilter.hpp`: the attack, hold, decay,
  sustain, release envelope and the state-variable filter, moved out of
  `Synth` to be shared (Synth sounds exactly as before).
- `Instrument` concept (`Processor.hpp`): an instrument a host wraps, with
  `noteOn()` and `noteOff()`; its `process()` adds its sound.
- `Wavetable`: four built-in tables (Basic Shapes, Filter Sweep, Formant,
  Pulse Width) of 32 frames, each kept at 11 band limits so a cycle played at
  any pitch does not alias.
- `instruments/Synth` ("vloom.synth"): a wavetable synthesizer. Two wavetable
  oscillators with unison, a sub oscillator and noise; a low-pass, band-pass
  or high-pass filter with drive; three envelopes and four LFOs; 16 voices, or
  mono with legato and glide.
- Project setup: CMake presets (dev, release, asan, tsan), CI on Linux, macOS
  and Windows, clang-format and commit message checks.
- `SmoothedValue`: a parameter that glides to its target in a straight line,
  so changes do not click.
- `version()`.
- `Processor.hpp`: `ParameterSpec`, `ProcessorSpec` and the `Effect` concept,
  the shape a host (vloom-engine, a plugin wrapper) wraps.
- `effects/Gain` ("vloom.gain"): -60 .. +12 dB, silent at -60, changes glide
  over 5 ms. Moved here from vloom-engine's built-in gain.
- `Biquad.hpp`: a second-order filter section (`BiquadCoefficients`,
  `BiquadState`) and the designs `biquad::peak`, `lowShelf`, `highShelf`,
  `highPass` and `lowPass`, with the response in dB for drawing curves.
- `effects/Eq` ("vloom.eq"): a parametric equalizer. Low cut and high cut
  (12 dB per octave, each with a switch), low and high shelf, and two bells
  with Q; gains of -18 .. +18 dB. Sections at 0 dB are skipped, changes glide
  over 20 ms, and `magnitudeDb()` gives the response curve. No latency.
- `ParameterSpec::group`: what a parameter belongs to ("Low Shelf"), for a
  host that lays out its own controls.
