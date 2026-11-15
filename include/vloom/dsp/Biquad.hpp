// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace vloom::dsp {

/// A second-order filter section, normalized (a0 = 1):
/// y[n] = b0 x[n] + b1 x[n-1] + b2 x[n-2] - a1 y[n-1] - a2 y[n-2].
/// The default passes its input through unchanged.
struct BiquadCoefficients {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;

    /// The gain at `frequency` (Hz), in dB. Not for the audio thread.
    [[nodiscard]] double magnitudeDb(double frequency, double sampleRate) const
    {
        if (!(sampleRate > 0.0) || !std::isfinite(frequency))
            return 0.0;
        const double w = 2.0 * std::numbers::pi * frequency / sampleRate;
        const std::complex<double> z1 = std::polar(1.0, -w);
        const std::complex<double> z2 = z1 * z1;
        const double gain = std::abs((b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2));
        return 20.0 * std::log10(std::max(gain, 1.0e-12));
    }
};

/// The memory of one channel of a biquad (transposed direct form II, which
/// stays well-behaved while the coefficients move).
struct BiquadState {
    double s1 = 0.0;
    double s2 = 0.0;

    [[nodiscard]] double process(const BiquadCoefficients& c, double x) noexcept
    {
        const double y = c.b0 * x + s1;
        s1 = c.b1 * x - c.a1 * y + s2;
        s2 = c.b2 * x - c.a2 * y;
        return y;
    }

    /// Zeroes values too small to hear, so a tail never decays into
    /// denormals (slow on some CPUs). Call once per block.
    void flushDenormals() noexcept
    {
        constexpr double kTiny = 1.0e-20;
        if (std::abs(s1) < kTiny)
            s1 = 0.0;
        if (std::abs(s2) < kTiny)
            s2 = 0.0;
    }

    void clear() noexcept { *this = {}; }
};

/// Filter designs (Robert Bristow-Johnson's Audio EQ Cookbook). Real-time
/// safe. The frequency is kept between 1 Hz and just under half the sample
/// rate, Q between 0.01 and 100; a bad sample rate or value gives a filter
/// that passes its input through.
namespace biquad {

namespace detail {

struct Design {
    double cosw = 0.0;
    double alpha = 0.0;
    bool valid = false;
};

[[nodiscard]] inline Design design(double frequency, double sampleRate, double q) noexcept
{
    if (!(sampleRate > 0.0) || !std::isfinite(sampleRate) || !std::isfinite(frequency) || !std::isfinite(q))
        return {};
    const double f = std::clamp(frequency, 1.0, 0.49 * sampleRate);
    const double w = 2.0 * std::numbers::pi * f / sampleRate;
    return {std::cos(w), std::sin(w) / (2.0 * std::clamp(q, 0.01, 100.0)), true};
}

[[nodiscard]] inline BiquadCoefficients normalized(
    double b0, double b1, double b2, double a0, double a1, double a2) noexcept
{
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

/// The amplitude for a shelf or bell of `gainDb` (the square root of its gain).
[[nodiscard]] inline double amplitude(double gainDb) noexcept
{
    return std::isfinite(gainDb) ? std::pow(10.0, std::clamp(gainDb, -60.0, 60.0) / 40.0) : 1.0;
}

} // namespace detail

inline constexpr double kButterworthQ = 0.70710678118654752440;

/// A bell: `gainDb` at `frequency`, narrower as `q` grows.
[[nodiscard]] inline BiquadCoefficients peak(
    double frequency, double sampleRate, double gainDb, double q) noexcept
{
    const detail::Design d = detail::design(frequency, sampleRate, q);
    if (!d.valid)
        return {};
    const double a = detail::amplitude(gainDb);
    return detail::normalized(1.0 + d.alpha * a, -2.0 * d.cosw, 1.0 - d.alpha * a, 1.0 + d.alpha / a,
        -2.0 * d.cosw, 1.0 - d.alpha / a);
}

/// `gainDb` below `frequency`, unchanged above.
[[nodiscard]] inline BiquadCoefficients lowShelf(
    double frequency, double sampleRate, double gainDb, double q = kButterworthQ) noexcept
{
    const detail::Design d = detail::design(frequency, sampleRate, q);
    if (!d.valid)
        return {};
    const double a = detail::amplitude(gainDb);
    const double k = 2.0 * std::sqrt(a) * d.alpha;
    return detail::normalized(a * ((a + 1.0) - (a - 1.0) * d.cosw + k),
        2.0 * a * ((a - 1.0) - (a + 1.0) * d.cosw), a * ((a + 1.0) - (a - 1.0) * d.cosw - k),
        (a + 1.0) + (a - 1.0) * d.cosw + k, -2.0 * ((a - 1.0) + (a + 1.0) * d.cosw),
        (a + 1.0) + (a - 1.0) * d.cosw - k);
}

/// `gainDb` above `frequency`, unchanged below.
[[nodiscard]] inline BiquadCoefficients highShelf(
    double frequency, double sampleRate, double gainDb, double q = kButterworthQ) noexcept
{
    const detail::Design d = detail::design(frequency, sampleRate, q);
    if (!d.valid)
        return {};
    const double a = detail::amplitude(gainDb);
    const double k = 2.0 * std::sqrt(a) * d.alpha;
    return detail::normalized(a * ((a + 1.0) + (a - 1.0) * d.cosw + k),
        -2.0 * a * ((a - 1.0) + (a + 1.0) * d.cosw), a * ((a + 1.0) + (a - 1.0) * d.cosw - k),
        (a + 1.0) - (a - 1.0) * d.cosw + k, 2.0 * ((a - 1.0) - (a + 1.0) * d.cosw),
        (a + 1.0) - (a - 1.0) * d.cosw - k);
}

/// Cuts below `frequency` at 12 dB per octave.
[[nodiscard]] inline BiquadCoefficients highPass(
    double frequency, double sampleRate, double q = kButterworthQ) noexcept
{
    const detail::Design d = detail::design(frequency, sampleRate, q);
    if (!d.valid)
        return {};
    const double b = (1.0 + d.cosw) / 2.0;
    return detail::normalized(b, -(1.0 + d.cosw), b, 1.0 + d.alpha, -2.0 * d.cosw, 1.0 - d.alpha);
}

/// Cuts above `frequency` at 12 dB per octave.
[[nodiscard]] inline BiquadCoefficients lowPass(
    double frequency, double sampleRate, double q = kButterworthQ) noexcept
{
    const detail::Design d = detail::design(frequency, sampleRate, q);
    if (!d.valid)
        return {};
    const double b = (1.0 - d.cosw) / 2.0;
    return detail::normalized(b, 1.0 - d.cosw, b, 1.0 + d.alpha, -2.0 * d.cosw, 1.0 - d.alpha);
}

} // namespace biquad

} // namespace vloom::dsp
