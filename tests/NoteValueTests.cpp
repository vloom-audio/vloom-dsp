// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/NoteValue.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string_view>

using Catch::Approx;
namespace dsp = vloom::dsp;

TEST_CASE("Note values run from longest to shortest", "[notevalue]")
{
    for (int i = 1; i < dsp::kNoteValueCount; ++i)
        CHECK(dsp::noteValue(i).beats < dsp::noteValue(i - 1).beats);
    CHECK(std::string_view(dsp::noteValue(dsp::note::kQuarter).name) == "1/4");
    CHECK(std::string_view(dsp::noteValue(dsp::note::kEighth).name) == "1/8");
    CHECK(std::string_view(dsp::noteValue(dsp::note::kSixteenth).name) == "1/16");
    CHECK(std::string_view(dsp::noteValue(dsp::note::kHalf).name) == "1/2");
    CHECK(std::string_view(dsp::noteValue(dsp::note::kWhole).name) == "1/1");
    CHECK(std::string_view(dsp::noteValue(dsp::note::kTwoBars).name) == "2/1");
    // Saved in projects: the list must not change, only grow.
    CHECK(dsp::kNoteValueCount == 20);
    CHECK(std::string_view(dsp::noteValue(0).name) == "4/1");
    CHECK(std::string_view(dsp::noteValue(19).name) == "1/32 T");
}

TEST_CASE("Note lengths follow the tempo", "[notevalue]")
{
    CHECK(dsp::noteSeconds(dsp::note::kQuarter, 120.0) == Approx(0.5));
    CHECK(dsp::noteSeconds(dsp::note::kQuarter, 90.0) == Approx(2.0 / 3.0));
    CHECK(dsp::noteHz(dsp::note::kEighth, 120.0) == Approx(4.0));
    CHECK(dsp::noteSeconds(dsp::note::kQuarter + 1, 120.0) == Approx(0.375)); // 1/8 dotted
    CHECK(dsp::noteSeconds(dsp::note::kQuarter + 2, 120.0) == Approx(1.0 / 3.0)); // 1/4 triplet
}

TEST_CASE("Note values clamp bad input", "[notevalue]")
{
    CHECK(&dsp::noteValue(-5) == &dsp::noteValue(0));
    CHECK(&dsp::noteValue(500) == &dsp::noteValue(dsp::kNoteValueCount - 1));
    CHECK(dsp::clampTempo(std::numeric_limits<double>::quiet_NaN()) == dsp::kDefaultTempo);
    CHECK(dsp::clampTempo(0.0) == dsp::kMinTempo);
    CHECK(dsp::clampTempo(5000.0) == dsp::kMaxTempo);
}
