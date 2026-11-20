// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "ModTestSignals.hpp"
#include "vloom/dsp/effects/TransientShaper.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string_view>

using Catch::Approx;
using vloom::dsp::TransientShaper;
using namespace modtest;

namespace {

constexpr double kRate = 48000.0;
constexpr std::size_t kOnset = 4800; // 100 ms of silence first

/// A drum-like hit: noise that starts at once and dies away over ~80 ms.
Signal drumHit(float level = 0.8f)
{
    Signal hit = constant(static_cast<int>(kRate), 0.0f);
    const Signal source = noise(static_cast<int>(kRate), 9);
    for (std::size_t i = kOnset; i < hit.size(); ++i)
        hit[i] = level * source[i]
            * static_cast<float>(std::exp(-static_cast<double>(i - kOnset) / (0.08 * kRate)));
    return hit;
}

/// The level of the first 5 ms of the hit over that of 100 .. 300 ms, in dB.
double onsetOverTailDb(const Signal& signal)
{
    return rmsDb(signal, kOnset, kOnset + 240) - rmsDb(signal, kOnset + 4800, kOnset + 14400);
}

double shapedOnsetOverTailDb(double attack, double sustain)
{
    TransientShaper shaper;
    shaper.setParameter(TransientShaper::kAttack, attack);
    shaper.setParameter(TransientShaper::kSustain, sustain);
    shaper.prepare(kRate, 512);
    Signal signal = drumHit();
    runMono(shaper, signal);
    return onsetOverTailDb(signal);
}

} // namespace

TEST_CASE("Transient shaper describes itself and its parameters", "[transient]")
{
    CHECK(std::string_view(TransientShaper::spec().id) == "vloom.transient");
    checkParameters<TransientShaper>({"Attack", "Sustain", "Speed", "Output", "Mix"});
    CHECK(TransientShaper::parameters()[TransientShaper::kAttack].minValue == -100.0);
    CHECK(TransientShaper::parameters()[TransientShaper::kAttack].maxValue == 100.0);
    CHECK(TransientShaper().latencyFrames() == 0);
}

TEST_CASE("Transient shaper attack boost raises a hit's onset over its tail", "[transient]")
{
    const double dry = onsetOverTailDb(drumHit());
    CHECK(shapedOnsetOverTailDb(0.0, 0.0) == Approx(dry).margin(1.0e-6));
    CHECK(shapedOnsetOverTailDb(100.0, 0.0) > dry + 6.0);
    CHECK(shapedOnsetOverTailDb(-100.0, 0.0) < dry - 3.0);
    CHECK(shapedOnsetOverTailDb(0.0, 100.0) < dry - 3.0); // a longer tail
    CHECK(shapedOnsetOverTailDb(0.0, -100.0) > dry + 3.0); // a shorter tail
}

TEST_CASE("Transient shaper does not depend on the input level", "[transient]")
{
    const auto shape = [](float level) {
        TransientShaper shaper;
        shaper.setParameter(TransientShaper::kAttack, 70.0);
        shaper.setParameter(TransientShaper::kSustain, -40.0);
        shaper.prepare(kRate, 512);
        const Signal in = drumHit(level);
        Signal out = in;
        runMono(shaper, out);
        Signal gain(out.size(), 0.0f);
        for (std::size_t i = 0; i < out.size(); ++i)
            if (std::abs(in[i]) > level * 1.0e-3f)
                gain[i] = out[i] / in[i];
        return gain;
    };
    const Signal loud = shape(1.0f);
    const Signal quiet = shape(0.01f);
    for (std::size_t i = kOnset; i < loud.size(); i += 7)
        if (loud[i] != 0.0f && quiet[i] != 0.0f)
            CHECK(quiet[i] == Approx(loud[i]).epsilon(1.0e-3));
}

TEST_CASE("Transient shaper at its defaults passes the input through bit for bit", "[transient]")
{
    TransientShaper shaper;
    shaper.prepare(kRate, 512);
    const std::vector<Signal> in{drumHit(), noise(static_cast<int>(kRate), 3)};
    std::vector<Signal> out = in;
    run(shaper, out);
    CHECK(out == in);
}

TEST_CASE("Transient shaper at Mix 0 passes the input through bit for bit", "[transient]")
{
    TransientShaper shaper;
    shaper.setParameter(TransientShaper::kAttack, 100.0);
    shaper.setParameter(TransientShaper::kSustain, -100.0);
    shaper.setParameter(TransientShaper::kMix, 0.0);
    shaper.prepare(kRate, 512);
    const Signal in = drumHit();
    Signal out = in;
    runMono(shaper, out);
    CHECK(out == in);
}

TEST_CASE("Transient shaper output sets the level", "[transient]")
{
    TransientShaper shaper;
    shaper.setParameter(TransientShaper::kOutput, -6.0);
    shaper.prepare(kRate, 512);
    const Signal in = drumHit();
    Signal out = in;
    runMono(shaper, out);
    CHECK(rmsDb(out) - rmsDb(in) == Approx(-6.0).margin(1.0e-3));
}

TEST_CASE("Transient shaper turns silence into silence", "[transient]")
{
    TransientShaper shaper;
    shaper.setParameter(TransientShaper::kAttack, 100.0);
    shaper.setParameter(TransientShaper::kSustain, 100.0);
    shaper.prepare(kRate, 512);
    std::vector<Signal> io{constant(8000, 0.0f), constant(8000, 0.0f)};
    run(shaper, io);
    CHECK(allZero(io[0]));
    CHECK(allZero(io[1]));
}

TEST_CASE("Transient shaper reset clears its envelopes", "[transient]")
{
    TransientShaper shaper;
    shaper.setParameter(TransientShaper::kAttack, 100.0);
    shaper.prepare(kRate, 512);
    Signal first = drumHit();
    runMono(shaper, first);
    shaper.reset();
    Signal second = drumHit();
    runMono(shaper, second);
    CHECK(first == second);
}

TEST_CASE("Transient shaper stays bounded at every sample rate and setting", "[transient]")
{
    for (const double rate : {44100.0, 192000.0}) {
        for (const double speed : {0.0, 100.0}) {
            TransientShaper shaper;
            shaper.setParameter(TransientShaper::kAttack, 100.0);
            shaper.setParameter(TransientShaper::kSustain, 100.0);
            shaper.setParameter(TransientShaper::kSpeed, speed);
            shaper.prepare(rate, 512);
            Signal signal = noise(static_cast<int>(rate), 4);
            signal[77] = std::numeric_limits<float>::quiet_NaN();
            runMono(shaper, signal);
            Signal after = noise(4096, 5);
            runMono(shaper, after);
            CHECK(allFinite(after));
            CHECK(peak(after) < 8.0f); // at most +18 dB on a 1.0 input
        }
    }
}
