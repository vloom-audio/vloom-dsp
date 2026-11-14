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
> (`Processor.hpp`), a first effect (`Gain`) and building blocks
> (`SmoothedValue`). EQ, compressor, limiter, delay and reverb come next.

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
