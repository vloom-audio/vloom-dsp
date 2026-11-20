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

> **Status: just started.** The `Effect` and `Instrument` concepts that hosts
> wrap (`Processor.hpp`), the optional `SampleSlots` capability for processors
> that play samples (`Sample.hpp`, ADR 0002), and building blocks
> (`SmoothedValue`, `Biquad`, `Envelope`, `StateVariableFilter`, `Wavetable`).
> Compressor, limiter, delay and reverb come next.

## Processors

| Id | Class | What it is |
| --- | --- | --- |
| `vloom.gain` | `effects/Gain` | Gain, -60 .. +12 dB |
| `vloom.eq` | `effects/Eq` | Parametric equalizer: cuts, shelves, two bells |
| `vloom.synth` | `instruments/Synth` | Wavetable synthesizer |
| `vloom.sampler` | `instruments/Sampler` | One sample across the keyboard: loops, filter, mono and glide (1 sample slot) |
| `vloom.drums` | `instruments/DrumSampler` | 16 pads on MIDI keys, with tune, reverse and choke groups (16 sample slots) |

The sampler and drums play audio the host decodes and hands over with
`setSample()`. They do no file I/O; see
[ADR 0002](docs/adr/0002-sample-data.md).

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
