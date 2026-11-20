# vloom-dsp

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-brightgreen.svg)](#requirements)

Audio effects and instruments for Vloom Audio, as a plain **C++20** library
with no dependency beyond the standard library.

[vloom-engine](https://github.com/vloom-audio/vloom-engine) wraps these
processors as the built-in effects and instruments of
[VloomDAW](https://github.com/vloom-audio/VloomDAW). Because the library knows
nothing about the engine, a plugin format or a UI, the same code can later be
wrapped as CLAP or VST 3 plugins.

## Processors

| Id | Class | What it is | Latency |
|---|---|---|---|
| `vloom.gain` | `effects/Gain` | Level, -60 .. +12 dB | 0 |
| `vloom.eq` | `effects/Eq` | Parametric EQ: cuts, shelves, two bells | 0 |
| `vloom.compressor` | `effects/Compressor` | Compressor: soft knee, peak/RMS, stereo link, parallel mix, sidechain with filter | 0 |
| `vloom.limiter` | `effects/Limiter` | Brickwall look-ahead limiter, optional true peak | look-ahead (+4 with true peak) |
| `vloom.gate` | `effects/Gate` | Noise gate: hysteresis, hold, range, sidechain | 0 |
| `vloom.delay` | `effects/Delay` | Stereo / ping-pong delay, tempo sync, filtered feedback | 0 |
| `vloom.reverb` | `effects/Reverb` | Algorithmic reverb (8-line FDN), pre-delay, damping, freeze | 0 |
| `vloom.saturator` | `effects/Saturator` | Waveshaper (soft, tube, tape, hard), 2x/4x oversampled | 31 / 36 |
| `vloom.utility` | `effects/Utility` | Gain, balance, width, mono, polarity, swap, DC block | 0 |
| `vloom.filter` | `effects/Filter` | Resonant LP/HP/BP/notch, 12/24 dB, drive | 0 |
| `vloom.synth` | `instruments/Synth` | Wavetable synthesizer | 0 |
| `vloom.sampler` | `instruments/Sampler` | One sample across the keyboard: loops, filter, mono and glide (1 sample slot) | 0 |
| `vloom.drums` | `instruments/DrumSampler` | 16 pads on MIDI keys, with tune, reverse and choke groups (16 sample slots) | 0 |

Each one fits the `Effect` or `Instrument` concept in `Processor.hpp`. Two
optional capabilities ([ADR 0003](docs/adr/0003-sidechain-and-tempo-extensions.md)):
`SidechainEffect` (compressor, gate: `processWithSidechain()` with a key
signal) and `TempoAware` (delay: `setTempo()`).

The sampler and drums are `SampleSlots`: they play audio the host decodes
and hands over with `setSample()`, and do no file I/O
([ADR 0002](docs/adr/0002-sample-data.md)).

Building blocks: `SmoothedValue`, `Biquad`, `Svf` (state variable filter),
`DelayLine`, `Oversampler`, `SidechainFilter`, `SharedValue` (lock-free
meter values), `ParameterValues`, `Envelope` (AHDSR), `StateVariableFilter`
(the synth's filter), `Wavetable`.

## Requirements

- CMake 3.22 or newer
- A C++20 compiler: MSVC 2022, Clang 15+ or GCC 12+

## Quick start

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Presets: `dev` (debug, warnings as errors), `release`, and `asan` / `tsan`
(GCC or Clang).

## Using it from another project

```cmake
CPMAddPackage(
    NAME vloom_dsp
    GITHUB_REPOSITORY vloom-audio/vloom-dsp
    GIT_TAG <commit>)
target_link_libraries(my_target PRIVATE vloom::dsp)
```

Tests are built only when vloom-dsp is the top-level project.

## Documentation

- [Coding guidelines](docs/coding-guidelines.md)
- [Decisions (ADRs)](docs/adr/)
- [Changelog](CHANGELOG.md)

## License

MIT, see [LICENSE](LICENSE). vloom-engine itself is GPL-3.0-or-later and may
use this library.
