// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/SmoothedValue.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

using vloom::dsp::SmoothedValue;

TEST_CASE("A smoothed value ramps in a straight line and lands on its target", "[smoothed]")
{
    SmoothedValue value;
    value.reset(1000.0, 0.004, 0.0f); // 4 frames
    value.setTarget(1.0f);
    CHECK(value.isSmoothing());
    CHECK(value.next() == Catch::Approx(0.25f));
    CHECK(value.next() == Catch::Approx(0.5f));
    CHECK(value.next() == Catch::Approx(0.75f));
    CHECK(value.next() == 1.0f); // exactly
    CHECK_FALSE(value.isSmoothing());
    CHECK(value.next() == 1.0f);
}

TEST_CASE("A new target mid-ramp glides on from where the value is", "[smoothed]")
{
    SmoothedValue value;
    value.reset(1000.0, 0.004, 0.0f);
    value.setTarget(1.0f);
    value.next();
    value.next(); // 0.5
    value.setTarget(0.0f);
    CHECK(value.next() == Catch::Approx(0.375f));
    value.next();
    value.next();
    CHECK(value.next() == 0.0f);
}

TEST_CASE("Without a ramp a smoothed value jumps", "[smoothed]")
{
    SmoothedValue value;
    value.reset(48000.0, 0.0, 0.5f);
    value.setTarget(2.0f);
    CHECK_FALSE(value.isSmoothing());
    CHECK(value.next() == 2.0f);

    value.reset(std::numeric_limits<double>::quiet_NaN(), 0.01, 1.0f);
    value.setTarget(3.0f);
    CHECK(value.next() == 3.0f);
}

TEST_CASE("A smoothed value ignores non-finite targets", "[smoothed]")
{
    SmoothedValue value;
    value.reset(1000.0, 0.004, 1.0f);
    value.setTarget(std::numeric_limits<float>::quiet_NaN());
    value.setTarget(std::numeric_limits<float>::infinity());
    value.setCurrentAndTarget(std::numeric_limits<float>::quiet_NaN());
    CHECK_FALSE(value.isSmoothing());
    CHECK(value.next() == 1.0f);
}
