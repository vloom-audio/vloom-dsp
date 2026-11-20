// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/Svf.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using Catch::Approx;
using namespace vloom::dsp;

TEST_CASE("A state variable filter's outputs add up to its input", "[svf]")
{
    const SvfCoefficients c = svf::design(1000.0, 48000.0, 2.0);
    SvfState state;
    for (int i = 0; i < 1000; ++i) {
        const double x = std::sin(0.05 * i) + (i % 7 == 0 ? 0.5 : 0.0);
        const SvfOutputs o = state.process(c, x);
        REQUIRE(o.low + c.k * o.band + o.high == Approx(x).margin(1e-12));
    }
}

TEST_CASE("A state variable filter's response", "[svf]")
{
    const SvfCoefficients c = svf::design(1000.0, 48000.0, 0.7071067811865476);
    const svf::Response at = svf::response(c, 1000.0, 48000.0);
    CHECK(20.0 * std::log10(std::abs(at.low)) == Approx(-3.0103).margin(1e-3));
    CHECK(20.0 * std::log10(std::abs(at.high)) == Approx(-3.0103).margin(1e-3));
    CHECK(std::abs(c.k * at.band) == Approx(1.0));
    CHECK(std::abs(svf::response(c, 0.0, 48000.0).low) == Approx(1.0));

    // Bad values: a stable filter, not NaN.
    const SvfCoefficients bad = svf::design(std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0);
    CHECK(std::isfinite(bad.a1));
    CHECK(bad.g > 0.0);
}
