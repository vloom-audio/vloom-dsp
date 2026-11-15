// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/Wavetable.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>
#include <string_view>

using Catch::Approx;
using vloom::dsp::Wavetable;

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

/// How much of harmonic `k` a cycle holds (its amplitude).
double harmonic(std::span<const float> cycle, int k)
{
    double cosine = 0.0;
    double sine = 0.0;
    for (std::size_t i = 0; i < cycle.size(); ++i) {
        const double angle = kTwoPi * k * static_cast<double>(i) / static_cast<double>(cycle.size());
        cosine += cycle[i] * std::cos(angle);
        sine += cycle[i] * std::sin(angle);
    }
    return 2.0 * std::hypot(cosine, sine) / static_cast<double>(cycle.size());
}

double peak(std::span<const float> cycle)
{
    double most = 0.0;
    for (float sample : cycle)
        most = std::max(most, static_cast<double>(std::abs(sample)));
    return most;
}

} // namespace

TEST_CASE("The basic table starts as a sine and ends as a square", "[wavetable]")
{
    const Wavetable& table = Wavetable::builtin(Wavetable::Shape::Basic);
    const auto first = table.frame(0);
    REQUIRE(first.size() == static_cast<std::size_t>(Wavetable::kSize));
    for (std::size_t i = 0; i < first.size(); i += 97)
        CHECK(first[i] == Approx(std::sin(kTwoPi * static_cast<double>(i) / Wavetable::kSize)).margin(1e-4));

    const auto last = table.frame(Wavetable::kFrames - 1);
    CHECK(harmonic(last, 2) < 1e-3); // a square has no even harmonics
    CHECK(harmonic(last, 3) == Approx(harmonic(last, 1) / 3.0).epsilon(0.01));
}

TEST_CASE("Every frame of every table is as loud as the next", "[wavetable]")
{
    for (int shape = 0; shape < Wavetable::kShapes; ++shape) {
        const Wavetable& table = Wavetable::builtin(static_cast<Wavetable::Shape>(shape));
        CHECK(std::string_view(Wavetable::name(static_cast<Wavetable::Shape>(shape))).size() > 0);
        for (int frame = 0; frame < Wavetable::kFrames; ++frame)
            CHECK(peak(table.frame(frame)) == Approx(1.0).margin(1e-4));
    }
    // Asked for twice: the same table, not a second one.
    CHECK(&Wavetable::builtin(Wavetable::Shape::Sweep) == &Wavetable::builtin(Wavetable::Shape::Sweep));
}

TEST_CASE("A level keeps only the harmonics it is for", "[wavetable]")
{
    const Wavetable& table = Wavetable::builtin(Wavetable::Shape::Basic);
    const int saw = (Wavetable::kFrames - 1) * 2 / 3; // near the saw: every harmonic
    for (int level : {0, 4, 7, 10}) {
        const int kept = (Wavetable::kSize / 2) >> level;
        const auto cycle = table.frame(saw, level);
        CHECK(harmonic(cycle, 1) > 0.1);
        // The fullest level stops one short: a cycle cannot hold harmonic kSize / 2.
        const int last = std::min(kept, Wavetable::kSize / 2 - 1);
        if (last > 1)
            CHECK(harmonic(cycle, last) > 1e-5); // the last one kept is there
        if (kept < Wavetable::kSize / 2 - 1) {
            CHECK(harmonic(cycle, kept + 1) < 1e-6); // the next is not
            CHECK(harmonic(cycle, kept * 2 + 1) < 1e-6);
        }
    }
}

TEST_CASE("The level for a pitch has no harmonic above half the rate", "[wavetable]")
{
    for (double rate : {44100.0, 48000.0, 96000.0, 192000.0}) {
        int before = 0;
        for (double hz = 16.0; hz < rate / 2.0; hz *= 1.37) {
            const int level = Wavetable::levelFor(hz, rate);
            REQUIRE(level >= 0);
            REQUIRE(level < Wavetable::kLevels);
            CHECK(level >= before); // higher pitch: never more harmonics
            before = level;
            const int kept = (Wavetable::kSize / 2) >> level;
            if (level < Wavetable::kLevels - 1)
                CHECK(kept * hz <= rate / 2.0);
        }
    }
    CHECK(Wavetable::levelFor(0.0, 48000.0) == Wavetable::kLevels - 1); // nonsense: the safest
    CHECK(Wavetable::levelFor(440.0, 0.0) == Wavetable::kLevels - 1);
}

TEST_CASE("Samples are read between their neighbours, to the end of the cycle and the table", "[wavetable]")
{
    const Wavetable& table = Wavetable::builtin(Wavetable::Shape::Basic);
    CHECK(table.sample(0.0f, 0.25f, 0) == Approx(1.0f).margin(1e-4)); // the top of the sine
    CHECK(table.sample(0.0f, 0.0f, 0) == Approx(0.0f).margin(1e-4));
    // Just before the cycle ends: between the last sample and the first again.
    CHECK(table.sample(0.0f, 0.99999f, 0) == Approx(0.0f).margin(1e-3));
    // The last frame, and half way between two frames.
    const auto last = table.frame(Wavetable::kFrames - 1);
    CHECK(table.sample(1.0f, 0.25f, 0) == Approx(last[Wavetable::kSize / 4]).margin(1e-4));
    const float between = 0.5f / static_cast<float>(Wavetable::kFrames - 1);
    const float expected = 0.5f * (table.frame(0)[300] + table.frame(1)[300]);
    CHECK(table.sample(between, 300.0f / Wavetable::kSize, 0) == Approx(expected).margin(1e-4));
}
