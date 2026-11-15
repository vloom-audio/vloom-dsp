// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/Biquad.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <numbers>

using Catch::Approx;
using vloom::dsp::BiquadCoefficients;
using vloom::dsp::BiquadState;
namespace biquad = vloom::dsp::biquad;

namespace {

constexpr double kRate = 48000.0;

/// The level of a sine at `frequency` after the filter, in dB, measured on
/// whole cycles once the filter has settled.
double measuredDb(const BiquadCoefficients& c, double frequency)
{
    BiquadState state;
    const double step = 2.0 * std::numbers::pi * frequency / kRate;
    const int settle = 48000;
    const int measure = 48000;
    double sum = 0.0;
    for (int i = 0; i < settle + measure; ++i) {
        const double y = state.process(c, std::sin(step * i));
        if (i >= settle)
            sum += y * y;
    }
    return 10.0 * std::log10(2.0 * sum / measure);
}

} // namespace

TEST_CASE("The default biquad passes its input through", "[biquad]")
{
    BiquadState state;
    const BiquadCoefficients c;
    for (const double x : {0.0, 1.0, -0.25, 0.5})
        CHECK(state.process(c, x) == x);
    CHECK(c.magnitudeDb(1000.0, kRate) == Approx(0.0).margin(1e-9));
}

TEST_CASE("A bell has its gain at its frequency and none far away", "[biquad]")
{
    const BiquadCoefficients c = biquad::peak(1000.0, kRate, 6.0, 1.0);
    CHECK(c.magnitudeDb(1000.0, kRate) == Approx(6.0).margin(1e-6));
    CHECK(c.magnitudeDb(20.0, kRate) == Approx(0.0).margin(0.05));
    CHECK(c.magnitudeDb(20000.0, kRate) == Approx(0.0).margin(0.05));
    // A higher Q is narrower: less gain an octave away.
    CHECK(biquad::peak(1000.0, kRate, 6.0, 8.0).magnitudeDb(2000.0, kRate) < c.magnitudeDb(2000.0, kRate));
    // A cut mirrors a boost.
    CHECK(biquad::peak(1000.0, kRate, -6.0, 1.0).magnitudeDb(1500.0, kRate)
        == Approx(-c.magnitudeDb(1500.0, kRate)).margin(1e-9));
}

TEST_CASE("Shelves lift one side and leave the other", "[biquad]")
{
    const BiquadCoefficients low = biquad::lowShelf(200.0, kRate, 9.0);
    CHECK(low.magnitudeDb(10.0, kRate) == Approx(9.0).margin(0.05));
    CHECK(low.magnitudeDb(200.0, kRate) == Approx(4.5).margin(0.01)); // half way at the corner
    CHECK(low.magnitudeDb(15000.0, kRate) == Approx(0.0).margin(0.05));

    const BiquadCoefficients high = biquad::highShelf(5000.0, kRate, -12.0);
    CHECK(high.magnitudeDb(23000.0, kRate) == Approx(-12.0).margin(0.05));
    CHECK(high.magnitudeDb(5000.0, kRate) == Approx(-6.0).margin(0.01));
    CHECK(high.magnitudeDb(50.0, kRate) == Approx(0.0).margin(0.05));
}

TEST_CASE("Cuts are 3 dB down at their frequency and fall 12 dB per octave", "[biquad]")
{
    const BiquadCoefficients high = biquad::highPass(400.0, kRate);
    CHECK(high.magnitudeDb(400.0, kRate) == Approx(-3.01).margin(0.01));
    CHECK(high.magnitudeDb(50.0, kRate) - high.magnitudeDb(25.0, kRate) == Approx(12.04).margin(0.05));
    CHECK(high.magnitudeDb(10000.0, kRate) == Approx(0.0).margin(0.01));

    const BiquadCoefficients low = biquad::lowPass(400.0, kRate);
    CHECK(low.magnitudeDb(400.0, kRate) == Approx(-3.01).margin(0.01));
    // Well below half the sample rate, where the response bends down to its zero.
    CHECK(low.magnitudeDb(1600.0, kRate) - low.magnitudeDb(3200.0, kRate) == Approx(12.0).margin(0.3));
    CHECK(low.magnitudeDb(20.0, kRate) == Approx(0.0).margin(0.01));
}

TEST_CASE("A filter does to a sine what its response says", "[biquad]")
{
    const BiquadCoefficients c = biquad::peak(2000.0, kRate, -9.0, 2.0);
    for (const double frequency : {500.0, 2000.0, 3000.0})
        CHECK(measuredDb(c, frequency) == Approx(c.magnitudeDb(frequency, kRate)).margin(0.01));
}

TEST_CASE("Bad design values give a filter that passes through or stays stable", "[biquad]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (const BiquadCoefficients& c :
        {biquad::peak(nan, kRate, 6.0, 1.0), biquad::peak(1000.0, 0.0, 6.0, 1.0),
            biquad::lowShelf(100.0, inf, 6.0), biquad::highPass(100.0, kRate, nan)}) {
        CHECK(c.b0 == 1.0);
        CHECK(c.b1 == 0.0);
        CHECK(c.a2 == 0.0);
    }
    // Out of range: clamped. Stable means both poles inside the unit circle.
    for (const BiquadCoefficients& c :
        {biquad::peak(1.0e9, kRate, 500.0, 1.0e9), biquad::peak(-5.0, kRate, -500.0, 0.0),
            biquad::lowPass(1.0e9, kRate), biquad::highShelf(0.0, kRate, nan)}) {
        CHECK(std::isfinite(c.b0));
        CHECK(std::abs(c.a2) < 1.0);
        CHECK(std::abs(c.a1) < 1.0 + c.a2);
    }
}

TEST_CASE("A biquad's tail ends in zeros, not denormals", "[biquad]")
{
    const BiquadCoefficients c = biquad::peak(100.0, kRate, 12.0, 10.0);
    BiquadState state;
    (void)state.process(c, 1.0);
    for (int block = 0; block < 4000; ++block) {
        for (int i = 0; i < 64; ++i)
            (void)state.process(c, 0.0);
        state.flushDenormals();
    }
    CHECK(state.s1 == 0.0);
    CHECK(state.s2 == 0.0);
}
