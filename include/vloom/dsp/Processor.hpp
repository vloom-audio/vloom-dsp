// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include <concepts>
#include <cstdint>
#include <span>

namespace vloom::dsp {

/// One parameter of a processor. Values are in the parameter's own units,
/// between minValue and maxValue.
struct ParameterSpec {
    std::uint32_t id = 0; ///< stable across versions and saved projects
    const char* name = "";
    const char* unit = ""; ///< e.g. "dB", "Hz", "%"; empty: none
    double minValue = 0.0;
    double maxValue = 1.0;
    double defaultValue = 0.0;
    bool stepped = false; ///< whole numbers only (a switch, a choice)
    /// What it belongs to, for a host that lays out controls, e.g. "Low
    /// Shelf"; parameters of one group follow each other. Empty: none.
    const char* group = "";
};

/// What describes a processor to a host.
struct ProcessorSpec {
    const char* id = ""; ///< unique and stable, e.g. "vloom.gain"
    const char* name = "";
    const char* version = "";
};

/// An effect that a host (vloom-engine, a plugin wrapper) can wrap.
///
/// - Non-real-time: `prepare()` sizes all memory for a sample rate and a
///   largest block; nothing else allocates.
/// - Real-time (noexcept, no allocation, no locks): `setParameter()` takes a
///   value in the parameter's units, clamps it and ignores NaN; `process()`
///   works in place on `numChannels` channels; `reset()` clears tails.
/// - Starts at every parameter's default; a host sets the values it saved.
template <typename T>
concept Effect = std::default_initializable<T>
    && requires(T effect, const T constEffect, double sampleRate, int frames, std::uint32_t id, double value,
        float* const* channels) {
           { T::spec() } -> std::same_as<ProcessorSpec>;
           { T::parameters() } -> std::convertible_to<std::span<const ParameterSpec>>;
           effect.prepare(sampleRate, frames);
           { effect.setParameter(id, value) } noexcept;
           { effect.process(channels, frames, frames) } noexcept;
           { effect.reset() } noexcept;
           { constEffect.latencyFrames() } noexcept -> std::same_as<int>;
       };

/// An instrument that a host can wrap: it is given notes and adds its sound.
///
/// Like an Effect (prepare() sizes all memory; the rest is real-time), with
/// notes in place of an input:
/// - `noteOn(key, velocity)` starts MIDI key `key` (0 .. 127) at `velocity`
///   (0 .. 1); `noteOff(key)` lets it ring out. Both take effect at the next
///   `process()`: a host splits its block at each note for sample accuracy.
/// - `process()` ADDS the instrument's sound to `numChannels` channels.
/// - `reset()` silences every note at once.
template <typename T>
concept Instrument = std::default_initializable<T>
    && requires(T instrument, const T constInstrument, double sampleRate, int frames, std::uint32_t id,
        double value, float* const* channels, int key, float velocity) {
           { T::spec() } -> std::same_as<ProcessorSpec>;
           { T::parameters() } -> std::convertible_to<std::span<const ParameterSpec>>;
           instrument.prepare(sampleRate, frames);
           { instrument.setParameter(id, value) } noexcept;
           { instrument.noteOn(key, velocity) } noexcept;
           { instrument.noteOff(key) } noexcept;
           { instrument.process(channels, frames, frames) } noexcept;
           { instrument.reset() } noexcept;
           { constInstrument.latencyFrames() } noexcept -> std::same_as<int>;
       };

/// An Effect that can also take a key signal (sidechain).
template <typename T>
concept SidechainEffect
    = Effect<T> && requires(T e, float* const* ch, const float* const* key, int n, int k, int f) {
          { e.processWithSidechain(ch, key, n, k, f) } noexcept;
      };
/// A processor that follows the host's tempo (beats per minute, > 0).
template <typename T>
concept TempoAware = requires(T p, double bpm) {
    { p.setTempo(bpm) } noexcept;
};

/// The parameter with `id` in `list`, or nullptr.
[[nodiscard]] constexpr const ParameterSpec* findParameter(
    std::span<const ParameterSpec> list, std::uint32_t id) noexcept
{
    for (const ParameterSpec& parameter : list)
        if (parameter.id == id)
            return &parameter;
    return nullptr;
}

} // namespace vloom::dsp
