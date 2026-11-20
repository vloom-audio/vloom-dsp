// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

// Building blocks shared by the modulation effects (chorus, flanger, phaser,
// tremolo). Not a public API: names and behaviour may change.

#include "vloom/dsp/NoteValue.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace vloom::dsp::detail {

/// `value`, or 0 if it is NaN, infinite, or too small to hear: keeps feedback
/// paths free of denormals and of a bad sample that would ring forever.
[[nodiscard]] inline float sanitize(float value) noexcept
{
    const float size = std::abs(value);
    return size > 1.0e-20f && size < 1.0e10f ? value : 0.0f;
}

/// The value in -1 .. 1 of an LFO shape at `phase` (in cycles; only the part
/// after the decimal point counts). All shapes are 0 at phase 0, peak (+1) at
/// 0.25 and dip (-1) at 0.75, like a sine.
enum class LfoShape { Sine, Triangle, Square };

[[nodiscard]] inline float lfoValue(LfoShape shape, double phase) noexcept
{
    phase -= std::floor(phase);
    switch (shape) {
    case LfoShape::Triangle: {
        double q = phase + 0.25;
        q -= std::floor(q);
        return static_cast<float>(1.0 - 4.0 * std::abs(q - 0.5));
    }
    case LfoShape::Square: {
        // A square with rounded edges (no click): a sine pushed into tanh.
        constexpr double kSharpness = 5.0;
        constexpr double kNormal = 1.0 / 0.99990920426259513; // 1 / tanh(kSharpness): peaks at exactly 1
        return static_cast<float>(std::tanh(kSharpness * std::sin(2.0 * std::numbers::pi * phase)) * kNormal);
    }
    case LfoShape::Sine:
        break;
    }
    return static_cast<float>(std::sin(2.0 * std::numbers::pi * phase));
}

/// A free-running phase in cycles (0 .. 1). It does not follow the host's
/// transport position: a synced rate has the right period, not a fixed
/// alignment to the bar.
class LfoPhase {
public:
    /// Moves `hz` cycles per second at `sampleRate`.
    void setRate(double hz, double sampleRate) noexcept
    {
        const double increment = hz / sampleRate;
        increment_ = std::isfinite(increment) && increment > 0.0 ? std::min(increment, 0.5) : 0.0;
    }

    /// The phase for this sample; moves one sample on.
    double advance() noexcept
    {
        const double now = phase_;
        phase_ += increment_;
        if (phase_ >= 1.0)
            phase_ -= std::floor(phase_);
        return now;
    }

    void reset() noexcept { phase_ = 0.0; }
    [[nodiscard]] double phase() const noexcept { return phase_; }
    [[nodiscard]] double increment() const noexcept { return increment_; }

private:
    double phase_ = 0.0;
    double increment_ = 0.0;
};

/// The rate of an LFO that is either free (`hz`) or synced to the note at
/// `noteIndex` (see kNoteValues) at `bpm`.
[[nodiscard]] inline double lfoRateHz(bool synced, double hz, int noteIndex, double bpm) noexcept
{
    return synced ? noteHz(noteIndex, bpm) : hz;
}

/// A delay line read at a fractional delay with cubic (Hermite)
/// interpolation. prepare() allocates; the rest is real-time safe.
class FractionalDelay {
public:
    /// Room for delays up to `maxDelayFrames`; clears the line.
    void prepare(int maxDelayFrames)
    {
        std::size_t size = 4;
        while (size < static_cast<std::size_t>(std::max(maxDelayFrames, 1)) + 4)
            size *= 2;
        buffer_.assign(size, 0.0f);
        mask_ = size - 1;
        write_ = 0;
        maxDelay_ = static_cast<double>(size - 3);
    }

    void clear() noexcept
    {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        write_ = 0;
    }

    /// The input `delay` frames before the one push() takes next: 1 is the
    /// last one pushed. Kept between 2 and the prepared length (the
    /// interpolation looks one frame to either side). 0 before prepare().
    [[nodiscard]] float read(double delay) const noexcept
    {
        if (buffer_.empty())
            return 0.0f;
        delay = std::clamp(delay, 2.0, maxDelay_);
        const double whole = std::floor(delay);
        const auto t = static_cast<float>(delay - whole);
        const std::size_t back = static_cast<std::size_t>(whole);
        const std::size_t base = write_ + buffer_.size(); // keeps the index positive
        const float newer = buffer_[(base - back + 1) & mask_];
        const float x0 = buffer_[(base - back) & mask_];
        const float x1 = buffer_[(base - back - 1) & mask_];
        const float older = buffer_[(base - back - 2) & mask_];
        const float c1 = 0.5f * (x1 - newer);
        const float c2 = newer - 2.5f * x0 + 2.0f * x1 - 0.5f * older;
        const float c3 = 0.5f * (older - newer) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    void push(float value) noexcept
    {
        if (buffer_.empty())
            return;
        buffer_[write_] = value;
        write_ = (write_ + 1) & mask_;
    }

private:
    std::vector<float> buffer_;
    std::size_t mask_ = 0;
    std::size_t write_ = 0;
    double maxDelay_ = 2.0;
};

/// A first-order all-pass filter: y = a x + x1 - a y1. Unit gain at every
/// frequency; the phase passes -90 degrees at the frequency `a` was made for.
struct AllpassStage {
    float x1 = 0.0f;
    float y1 = 0.0f;

    /// The coefficient for a -90 degree point at `frequency` (Hz).
    [[nodiscard]] static float coefficient(double frequency, double sampleRate) noexcept
    {
        const double t = std::tan(std::numbers::pi * frequency / sampleRate);
        return static_cast<float>((t - 1.0) / (t + 1.0));
    }

    [[nodiscard]] float process(float a, float x) noexcept
    {
        const float y = a * (x - y1) + x1;
        x1 = x;
        y1 = sanitize(y);
        return y1;
    }

    void clear() noexcept { *this = {}; }
};

/// A small, repeatable random number generator (xorshift32) for the audio
/// thread.
class Random {
public:
    /// In 0 .. 1.
    [[nodiscard]] float next() noexcept
    {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return static_cast<float>(state_ >> 8) * (1.0f / 16777216.0f);
    }

    void reset() noexcept { state_ = kSeed; }

private:
    static constexpr std::uint32_t kSeed = 0x9e3779b9u;
    std::uint32_t state_ = kSeed;
};

/// The factor for a level in dB.
[[nodiscard]] inline float dbToGain(double db) noexcept
{
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

/// A ParameterSpec-clamped value: in range, rounded if stepped.
template <typename Spec> [[nodiscard]] double clampToSpec(const Spec& spec, double value) noexcept
{
    value = std::clamp(value, spec.minValue, spec.maxValue);
    return spec.stepped ? std::round(value) : value;
}

} // namespace vloom::dsp::detail
