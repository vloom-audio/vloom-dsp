// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include <atomic>

namespace vloom::dsp {

/// A value the audio thread writes and any other thread reads, without a
/// lock: a meter reading such as gain reduction. Unlike a bare std::atomic,
/// it can be copied (the copy takes the current value), so the processor
/// that holds it can be copied and moved too.
template <typename T> class SharedValue {
public:
    static_assert(std::atomic<T>::is_always_lock_free);

    constexpr SharedValue() noexcept = default;
    constexpr explicit SharedValue(T value) noexcept
        : value_(value)
    {
    }
    SharedValue(const SharedValue& other) noexcept
        : value_(other.load())
    {
    }
    SharedValue& operator=(const SharedValue& other) noexcept
    {
        store(other.load());
        return *this;
    }
    ~SharedValue() = default;

    void store(T value) noexcept { value_.store(value, std::memory_order_relaxed); }
    [[nodiscard]] T load() const noexcept { return value_.load(std::memory_order_relaxed); }

private:
    std::atomic<T> value_{};
};

} // namespace vloom::dsp
