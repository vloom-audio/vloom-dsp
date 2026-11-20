// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "support/EffectTesting.hpp"
#include "vloom/dsp/effects/Analyzer.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string_view>

using Catch::Approx;
using vloom::dsp::Analyzer;
using namespace vloom::test;

namespace {

constexpr double kRate = 48000.0;

float peakDb(const Analyzer& analyzer) { return analyzer.readout(Analyzer::kReadoutPeak); }

} // namespace

TEST_CASE("Analyzer describes itself, its parameters and its readout", "[analyzer]")
{
    CHECK(std::string_view(Analyzer::spec().id) == "vloom.analyzer");
    checkParameterList<Analyzer>();
    CHECK(Analyzer().latencyFrames() == 0);
    STATIC_CHECK(vloom::dsp::HasReadouts<Analyzer>);
    REQUIRE(Analyzer::readoutSpecs().size() == Analyzer::kReadoutCount);
    CHECK(vloom::dsp::findReadout(Analyzer::readoutSpecs(), "peak_db") == Analyzer::kReadoutPeak);

    // The mockup's defaults: Medium, 4.5 dB/oct, 72 dB, peak hold on, 4096.
    Analyzer analyzer;
    CHECK(analyzer.parameter(Analyzer::kSpeed) == static_cast<double>(Analyzer::Speed::Medium));
    CHECK(analyzer.parameter(Analyzer::kTilt) == 4.5);
    CHECK(analyzer.parameter(Analyzer::kRange) == 72.0);
    CHECK(analyzer.parameter(Analyzer::kPeakHold) == 1.0);
    CHECK(analyzer.parameter(Analyzer::kFreeze) == 0.0);
    CHECK(Analyzer::fftSize(static_cast<int>(analyzer.parameter(Analyzer::kResolution))) == 4096);
    CHECK(analyzer.parameter(Analyzer::kChannel) == static_cast<double>(Analyzer::Channel::Both));
    CHECK(analyzer.parameter(99) == 0.0);
}

TEST_CASE("Analyzer's parameters clamp, and its helpers say what they mean", "[analyzer]")
{
    Analyzer analyzer;
    analyzer.setParameter(Analyzer::kTilt, 9.0);
    CHECK(analyzer.parameter(Analyzer::kTilt) == 6.0);
    analyzer.setParameter(Analyzer::kRange, 1.0);
    CHECK(analyzer.parameter(Analyzer::kRange) == 24.0);
    analyzer.setParameter(Analyzer::kResolution, 3.4);
    CHECK(analyzer.parameter(Analyzer::kResolution) == 3.0);
    analyzer.setParameter(Analyzer::kResolution, std::numeric_limits<double>::quiet_NaN());
    CHECK(analyzer.parameter(Analyzer::kResolution) == 3.0);

    CHECK(Analyzer::fftSize(0) == 1024);
    CHECK(Analyzer::fftSize(4) == 16384);
    CHECK(Analyzer::fftSize(-3) == 1024);
    CHECK(Analyzer::fftSize(12) == 16384);
    CHECK(Analyzer::averagingSeconds(0) > Analyzer::averagingSeconds(1));
    CHECK(Analyzer::averagingSeconds(1) > Analyzer::averagingSeconds(2));
    CHECK(Analyzer::tiltDb(1000.0, 4.5) == 0.0);
    CHECK(Analyzer::tiltDb(2000.0, 4.5) == Approx(4.5));
    CHECK(Analyzer::tiltDb(250.0, 3.0) == Approx(-6.0));
    CHECK(Analyzer::tiltDb(0.0, 3.0) == 0.0);
}

TEST_CASE("Analyzer passes audio through bit for bit", "[analyzer]")
{
    const Signal left = noise(20000, 1, 0.9f);
    const Signal right = sine(997.0, 20000, kRate, 0.7f);
    for (const std::uint32_t id :
        {Analyzer::kSpeed, Analyzer::kFreeze, Analyzer::kChannel, Analyzer::kPeakHold}) {
        Analyzer analyzer;
        analyzer.prepare(kRate, 512);
        analyzer.setParameter(id, 2.0);
        Audio audio{left, right};
        render(analyzer, audio, 333);
        CHECK(audio[0] == left);
        CHECK(audio[1] == right);
    }
    Analyzer unprepared;
    Audio audio{left};
    render(unprepared, audio);
    CHECK(audio[0] == left);
}

TEST_CASE("Analyzer's peak readout holds, then falls", "[analyzer]")
{
    Analyzer analyzer;
    analyzer.prepare(kRate, 512);
    CHECK(peakDb(analyzer) == Approx(Analyzer::kFloorDb));

    // A 0.5 sine peaks at -6.02 dB.
    (void)renderMono(analyzer, sine(1000.0, 4800, kRate, 0.5f));
    CHECK(peakDb(analyzer) == Approx(-6.02).margin(0.01));

    // Quieter afterwards: held for kPeakHoldSeconds, then 20 dB a second.
    (void)renderMono(analyzer, sine(1000.0, 24000, kRate, 0.05f)); // 0.5 s
    CHECK(peakDb(analyzer) == Approx(-6.02).margin(0.01));
    (void)renderMono(analyzer, sine(1000.0, 48000, kRate, 0.05f)); // 1 s more: 0.5 s falling
    CHECK(peakDb(analyzer) == Approx(-6.02 - 10.0).margin(0.1));
    (void)renderMono(analyzer, sine(1000.0, 48000, kRate, 0.05f)); // down to the new level
    CHECK(peakDb(analyzer) == Approx(-26.02).margin(0.01));

    // Silence falls to the floor and stays there.
    (void)renderMono(analyzer, Signal(static_cast<std::size_t>(kRate * 8), 0.0f));
    CHECK(peakDb(analyzer) == Approx(Analyzer::kFloorDb));

    analyzer.reset();
    CHECK(peakDb(analyzer) == Approx(Analyzer::kFloorDb));
}

TEST_CASE("Analyzer's peak follows the channel, and Freeze holds it", "[analyzer]")
{
    const Signal quiet = sine(500.0, 4800, kRate, 0.1f); // -20 dB
    const Signal loud = sine(500.0, 4800, kRate, 0.8f); // -1.94 dB
    const auto measure = [&](Analyzer::Channel channel) {
        Analyzer analyzer;
        analyzer.prepare(kRate, 512);
        analyzer.setParameter(Analyzer::kChannel, static_cast<double>(channel));
        Audio audio{quiet, loud};
        render(analyzer, audio);
        return peakDb(analyzer);
    };
    CHECK(measure(Analyzer::Channel::Both) == Approx(-1.94).margin(0.01));
    CHECK(measure(Analyzer::Channel::Left) == Approx(-20.0).margin(0.01));
    CHECK(measure(Analyzer::Channel::Right) == Approx(-1.94).margin(0.01));

    Analyzer analyzer;
    analyzer.prepare(kRate, 512);
    (void)renderMono(analyzer, quiet);
    analyzer.setParameter(Analyzer::kFreeze, 1.0);
    (void)renderMono(analyzer, loud);
    CHECK(peakDb(analyzer) == Approx(-20.0).margin(0.01));
    analyzer.setParameter(Analyzer::kFreeze, 0.0);
    (void)renderMono(analyzer, loud);
    CHECK(peakDb(analyzer) == Approx(-1.94).margin(0.01));

    // NaN and infinity are not peaks.
    Signal bad = quiet;
    bad[10] = std::numeric_limits<float>::quiet_NaN();
    bad[20] = std::numeric_limits<float>::infinity();
    Analyzer fresh;
    fresh.prepare(kRate, 512);
    (void)renderMono(fresh, bad);
    CHECK(peakDb(fresh) == Approx(-20.0).margin(0.01));
}

TEST_CASE("Analyzer: no allocation, stable everywhere", "[analyzer]")
{
    Analyzer analyzer;
    analyzer.prepare(kRate, 512);
    checkNoAllocation(analyzer);
    checkNoAllocation(analyzer, 1);
    checkStableEverywhere<Analyzer>();
}
