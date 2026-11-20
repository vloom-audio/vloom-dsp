// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/Oversampler.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <numbers>
#include <vector>

using Catch::Approx;
using vloom::dsp::Oversampler;

namespace {

/// Up and straight back down: what passes through the oversampler alone.
std::vector<float> roundTrip(Oversampler& oversampler, const std::vector<float>& in)
{
    std::vector<float> out(in.size());
    std::array<float, Oversampler::kMaxFactor> fast{};
    for (std::size_t i = 0; i < in.size(); ++i) {
        oversampler.upsample(0, in[i], fast.data());
        out[i] = oversampler.downsample(0, fast.data());
    }
    return out;
}

std::vector<float> sine(double frequency, int frames)
{
    std::vector<float> out(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i)
        out[static_cast<std::size_t>(i)]
            = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * frequency / 48000.0 * i));
    return out;
}

} // namespace

TEST_CASE("The oversampler delays by its latency and keeps the passband", "[oversampler]")
{
    for (const int factor : {2, 4}) {
        Oversampler oversampler;
        oversampler.setFactor(factor);
        CHECK(oversampler.factor() == factor);
        const int latency = oversampler.latencyFrames();
        CHECK(latency == (factor == 2 ? Oversampler::kLatency2x : Oversampler::kLatency4x));

        // A step settles to 1 (unity gain at DC) after the latency.
        std::vector<float> step(512, 1.0f);
        const std::vector<float> settled = roundTrip(oversampler, step);
        CHECK(settled.back() == Approx(1.0f).margin(1e-4));

        // Sines up to 20 kHz at 48 kHz come back delayed by exactly the latency.
        for (const double frequency : {100.0, 5000.0, 15000.0, 20000.0}) {
            oversampler.reset();
            const std::vector<float> in = sine(frequency, 4096);
            const std::vector<float> out = roundTrip(oversampler, in);
            double error = 0.0;
            for (std::size_t i = 1024; i < out.size(); ++i)
                error = std::max(
                    error, std::abs(static_cast<double>(out[i] - in[i - static_cast<std::size_t>(latency)])));
            INFO("factor " << factor << ", " << frequency << " Hz");
            CHECK(error < (frequency > 19000.0 ? 0.03 : 0.005));
        }
    }
}

TEST_CASE("The oversampler's images are filtered away", "[oversampler]")
{
    // Upsampled, a 10 kHz sine must not leave an image at 38 kHz (2x rate:
    // 96 kHz). Check it by its energy: what is left after the sine.
    Oversampler oversampler;
    oversampler.setFactor(2);
    const std::vector<float> in = sine(10000.0, 8192);
    std::vector<double> fast;
    std::array<float, Oversampler::kMaxFactor> pair{};
    for (const float sample : in) {
        oversampler.upsample(0, sample, pair.data());
        fast.push_back(static_cast<double>(pair[0]));
        fast.push_back(static_cast<double>(pair[1]));
    }
    // Project onto the image frequency.
    double re = 0.0;
    double im = 0.0;
    const double w = 2.0 * std::numbers::pi * 38000.0 / 96000.0;
    for (std::size_t i = 4096; i < fast.size(); ++i) {
        re += fast[i] * std::cos(w * static_cast<double>(i));
        im += fast[i] * std::sin(w * static_cast<double>(i));
    }
    const double image = 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(fast.size() - 4096);
    CHECK(20.0 * std::log10(image / 0.5) < -70.0);
}
