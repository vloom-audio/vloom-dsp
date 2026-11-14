// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/effects/Gain.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>
#include <string_view>
#include <vector>

using Catch::Approx;
using vloom::dsp::Gain;

namespace {

constexpr double kRate = 48000.0;
constexpr int kRamp = 240; // 5 ms at 48 kHz

/// Runs `frames` of ones through both channels; returns the left channel.
std::vector<float> runOnes(Gain& gain, int frames)
{
    std::vector<float> left(static_cast<std::size_t>(frames), 1.0f);
    std::vector<float> right(static_cast<std::size_t>(frames), 1.0f);
    std::array<float*, 2> channels{left.data(), right.data()};
    gain.process(channels.data(), 2, frames);
    CHECK(left == right);
    return left;
}

} // namespace

TEST_CASE("Gain describes itself and its parameter", "[gain]")
{
    CHECK(std::string_view(Gain::spec().id) == "vloom.gain");
    REQUIRE(Gain::parameters().size() == 1);
    const auto* gain = vloom::dsp::findParameter(Gain::parameters(), Gain::kGain);
    REQUIRE(gain != nullptr);
    CHECK(gain->minValue == Gain::kMinDb);
    CHECK(gain->maxValue == Gain::kMaxDb);
    CHECK(gain->defaultValue == 0.0);
    CHECK(vloom::dsp::findParameter(Gain::parameters(), 99) == nullptr);
}

TEST_CASE("Gain starts at 0 dB and glides to a new level", "[gain]")
{
    Gain gain;
    gain.prepare(kRate, 512);
    CHECK(runOnes(gain, 16).back() == 1.0f);

    gain.setParameter(Gain::kGain, -6.0);
    const auto ramp = runOnes(gain, kRamp + 10);
    CHECK(ramp.front() < 1.0f);
    CHECK(ramp.front() > 0.99f); // a glide, not a jump
    for (std::size_t i = 1; i < ramp.size(); ++i)
        CHECK(ramp[i] <= ramp[i - 1]);
    CHECK(ramp[kRamp - 1] == Approx(0.501187f).margin(1e-6));
    CHECK(runOnes(gain, 8).front() == Approx(0.501187f).margin(1e-6)); // and stays
}

TEST_CASE("Gain is silent at its minimum and clamps to its range", "[gain]")
{
    Gain gain;
    gain.prepare(kRate, 512);
    gain.setParameter(Gain::kGain, -1000.0);
    (void)runOnes(gain, kRamp);
    CHECK(runOnes(gain, 4).front() == 0.0f);

    gain.setParameter(Gain::kGain, 1000.0);
    (void)runOnes(gain, kRamp);
    CHECK(runOnes(gain, 4).front() == Approx(Gain::linear(Gain::kMaxDb)));
}

TEST_CASE("Gain ignores NaN and other parameters", "[gain]")
{
    Gain gain;
    gain.prepare(kRate, 512);
    gain.setParameter(Gain::kGain, std::numeric_limits<double>::quiet_NaN());
    gain.setParameter(7, -60.0);
    CHECK(runOnes(gain, 4).front() == 1.0f);
}

TEST_CASE("Gain set before prepare starts there, and reset ends a glide", "[gain]")
{
    Gain gain;
    gain.setParameter(Gain::kGain, -6.0);
    gain.prepare(kRate, 512);
    CHECK(runOnes(gain, 1).front() == Approx(0.501187f).margin(1e-6)); // no ramp from 0 dB

    gain.setParameter(Gain::kGain, 0.0);
    (void)runOnes(gain, 10); // mid-glide
    gain.reset();
    CHECK(runOnes(gain, 1).front() == 1.0f);
}
