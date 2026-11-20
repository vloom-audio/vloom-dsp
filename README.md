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

> **Status: just started.** The `Effect` concept that hosts wrap
> (`Processor.hpp`), the effects `Gain` and `Eq` (a parametric equalizer) and
> building blocks (`SmoothedValue`, `Biquad`, `NoteValue`). Compressor,
> limiter, delay and reverb come next.

## Processors

| Id | Class | What it is |
| --- | --- | --- |
| `vloom.gain` | `effects/Gain` | Level, -60 .. +12 dB |
| `vloom.eq` | `effects/Eq` | Parametric equalizer: cuts, shelves, two bells |
| `vloom.chorus` | `effects/Chorus` | Chorus, 1 .. 4 voices, tempo sync |
| `vloom.flanger` | `effects/Flanger` | Flanger with +- feedback, tempo sync |
| `vloom.phaser` | `effects/Phaser` | Phaser, 2 .. 12 stages, tempo sync |
| `vloom.tremolo` | `effects/Tremolo` | Tremolo and auto-pan, tempo sync |
| `vloom.deesser` | `effects/DeEsser` | De-esser, wideband or split-band, with listen |
| `vloom.transient` | `effects/TransientShaper` | Attack and sustain shaper, level-independent |
| `vloom.bitcrusher` | `effects/Bitcrusher` | Bit depth and sample-rate reduction |
| `vloom.synth` | `instruments/Synth` | Wavetable synthesizer |

Tempo-synced effects are `TempoAware`: the host calls `setTempo(bpm)`
(120 until then). Their LFOs run free; they do not follow the transport
position.

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
