// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace vloom::dsp {

/// The settings of a state variable filter (Andrew Simper's trapezoidal,
/// "topology-preserving" design). Unlike a biquad, it stays stable and free
/// of zipper noise while its frequency and Q move every sample. The default
/// does nothing useful: design one with svf::design().
struct SvfCoefficients {
    double g = 0.0; ///< tan(pi f / rate)
    double k = 2.0; ///< 1 / Q
    double a1 = 1.0;
    double a2 = 0.0;
    double a3 = 0.0;
};

/// The three outputs of a state variable filter for one sample.
struct SvfOutputs {
    double low = 0.0;
    double band = 0.0; ///< peaks at Q at the frequency; times k: 0 dB there
    double high = 0.0;
};

namespace svf {

/// A filter at `frequency` (Hz, kept between 1 Hz and just under half the
/// sample rate) with `q` (kept between 0.025 and 100). A bad sample rate or
/// value gives the coefficients of a 1 kHz filter at 48 kHz, which are
/// stable. Real-time safe.
[[nodiscard]] inline SvfCoefficients design(double frequency, double sampleRate, double q) noexcept
{
    if (!(sampleRate > 0.0) || !std::isfinite(sampleRate) || !std::isfinite(frequency) || !std::isfinite(q)) {
        frequency = 1000.0;
        sampleRate = 48000.0;
        q = 0.7071067811865476;
    }
    const double f = std::clamp(frequency, 1.0, 0.49 * sampleRate);
    SvfCoefficients c;
    c.g = std::tan(std::numbers::pi * f / sampleRate);
    c.k = 1.0 / std::clamp(q, 0.025, 100.0);
    c.a1 = 1.0 / (1.0 + c.g * (c.g + c.k));
    c.a2 = c.g * c.a1;
    c.a3 = c.g * c.a2;
    return c;
}

/// The responses of the three outputs at `frequency` for `c` designed at
/// `sampleRate`. Not for the audio thread.
struct Response {
    std::complex<double> low;
    std::complex<double> band;
    std::complex<double> high;
};

[[nodiscard]] inline Response response(const SvfCoefficients& c, double frequency, double sampleRate)
{
    if (!(sampleRate > 0.0) || !(c.g > 0.0))
        return {1.0, 0.0, 0.0};
    // The trapezoidal filter is the bilinear transform of the analog one, with
    // the frequency pre-warped: s = j tan(pi f / rate) / g.
    const double warped
        = std::tan(std::numbers::pi * std::clamp(frequency, 0.0, 0.4999 * sampleRate) / sampleRate);
    const std::complex<double> s(0.0, warped / c.g);
    const std::complex<double> d = s * s + c.k * s + 1.0;
    return {1.0 / d, s / d, s * s / d};
}

} // namespace svf

/// The memory of one channel of a state variable filter.
struct SvfState {
    double ic1 = 0.0;
    double ic2 = 0.0;

    [[nodiscard]] SvfOutputs process(const SvfCoefficients& c, double x) noexcept
    {
        const double v3 = x - ic2;
        const double v1 = c.a1 * ic1 + c.a2 * v3;
        const double v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return {v2, v1, x - c.k * v1 - v2};
    }

    /// Zeroes values too small to hear, so a tail never decays into
    /// denormals. Call once per block.
    void flushDenormals() noexcept
    {
        constexpr double kTiny = 1.0e-20;
        if (std::abs(ic1) < kTiny)
            ic1 = 0.0;
        if (std::abs(ic2) < kTiny)
            ic2 = 0.0;
    }

    void clear() noexcept { *this = {}; }
};

} // namespace vloom::dsp
