# Changelog

All notable changes to vloom-dsp are listed here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
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
