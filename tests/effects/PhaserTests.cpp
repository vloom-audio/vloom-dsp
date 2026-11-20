// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "ModTestSignals.hpp"
#include "vloom/dsp/effects/Phaser.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

using Catch::Approx;
using vloom::dsp::Phaser;
using namespace modtest;

namespace {

constexpr double kRate = 48000.0;

/// The notches (dips below -20 dB) of a still phaser with `stages`, mixed
/// 50/50, between 20 Hz and 20 kHz.
int notchCount(int stages)
{
    Phaser phaser;
    phaser.setParameter(Phaser::kStages, stages);
    phaser.setParameter(Phaser::kDepth, 0.0);
    phaser.setParameter(Phaser::kFeedback, 0.0);
    phaser.setParameter(Phaser::kMix, 50.0);
    phaser.prepare(kRate, 512);
    Signal impulse = constant(16384, 0.0f);
    impulse[0] = 1.0f;
    runMono(phaser, impulse);

    constexpr int kPoints = 2400;
    std::vector<double> db(kPoints);
    for (int i = 0; i < kPoints; ++i)
        db[static_cast<std::size_t>(i)]
            = responseDb(impulse, 20.0 * std::pow(1000.0, i / (kPoints - 1.0)), kRate);
    int notches = 0;
    for (std::size_t i = 1; i + 1 < db.size(); ++i)
        if (db[i] < -20.0 && db[i] <= db[i - 1] && db[i] < db[i + 1])
            ++notches;
    return notches;
}

double levelSwingDb(Phaser& phaser)
{
    phaser.prepare(kRate, 512);
    Signal signal = sine(1000.0, 96000, kRate);
    runMono(phaser, signal);
    const std::vector<double> levels = windowLevels(signal, 480, 9600);
    const auto [low, high] = std::minmax_element(levels.begin(), levels.end());
    return *high - *low;
}

} // namespace

TEST_CASE("Phaser describes itself and its parameters", "[phaser]")
{
    CHECK(std::string_view(Phaser::spec().id) == "vloom.phaser");
    checkParameters<Phaser>(
        {"Rate", "Sync", "Note", "Depth", "Stages", "Frequency", "Feedback", "Stereo Phase", "Mix"});
    const auto& stages = Phaser::parameters()[Phaser::kStages];
    CHECK(stages.stepped);
    CHECK(stages.minValue == 2.0);
    CHECK(stages.maxValue == 12.0);
    CHECK(Phaser().latencyFrames() == 0);
}

TEST_CASE("Phaser makes one notch per two stages", "[phaser]")
{
    for (int stages = 2; stages <= 12; stages += 2) {
        INFO("stages: " << stages);
        CHECK(notchCount(stages) == stages / 2);
    }
}

TEST_CASE("Phaser rounds an odd stage count up", "[phaser]")
{
    Phaser phaser;
    phaser.setParameter(Phaser::kStages, 5.0);
    CHECK(phaser.stages() == 6);
    phaser.setParameter(Phaser::kStages, 1.0);
    CHECK(phaser.stages() == 2);
    phaser.setParameter(Phaser::kStages, 40.0);
    CHECK(phaser.stages() == 12);
}

TEST_CASE("Phaser turns silence into silence", "[phaser]")
{
    Phaser phaser;
    phaser.setParameter(Phaser::kFeedback, 95.0);
    phaser.prepare(kRate, 512);
    std::vector<Signal> io{constant(48000, 0.0f), constant(48000, 0.0f)};
    run(phaser, io);
    CHECK(allZero(io[0]));
    CHECK(allZero(io[1]));
}

TEST_CASE("Phaser at Mix 0 passes the input through bit for bit", "[phaser]")
{
    Phaser phaser;
    phaser.setParameter(Phaser::kMix, 0.0);
    phaser.prepare(kRate, 512);
    const std::vector<Signal> in{noise(20000, 1), noise(20000, 2)};
    std::vector<Signal> out = in;
    run(phaser, out);
    CHECK(out == in);
}

TEST_CASE("Phaser sweeps its notches over a tone", "[phaser]")
{
    Phaser still;
    still.setParameter(Phaser::kDepth, 0.0);
    CHECK(levelSwingDb(still) < 0.1);

    Phaser moving;
    moving.setParameter(Phaser::kRate, 1.0);
    moving.setParameter(Phaser::kDepth, 100.0);
    CHECK(levelSwingDb(moving) > 12.0);
}

TEST_CASE("Phaser fades between stage counts without a jump", "[phaser]")
{
    Phaser phaser;
    phaser.setParameter(Phaser::kDepth, 0.0);
    phaser.prepare(kRate, 512);
    Signal signal = sine(500.0, 24000, kRate);
    phaser.setParameter(Phaser::kStages, 12.0);
    runMono(phaser, signal, 64);
    float step = 0.0f;
    for (std::size_t i = 1; i < signal.size(); ++i)
        step = std::max(step, std::abs(signal[i] - signal[i - 1]));
    CHECK(step < 0.1f); // a 500 Hz tone at 0.5 moves at most ~0.033 per frame
}

TEST_CASE("Phaser stays bounded at full feedback at every sample rate", "[phaser]")
{
    for (const double feedback : {95.0, -95.0}) {
        for (const double rate : {44100.0, 96000.0, 192000.0}) {
            Phaser phaser;
            phaser.setParameter(Phaser::kFeedback, feedback);
            phaser.setParameter(Phaser::kDepth, 100.0);
            phaser.setParameter(Phaser::kRate, 10.0);
            phaser.setParameter(Phaser::kStages, 12.0);
            phaser.setParameter(Phaser::kFrequency, 8000.0);
            phaser.setParameter(Phaser::kMix, 100.0);
            phaser.prepare(rate, 512);
            std::vector<Signal> io{
                noise(static_cast<int>(rate), 5), sine(2000.0, static_cast<int>(rate), rate, 1.0f)};
            run(phaser, io, 300);
            CHECK(allFinite(io[0]));
            CHECK(peak(io[0]) < 21.0f);
            CHECK(peak(io[1]) < 21.0f);
        }
    }
}

TEST_CASE("Phaser follows the tempo when synced", "[phaser]")
{
    Phaser phaser;
    phaser.setParameter(Phaser::kSync, 1.0);
    phaser.setParameter(Phaser::kNote, vloom::dsp::note::kEighth);
    CHECK(phaser.rateHz() == Approx(4.0));
    phaser.setTempo(90.0);
    CHECK(phaser.rateHz() == Approx(3.0));
}

TEST_CASE("Phaser reset clears its filters", "[phaser]")
{
    Phaser phaser;
    phaser.setParameter(Phaser::kFeedback, 90.0);
    phaser.prepare(kRate, 512);
    Signal signal = noise(4096);
    runMono(phaser, signal);
    phaser.reset();
    Signal silence = constant(4096, 0.0f);
    runMono(phaser, silence);
    CHECK(allZero(silence));
}

TEST_CASE("Phaser recovers from a NaN input", "[phaser]")
{
    Phaser phaser;
    phaser.setParameter(Phaser::kFeedback, 90.0);
    phaser.prepare(kRate, 512);
    Signal signal = noise(4096);
    signal[30] = std::numeric_limits<float>::quiet_NaN();
    runMono(phaser, signal);
    Signal after = noise(4096);
    runMono(phaser, after);
    CHECK(allFinite(after));
}
