# Changelog

All notable changes to vloom-dsp are listed here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
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
