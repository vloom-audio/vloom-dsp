// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/DelayLine.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using vloom::dsp::DelayLine;

TEST_CASE("A delay line reads back what was pushed, whole and between samples", "[delayline]")
{
    DelayLine line;
    CHECK(line.tap(1) == 0.0f); // before prepare()
    line.prepare(100);
    CHECK(line.capacity() >= 100);
    for (int i = 1; i <= 200; ++i)
        line.push(static_cast<float>(i));
    CHECK(line.tap(1) == 200.0f);
    CHECK(line.tap(100) == 101.0f);
    CHECK(line.tapFractional(1.0) == 200.0f);
    CHECK(line.tapFractional(2.5) == Approx(198.5f));
    CHECK(line.tap(0) == 200.0f); // kept at 1
    CHECK(line.tapFractional(-3.0) == 200.0f);

    line.clear();
    CHECK(line.tap(1) == 0.0f);
    CHECK(line.tap(100) == 0.0f);
}
