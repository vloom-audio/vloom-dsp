// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/Version.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string_view>

TEST_CASE("The version comes from the CMake project", "[version]")
{
    CHECK(std::string_view(vloom::dsp::version()) == "0.1.0");
}
