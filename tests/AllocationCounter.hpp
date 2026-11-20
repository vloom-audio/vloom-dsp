// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

namespace vloom::dsp::test {

/// Counts the allocations and frees made with operator new and delete on
/// this thread while it lives (the test executable replaces the global
/// operators). For checking that real-time code does neither.
class AllocationCounter {
public:
    AllocationCounter() noexcept;
    ~AllocationCounter();
    AllocationCounter(const AllocationCounter&) = delete;
    AllocationCounter& operator=(const AllocationCounter&) = delete;

    [[nodiscard]] long allocations() const noexcept;
    [[nodiscard]] long deallocations() const noexcept;

private:
    long allocationsBefore_ = 0;
    long deallocationsBefore_ = 0;
};

} // namespace vloom::dsp::test
