// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/Oversampler.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vloom::dsp {

namespace {

/// The modified Bessel function of the first kind, order 0 (for the Kaiser
/// window; std::cyl_bessel_i is missing from some standard libraries).
[[nodiscard]] double besselI0(double x) noexcept
{
    double sum = 1.0;
    double term = 1.0;
    const double quarter = x * x / 4.0;
    for (int k = 1; k < 64; ++k) {
        term *= quarter / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
        if (term < sum * 1.0e-17)
            break;
    }
    return sum;
}

constexpr double kBeta = 8.0; // about 80 dB of stopband

} // namespace

template <int K> void Oversampler::Stage<K>::design(double beta) noexcept
{
    constexpr int taps = 4 * K + 3;
    constexpr int centre = 2 * K + 1;
    double sum = 0.0;
    for (int i = 0; i < kEven; ++i) {
        const int j = 2 * i;
        const double m = static_cast<double>(j - centre); // odd, never 0
        const double x = std::numbers::pi * m / 2.0;
        const double sinc = std::sin(x) / x;
        const double r = 2.0 * j / static_cast<double>(taps - 1) - 1.0;
        const double window = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / besselI0(beta);
        even[static_cast<std::size_t>(i)] = 0.5 * sinc * window;
        sum += even[static_cast<std::size_t>(i)];
    }
    // The even taps add up to 1/2 and the centre is 1/2: a gain of 1 at DC.
    for (double& tap : even)
        tap *= 0.5 / sum;
}

template <int K> void Oversampler::Stage<K>::up(int channel, double x, double* out) noexcept
{
    History& history = channels[static_cast<std::size_t>(channel)].up;
    history.push(x);
    double sum = 0.0;
    for (int i = 0; i < kEven; ++i)
        sum += even[static_cast<std::size_t>(i)] * history[i];
    out[0] = 2.0 * sum;
    out[1] = history[K]; // the odd phase is the centre tap alone: a delay
}

template <int K> double Oversampler::Stage<K>::down(int channel, const double* in) noexcept
{
    Channel& state = channels[static_cast<std::size_t>(channel)];
    state.downEven.push(in[0]);
    double sum = 0.0;
    for (int i = 0; i < kEven; ++i)
        sum += even[static_cast<std::size_t>(i)] * state.downEven[i];
    sum += 0.5 * state.downOdd[K];
    state.downOdd.push(in[1]);
    return sum;
}

Oversampler::Oversampler() noexcept
{
    first_.design(kBeta);
    second_.design(kBeta);
}

void Oversampler::setFactor(int factor) noexcept
{
    factor_ = factor == 2 ? 2 : 4;
    reset();
}

void Oversampler::reset() noexcept
{
    first_.clear();
    second_.clear();
    pad_ = {};
}

void Oversampler::upsample(int channel, float sample, float* out) noexcept
{
    if (channel < 0 || channel >= kMaxChannels) {
        for (int i = 0; i < factor_; ++i)
            out[i] = sample;
        return;
    }
    double twice[2];
    first_.up(channel, static_cast<double>(sample), twice);
    if (factor_ == 2) {
        out[0] = static_cast<float>(twice[0]);
        out[1] = static_cast<float>(twice[1]);
        return;
    }
    double four[4];
    second_.up(channel, twice[0], four);
    second_.up(channel, twice[1], four + 2);
    for (int i = 0; i < 4; ++i)
        out[i] = static_cast<float>(four[i]);
}

float Oversampler::downsample(int channel, const float* in) noexcept
{
    if (channel < 0 || channel >= kMaxChannels)
        return in[0];
    double twice[2];
    if (factor_ == 2) {
        twice[0] = static_cast<double>(in[0]);
        twice[1] = static_cast<double>(in[1]);
    } else {
        const double four[4] = {static_cast<double>(in[0]), static_cast<double>(in[1]),
            static_cast<double>(in[2]), static_cast<double>(in[3])};
        const double a = second_.down(channel, four);
        const double b = second_.down(channel, four + 2);
        double& pad = pad_[static_cast<std::size_t>(channel)];
        twice[0] = pad;
        twice[1] = a;
        pad = b;
    }
    const double y = first_.down(channel, twice);
    return std::abs(y) < 1.0e-30 ? 0.0f : static_cast<float>(y);
}

} // namespace vloom::dsp
