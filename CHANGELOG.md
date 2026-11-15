# Changelog

All notable changes to vloom-dsp are listed here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
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
