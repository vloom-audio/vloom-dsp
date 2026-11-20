// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "ModTestSignals.hpp"
#include "vloom/dsp/effects/Tremolo.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>

using Catch::Approx;
using vloom::dsp::Tremolo;
using namespace modtest;

namespace {

constexpr double kRate = 48000.0;

/// The frames at which `gain` dips to its lowest within each `period`-long
/// stretch (from `from` on).
std::vector<std::size_t> dips(const Signal& gain, std::size_t from, std::size_t period)
{
    std::vector<std::size_t> at;
    for (std::size_t start = from; start + period <= gain.size(); start += period)
        at.push_back(
            static_cast<std::size_t>(std::min_element(gain.begin() + static_cast<std::ptrdiff_t>(start),
                                         gain.begin() + static_cast<std::ptrdiff_t>(start + period))
                - gain.begin()));
    return at;
}

/// The mean spacing of the dips of a triangle tremolo on a constant signal.
double measuredPeriod(Tremolo& tremolo, std::size_t expected)
{
    tremolo.setParameter(Tremolo::kShape, static_cast<double>(Tremolo::Shape::Triangle));
    tremolo.setParameter(Tremolo::kDepth, 100.0);
    tremolo.prepare(kRate, 512);
    Signal gain = constant(static_cast<int>(expected * 6), 1.0f);
    runMono(tremolo, gain);
    const std::vector<std::size_t> at = dips(gain, expected / 2, expected);
    REQUIRE(at.size() >= 4);
    for (std::size_t i = 1; i < at.size(); ++i)
        CHECK(static_cast<double>(at[i] - at[i - 1]) == Approx(static_cast<double>(expected)).margin(1.0));
    return static_cast<double>(at.back() - at.front()) / static_cast<double>(at.size() - 1);
}

} // namespace

TEST_CASE("Tremolo describes itself and its parameters", "[tremolo]")
{
    CHECK(std::string_view(Tremolo::spec().id) == "vloom.tremolo");
    checkParameters<Tremolo>({"Mode", "Rate", "Sync", "Note", "Shape", "Depth", "Stereo Phase"});
    CHECK(Tremolo::parameters()[Tremolo::kMode].stepped);
    CHECK(Tremolo::parameters()[Tremolo::kShape].stepped);
    CHECK(Tremolo::parameters()[Tremolo::kShape].maxValue == Tremolo::kShapes - 1);
    CHECK(Tremolo().latencyFrames() == 0);
}

TEST_CASE("Tremolo swings the gain between 1 and 1 - depth", "[tremolo]")
{
    for (const int shape : {0, 1, 2}) {
        INFO("shape: " << shape);
        Tremolo tremolo;
        tremolo.setParameter(Tremolo::kShape, shape);
        tremolo.setParameter(Tremolo::kDepth, 60.0);
        tremolo.setParameter(Tremolo::kRate, 5.0);
        tremolo.prepare(kRate, 512);
        Signal gain = constant(48000, 1.0f);
        runMono(tremolo, gain);
        const auto [low, high] = std::minmax_element(gain.begin(), gain.end());
        CHECK(*low == Approx(0.4).margin(1.0e-4));
        CHECK(*high == Approx(1.0).margin(1.0e-6));
    }
}

TEST_CASE("Tremolo has the period of its rate", "[tremolo]")
{
    Tremolo tremolo;
    tremolo.setParameter(Tremolo::kRate, 5.0);
    CHECK(measuredPeriod(tremolo, 9600) == Approx(9600.0).margin(0.5));
}

TEST_CASE("Tremolo synced to a quarter note follows the tempo", "[tremolo]")
{
    Tremolo tremolo;
    tremolo.setParameter(Tremolo::kSync, 1.0);
    tremolo.setParameter(Tremolo::kNote, vloom::dsp::note::kQuarter);

    SECTION("120 BPM (the default): half a second")
    {
        CHECK(tremolo.rateHz() == Approx(2.0));
        CHECK(measuredPeriod(tremolo, 24000) == Approx(24000.0).margin(0.5));
    }
    SECTION("90 BPM: two thirds of a second")
    {
        tremolo.setTempo(90.0);
        CHECK(tremolo.rateHz() == Approx(1.5));
        CHECK(measuredPeriod(tremolo, 32000) == Approx(32000.0).margin(0.5));
    }
}

TEST_CASE("Tremolo at depth 0 passes the input through bit for bit", "[tremolo]")
{
    for (const int mode : {0, 1}) {
        Tremolo tremolo;
        tremolo.setParameter(Tremolo::kMode, mode);
        tremolo.setParameter(Tremolo::kDepth, 0.0);
        tremolo.prepare(kRate, 512);
        const std::vector<Signal> in{noise(8000, 1), noise(8000, 2)};
        std::vector<Signal> out = in;
        run(tremolo, out);
        CHECK(out == in);
    }
}

TEST_CASE("Tremolo turns silence into silence", "[tremolo]")
{
    Tremolo tremolo;
    tremolo.setParameter(Tremolo::kDepth, 100.0);
    tremolo.prepare(kRate, 512);
    std::vector<Signal> io{constant(8000, 0.0f), constant(8000, 0.0f)};
    run(tremolo, io);
    CHECK(allZero(io[0]));
    CHECK(allZero(io[1]));
}

TEST_CASE("Tremolo stereo phase mirrors the right channel", "[tremolo]")
{
    Tremolo tremolo;
    tremolo.setParameter(Tremolo::kDepth, 80.0);
    tremolo.setParameter(Tremolo::kStereoPhase, 180.0);
    tremolo.prepare(kRate, 512);
    std::vector<Signal> io{constant(20000, 1.0f), constant(20000, 1.0f)};
    run(tremolo, io);
    for (std::size_t i = 0; i < io[0].size(); i += 97)
        CHECK(io[0][i] + io[1][i] == Approx(2.0 - 0.8).margin(1.0e-5));
}

TEST_CASE("Tremolo auto-pan moves the sound between the sides", "[tremolo]")
{
    Tremolo tremolo;
    tremolo.setParameter(Tremolo::kMode, static_cast<double>(Tremolo::Mode::AutoPan));
    tremolo.setParameter(Tremolo::kDepth, 100.0);
    tremolo.setParameter(Tremolo::kRate, 2.0);
    tremolo.prepare(kRate, 512);
    std::vector<Signal> io{constant(48000, 1.0f), constant(48000, 1.0f)};
    run(tremolo, io);
    for (std::size_t i = 0; i < io[0].size(); i += 31)
        CHECK(std::max(io[0][i], io[1][i]) == Approx(1.0).margin(1.0e-6)); // never boosts, never dips both
    CHECK(*std::min_element(io[0].begin(), io[0].end()) == Approx(0.0).margin(1.0e-4));
    CHECK(*std::min_element(io[1].begin(), io[1].end()) == Approx(0.0).margin(1.0e-4));
}

TEST_CASE("Tremolo reset restarts the LFO", "[tremolo]")
{
    Tremolo tremolo;
    tremolo.setParameter(Tremolo::kDepth, 100.0);
    tremolo.prepare(kRate, 512);
    Signal first = noise(5000);
    const Signal input = first;
    runMono(tremolo, first);
    tremolo.reset();
    Signal second = input;
    runMono(tremolo, second);
    CHECK(first == second);
}
