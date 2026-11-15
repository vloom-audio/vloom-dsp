// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace vloom::dsp {

/// A wavetable: kFrames single-cycle waveforms to scan through, each stored
/// at kLevels band limits so a cycle played at any pitch has no harmonic
/// above half the sample rate (no aliasing from the table itself).
///
/// The built-in tables are worked out on first use and then shared and
/// never changed: fetch them in a `prepare` step, read them on the audio
/// thread.
class Wavetable {
public:
    static constexpr int kSize = 2048; ///< samples per cycle
    static constexpr int kFrames = 32;
    /// Level 0 keeps every harmonic a cycle can hold (just under kSize / 2);
    /// each next level half as many, down to 1.
    static constexpr int kLevels = 11;

    enum class Shape {
        Basic, ///< sine, triangle, saw, square
        Sweep, ///< a saw through a resonant low-pass that opens
        Formant, ///< a saw through two moving vowel peaks
        Pulse, ///< a square that narrows to a thin pulse
    };
    static constexpr int kShapes = 4;
    [[nodiscard]] static const char* name(Shape shape) noexcept;

    /// The built-in table `shape`. The first call for a shape builds it:
    /// not real-time. Later calls only return it.
    [[nodiscard]] static const Wavetable& builtin(Shape shape);

    /// The level to play a cycle of `frequency` (Hz) from at `sampleRate`:
    /// the one with the most harmonics that all stay below half the rate.
    [[nodiscard]] static int levelFor(double frequency, double sampleRate) noexcept;

    /// One sample: `position` 0 .. 1 through the frames, `phase` 0 .. 1
    /// through the cycle, both read between their neighbours. Real-time.
    [[nodiscard]] float sample(float position, float phase, int level) const noexcept
    {
        const float framePlace = position * static_cast<float>(kFrames - 1);
        const int frame
            = framePlace >= static_cast<float>(kFrames - 1) ? kFrames - 2 : static_cast<int>(framePlace);
        const float frameMix = framePlace - static_cast<float>(frame);
        const float place = phase * static_cast<float>(kSize);
        const int index = place >= static_cast<float>(kSize) ? kSize - 1 : static_cast<int>(place);
        const float mix = place - static_cast<float>(index);
        const float* a = data_.data() + offset(level, frame) + index;
        const float* b = a + kStride;
        const float first = a[0] + (a[1] - a[0]) * mix;
        const float second = b[0] + (b[1] - b[0]) * mix;
        return first + (second - first) * frameMix;
    }

    /// The kSize samples of one frame at one level, for a host that draws it.
    [[nodiscard]] std::span<const float> frame(int frame, int level = 0) const noexcept
    {
        return {data_.data() + offset(level, frame), static_cast<std::size_t>(kSize)};
    }

private:
    explicit Wavetable(Shape shape);

    /// Each cycle is followed by a copy of its first sample, so reading
    /// between the last sample and the first needs no wrap.
    static constexpr int kStride = kSize + 1;
    [[nodiscard]] static std::size_t offset(int level, int frame) noexcept
    {
        return (static_cast<std::size_t>(level) * kFrames + static_cast<std::size_t>(frame)) * kStride;
    }

    std::vector<float> data_; ///< kLevels x kFrames cycles of kStride
};

} // namespace vloom::dsp
