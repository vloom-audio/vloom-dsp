// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "ModTestSignals.hpp"
#include "vloom/dsp/effects/Flanger.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <string_view>

using Catch::Approx;
using vloom::dsp::Flanger;
using namespace modtest;

namespace {

constexpr double kRate = 48000.0;

/// How far (dB) the level of 10 ms windows of a flanged 1 kHz tone swings.
double levelSwingDb(Flanger& flanger)
{
    flanger.prepare(kRate, 512);
    Signal signal = sine(1000.0, 96000, kRate);
    runMono(flanger, signal);
    const std::vector<double> levels = windowLevels(signal, 480, 9600);
    const auto [low, high] = std::minmax_element(levels.begin(), levels.end());
    return *high - *low;
}

} // namespace

TEST_CASE("Flanger describes itself and its parameters", "[flanger]")
{
    CHECK(std::string_view(Flanger::spec().id) == "vloom.flanger");
    checkParameters<Flanger>({"Rate", "Sync", "Note", "Depth", "Delay", "Feedback", "Stereo Phase", "Mix"});
    CHECK(Flanger::parameters()[Flanger::kFeedback].minValue == -95.0);
    CHECK(Flanger::parameters()[Flanger::kFeedback].maxValue == 95.0);
    CHECK(Flanger().latencyFrames() == 0);
}

TEST_CASE("Flanger turns silence into silence", "[flanger]")
{
    Flanger flanger;
    flanger.setParameter(Flanger::kFeedback, 95.0);
    flanger.prepare(kRate, 512);
    std::vector<Signal> io{constant(48000, 0.0f), constant(48000, 0.0f)};
    run(flanger, io);
    CHECK(allZero(io[0]));
    CHECK(allZero(io[1]));
}

TEST_CASE("Flanger at Mix 0 passes the input through bit for bit", "[flanger]")
{
    Flanger flanger;
    flanger.setParameter(Flanger::kMix, 0.0);
    flanger.prepare(kRate, 512);
    const std::vector<Signal> in{noise(20000, 1), noise(20000, 2)};
    std::vector<Signal> out = in;
    run(flanger, out);
    CHECK(out == in);
}

TEST_CASE("Flanger sweeps a comb over a tone", "[flanger]")
{
    Flanger still;
    still.setParameter(Flanger::kDepth, 0.0);
    still.setParameter(Flanger::kFeedback, 0.0);
    CHECK(levelSwingDb(still) < 0.1); // a fixed comb: a fixed level

    Flanger moving;
    moving.setParameter(Flanger::kRate, 1.0);
    moving.setParameter(Flanger::kDepth, 100.0);
    moving.setParameter(Flanger::kFeedback, 0.0);
    CHECK(levelSwingDb(moving) > 12.0); // the notches pass over the tone
}

TEST_CASE("Flanger stays bounded at full feedback at every sample rate", "[flanger]")
{
    for (const double feedback : {95.0, -95.0}) {
        for (const double rate : {44100.0, 96000.0, 192000.0}) {
            Flanger flanger;
            flanger.setParameter(Flanger::kFeedback, feedback);
            flanger.setParameter(Flanger::kDepth, 100.0);
            flanger.setParameter(Flanger::kRate, 10.0);
            flanger.setParameter(Flanger::kDelay, 0.1);
            flanger.setParameter(Flanger::kMix, 100.0);
            flanger.prepare(rate, 512);
            std::vector<Signal> io{
                noise(static_cast<int>(rate) * 2, 5), sine(3000.0, static_cast<int>(rate) * 2, rate, 1.0f)};
            run(flanger, io, 256);
            CHECK(allFinite(io[0]));
            CHECK(allFinite(io[1]));
            CHECK(peak(io[0]) < 21.0f); // 1 / (1 - 0.95), the most a 1.0 input can build up to
            CHECK(peak(io[1]) < 21.0f);
        }
    }
}

TEST_CASE("Flanger follows the tempo when synced", "[flanger]")
{
    Flanger flanger;
    flanger.setParameter(Flanger::kSync, 1.0);
    flanger.setParameter(Flanger::kNote, vloom::dsp::note::kWhole);
    CHECK(flanger.rateHz() == Approx(0.5)); // 120 BPM until told otherwise
    flanger.setTempo(90.0);
    CHECK(flanger.rateHz() == Approx(0.375));
    flanger.setParameter(Flanger::kSync, 0.0);
    CHECK(flanger.rateHz() == Approx(0.2));
}

TEST_CASE("Flanger reset clears its delay lines", "[flanger]")
{
    Flanger flanger;
    flanger.setParameter(Flanger::kFeedback, 90.0);
    flanger.prepare(kRate, 512);
    std::vector<Signal> io{noise(4096, 1), noise(4096, 2)};
    run(flanger, io);
    flanger.reset();
    std::vector<Signal> silence{constant(4096, 0.0f), constant(4096, 0.0f)};
    run(flanger, silence);
    CHECK(allZero(silence[0]));
    CHECK(allZero(silence[1]));
}

TEST_CASE("Flanger recovers from a NaN input", "[flanger]")
{
    Flanger flanger;
    flanger.setParameter(Flanger::kFeedback, 90.0);
    flanger.prepare(kRate, 512);
    Signal signal = noise(4096);
    signal[10] = std::numeric_limits<float>::infinity();
    signal[20] = std::numeric_limits<float>::quiet_NaN();
    runMono(flanger, signal);
    Signal after = noise(4096);
    runMono(flanger, after);
    CHECK(allFinite(after));
}
