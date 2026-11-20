// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace vloom::dsp {

/// A delay line for one channel: push one sample in per frame, read any
/// earlier one back. prepare() allocates; everything else is real-time safe.
class DelayLine {
public:
    /// Makes room for reading `maxDelayFrames` back, and clears the line.
    void prepare(int maxDelayFrames)
    {
        std::size_t size = 4;
        const auto wanted = static_cast<std::size_t>(std::max(maxDelayFrames, 1)) + 2;
        while (size < wanted)
            size *= 2;
        buffer_.assign(size, 0.0f);
        mask_ = size - 1;
        write_ = 0;
    }

    void clear() noexcept
    {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        write_ = 0;
    }

    /// Adds the newest sample.
    void push(float sample) noexcept
    {
        if (buffer_.empty())
            return;
        buffer_[write_] = sample;
        write_ = (write_ + 1) & mask_;
    }

    /// The sample pushed `delay` pushes ago, the newest being 1; kept between
    /// 1 and capacity() + 1. 0 before prepare().
    [[nodiscard]] float tap(int delay) const noexcept
    {
        if (buffer_.empty())
            return 0.0f;
        const auto d = static_cast<std::size_t>(std::clamp(delay, 1, static_cast<int>(mask_)));
        return buffer_[(write_ - d) & mask_];
    }

    /// Like tap(), between samples (linear interpolation): `delay` >= 1.
    [[nodiscard]] float tapFractional(double delay) const noexcept
    {
        if (!(delay >= 1.0))
            delay = 1.0;
        const double whole = std::floor(delay);
        const auto fraction = static_cast<float>(delay - whole);
        const int d = static_cast<int>(std::min(whole, static_cast<double>(mask_)));
        const float a = tap(d);
        const float b = tap(d + 1);
        return a + fraction * (b - a);
    }

    /// The longest delay tap() can read (0 before prepare()).
    [[nodiscard]] int capacity() const noexcept { return buffer_.empty() ? 0 : static_cast<int>(mask_) - 1; }

private:
    std::vector<float> buffer_;
    std::size_t mask_ = 0;
    std::size_t write_ = 0;
};

} // namespace vloom::dsp
