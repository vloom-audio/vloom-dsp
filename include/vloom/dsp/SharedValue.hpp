// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include <array>
#include <atomic>
#include <cstddef>

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

/// The values behind a processor's readouts (HasReadouts, ADR 0005): the
/// audio thread stores, any thread loads. An index out of range stores
/// nothing and loads 0.
template <std::size_t N> class ReadoutValues {
public:
    void store(std::size_t index, float value) noexcept
    {
        if (index < N)
            values_[index].store(value);
    }
    [[nodiscard]] float load(std::size_t index) const noexcept
    {
        return index < N ? values_[index].load() : 0.0f;
    }
    /// Every value back to 0.
    void clear() noexcept
    {
        for (SharedValue<float>& value : values_)
            value.store(0.0f);
    }

private:
    std::array<SharedValue<float>, N> values_{};
};

} // namespace vloom::dsp
