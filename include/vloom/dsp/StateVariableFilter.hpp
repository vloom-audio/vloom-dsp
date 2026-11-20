// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vloom::dsp {

/// The coefficients of a state-variable filter (12 dB per octave; Andrew
/// Simper's trapezoidal form), which stays stable while its cutoff moves.
/// Real-time safe.
struct SvfCoefficients {
    float g = 0.0f; ///< tan(pi * cutoff / rate)
    float k = 2.0f; ///< 1 / Q
    float a1 = 1.0f;
    float a2 = 0.0f;
    float a3 = 0.0f;

    /// From `g` and `k` as above.
    [[nodiscard]] static SvfCoefficients make(float g, float k) noexcept
    {
        SvfCoefficients c;
        c.g = g;
        c.k = k;
        c.a1 = 1.0f / (1.0f + g * (g + k));
        c.a2 = g * c.a1;
        c.a3 = g * c.a2;
        return c;
    }

    /// `cutoff` in Hz (kept between 20 Hz and 0.45 of the rate); `resonance`
    /// 0 .. 1 gives Q 0.5 .. 17.
    [[nodiscard]] static SvfCoefficients design(float cutoff, float sampleRate, float resonance) noexcept
    {
        const float f = std::clamp(cutoff, 20.0f, 0.45f * sampleRate);
        return make(std::tan(std::numbers::pi_v<float> * f / sampleRate),
            2.0f * std::pow(0.03f, std::clamp(resonance, 0.0f, 1.0f)));
    }
};

/// The memory of one channel of a state-variable filter.
struct SvfState {
    float band = 0.0f;
    float low = 0.0f;

    struct Outputs {
        float low = 0.0f;
        float band = 0.0f;
        float high = 0.0f;
    };

    [[nodiscard]] Outputs process(const SvfCoefficients& c, float in) noexcept
    {
        const float v3 = in - low;
        const float v1 = c.a1 * band + c.a2 * v3;
        const float v2 = low + c.a2 * band + c.a3 * v3;
        band = 2.0f * v1 - band;
        low = 2.0f * v2 - low;
        return {v2, v1, in - c.k * v1 - v2};
    }

    /// Zeroes values too small to hear (no denormals left ringing). Call
    /// once per block.
    void flushDenormals() noexcept
    {
        if (std::abs(band) < 1.0e-20f)
            band = 0.0f;
        if (std::abs(low) < 1.0e-20f)
            low = 0.0f;
    }

    void clear() noexcept { *this = {}; }
};

} // namespace vloom::dsp
