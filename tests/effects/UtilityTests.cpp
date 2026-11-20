// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "support/EffectTesting.hpp"
#include "vloom/dsp/effects/Utility.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string_view>

using Catch::Approx;
using vloom::dsp::Utility;
using namespace vloom::test;

namespace {

constexpr double kRate = 48000.0;
constexpr std::size_t kSettled = 960; // past the 10 ms glides

/// Renders a stereo pair (two different noises) through a fresh utility
/// with one setting; returns input and output.
std::pair<Audio, Audio> run(std::uint32_t id, double value, int frames = 4800)
{
    Utility utility;
    utility.prepare(kRate, 512);
    utility.setParameter(id, value);
    Audio in{noise(frames, 1, 0.5f), noise(frames, 2, 0.5f)};
    Audio out = in;
    render(utility, out);
    return {in, out};
}

} // namespace

TEST_CASE("Utility describes itself and its parameters", "[utility]")
{
    CHECK(std::string_view(Utility::spec().id) == "vloom.utility");
    checkParameterList<Utility>();
    CHECK(Utility().latencyFrames() == 0);
}

TEST_CASE("Utility at its defaults passes the input through bit for bit", "[utility]")
{
    Utility utility;
    utility.prepare(kRate, 512);
    Audio in{noise(4096, 1), noise(4096, 2)};
    Audio out = in;
    render(utility, out);
    CHECK(out == in);
    Signal mono = renderMono(utility, in[0]);
    CHECK(mono == in[0]);
}

TEST_CASE("Utility: width 0 % and Mono make both sides the mid signal", "[utility]")
{
    for (const auto& [id, value] :
        std::array<std::pair<std::uint32_t, double>, 2>{{{Utility::kWidth, 0.0}, {Utility::kMono, 1.0}}}) {
        const auto [in, out] = run(id, value);
        for (std::size_t i = kSettled; i < out[0].size(); ++i) {
            const float mid = 0.5f * (in[0][i] + in[1][i]);
            REQUIRE(out[0][i] == Approx(mid).margin(1e-6));
            REQUIRE(out[1][i] == out[0][i]);
        }
    }
    // 200 %: the side signal doubled.
    const auto [in, out] = run(Utility::kWidth, 200.0);
    for (std::size_t i = kSettled; i < out[0].size(); ++i) {
        const float mid = 0.5f * (in[0][i] + in[1][i]);
        const float side = 0.5f * (in[0][i] - in[1][i]);
        REQUIRE(out[0][i] == Approx(mid + 2.0f * side).margin(1e-6));
        REQUIRE(out[1][i] == Approx(mid - 2.0f * side).margin(1e-6));
    }
}

TEST_CASE("Utility inverts, swaps, and glides there without a click", "[utility]")
{
    {
        const auto [in, out] = run(Utility::kInvertLeft, 1.0);
        CHECK(out[0][0] == Approx(in[0][0]).margin(0.01)); // a glide, not a jump
        for (std::size_t i = kSettled; i < out[0].size(); ++i) {
            REQUIRE(out[0][i] == -in[0][i]);
            REQUIRE(out[1][i] == in[1][i]);
        }
    }
    {
        const auto [in, out] = run(Utility::kInvertRight, 1.0);
        for (std::size_t i = kSettled; i < out[0].size(); ++i) {
            REQUIRE(out[0][i] == in[0][i]);
            REQUIRE(out[1][i] == -in[1][i]);
        }
    }
    {
        const auto [in, out] = run(Utility::kSwap, 1.0);
        for (std::size_t i = kSettled; i < out[0].size(); ++i) {
            REQUIRE(out[0][i] == in[1][i]);
            REQUIRE(out[1][i] == in[0][i]);
        }
    }
}

TEST_CASE("Utility's gain and pan", "[utility]")
{
    {
        const auto [in, out] = run(Utility::kGain, -6.0);
        for (std::size_t i = kSettled; i < out[0].size(); ++i)
            REQUIRE(out[0][i] == Approx(in[0][i] * 0.501187f).margin(1e-6));
    }
    {
        const auto [in, out] = run(Utility::kGain, -60.0);
        CHECK(peak(out[0], kSettled) == 0.0f); // silent at the minimum
    }
    {
        const auto [in, out] = run(Utility::kPan, 100.0);
        CHECK(peak(out[0], kSettled) == 0.0f);
        for (std::size_t i = kSettled; i < out[0].size(); ++i)
            REQUIRE(out[1][i] == in[1][i]);
    }
    {
        const auto [in, out] = run(Utility::kPan, -50.0);
        for (std::size_t i = kSettled; i < out[0].size(); ++i) {
            REQUIRE(out[0][i] == in[0][i]);
            REQUIRE(out[1][i] == Approx(in[1][i] * 0.7071068f).margin(1e-6));
        }
    }
    // Mono: the gain works, pan does not.
    Utility utility;
    utility.prepare(kRate, 512);
    utility.setParameter(Utility::kGain, -6.0);
    utility.setParameter(Utility::kPan, 100.0);
    const Signal in = noise(4800);
    const Signal out = renderMono(utility, in);
    CHECK(out.back() == Approx(in.back() * 0.501187f).margin(1e-6));
}

TEST_CASE("Utility's DC blocker removes an offset", "[utility]")
{
    Utility utility;
    utility.prepare(kRate, 512);
    utility.setParameter(Utility::kDcBlock, 1.0);
    Signal in = sine(1000.0, 96000, kRate, 0.25f);
    for (float& sample : in)
        sample += 0.3f;
    const Signal out = renderMono(utility, in);
    double mean = 0.0;
    for (std::size_t i = 48000; i < out.size(); ++i)
        mean += static_cast<double>(out[i]);
    mean /= 48000.0;
    CHECK(std::abs(mean) < 1.0e-3);
    CHECK(rmsDb(out, 48000) == Approx(rmsDb(sine(1000.0, 48000, kRate, 0.25f))).margin(0.05));
}

TEST_CASE("Utility: silence, reset, no allocation, stable everywhere", "[utility]")
{
    Utility utility;
    utility.prepare(kRate, 512);
    utility.setParameter(Utility::kWidth, 150.0);
    utility.setParameter(Utility::kDcBlock, 1.0);
    checkSilentWhenSilent(utility);
    checkNoAllocation(utility);
    checkNoAllocation(utility, 1);

    // reset() ends a glide: the next frame is at the new setting.
    utility.setParameter(Utility::kInvertLeft, 1.0);
    utility.setParameter(Utility::kDcBlock, 0.0);
    utility.setParameter(Utility::kWidth, 100.0);
    utility.reset();
    Audio audio{constant(1, 0.5f), constant(1, 0.5f)};
    render(utility, audio);
    CHECK(audio[0][0] == -0.5f);

    checkStableEverywhere<Utility>();
}
