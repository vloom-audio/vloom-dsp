// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include <cmath>

namespace vloom::dsp {

/// A parameter that glides to a new target in a straight line, so a change
/// does not click. Real-time safe: no allocation, no locks.
///
/// Call reset() when the sample rate is known, setTarget() whenever the value
/// changes, and next() once per sample.
class SmoothedValue {
public:
    explicit SmoothedValue(float value = 0.0f) noexcept
        : current_(value)
        , target_(value)
    {
    }

    /// Sets the ramp length and jumps to `value`. A non-positive or
    /// non-finite rate or time means no ramp.
    void reset(double sampleRate, double rampSeconds, float value) noexcept
    {
        const double frames = sampleRate * rampSeconds;
        rampFrames_ = std::isfinite(frames) && frames >= 1.0 && frames < 1.0e9 ? static_cast<int>(frames) : 0;
        setCurrentAndTarget(value);
    }

    /// Glides to `value` over the ramp length. A non-finite value is ignored.
    void setTarget(float value) noexcept
    {
        if (!std::isfinite(value) || value == target_)
            return;
        target_ = value;
        if (rampFrames_ == 0) {
            current_ = value;
            remaining_ = 0;
            return;
        }
        remaining_ = rampFrames_;
        step_ = (target_ - current_) / static_cast<float>(rampFrames_);
    }

    /// Jumps to `value` without a ramp.
    void setCurrentAndTarget(float value) noexcept
    {
        if (!std::isfinite(value))
            return;
        current_ = value;
        target_ = value;
        remaining_ = 0;
    }

    /// The value for this sample; moves one sample along the ramp.
    float next() noexcept
    {
        if (remaining_ == 0)
            return current_;
        if (--remaining_ == 0)
            current_ = target_; // land exactly, whatever the rounding
        else
            current_ += step_;
        return current_;
    }

    [[nodiscard]] bool isSmoothing() const noexcept { return remaining_ > 0; }
    [[nodiscard]] float current() const noexcept { return current_; }
    [[nodiscard]] float target() const noexcept { return target_; }

private:
    float current_ = 0.0f;
    float target_ = 0.0f;
    float step_ = 0.0f;
    int rampFrames_ = 0;
    int remaining_ = 0;
};

} // namespace vloom::dsp
