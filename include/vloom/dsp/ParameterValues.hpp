// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include "vloom/dsp/Processor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace vloom::dsp {

/// The default of each parameter in `specs`, in the same order: for a
/// processor whose parameter ids are their indices.
template <std::size_t N>
[[nodiscard]] constexpr std::array<double, N> defaultValues(
    const std::array<ParameterSpec, N>& specs) noexcept
{
    std::array<double, N> values{};
    for (std::size_t i = 0; i < N; ++i)
        values[i] = specs[i].defaultValue;
    return values;
}

/// `value` kept within the range of `spec`, and whole if it is stepped.
/// `value` must be finite. Real-time safe.
[[nodiscard]] inline double clampToSpec(const ParameterSpec& spec, double value) noexcept
{
    value = std::clamp(value, spec.minValue, spec.maxValue);
    return spec.stepped ? std::round(value) : value;
}

/// A level in dB as a factor.
[[nodiscard]] inline double decibelsToGain(double db) noexcept { return std::pow(10.0, db / 20.0); }

/// The factor for a one-pole smoother that covers about 63 % of a step in
/// `seconds`; 0 (no smoothing) for a zero or bad time or rate.
[[nodiscard]] inline double onePoleFactor(double seconds, double sampleRate) noexcept
{
    const double frames = seconds * sampleRate;
    return std::isfinite(frames) && frames > 0.0 ? std::exp(-1.0 / frames) : 0.0;
}

} // namespace vloom::dsp
