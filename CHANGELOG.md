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
