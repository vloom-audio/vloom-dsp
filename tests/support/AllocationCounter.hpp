// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include <cstddef>

namespace vloom::test {

/// How many times the test program has called operator new so far.
[[nodiscard]] std::size_t allocationCount() noexcept;

} // namespace vloom::test
