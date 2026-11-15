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
