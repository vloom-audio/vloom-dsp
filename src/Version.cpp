// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/Version.hpp"

namespace vloom::dsp {

const char* version() noexcept
{
    return VLOOM_DSP_VERSION; // defined by CMake from project(VERSION)
}

} // namespace vloom::dsp
