// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include <array>
#include <cstddef>

namespace vloom::dsp {

/// Runs a nonlinear stage at 2 or 4 times the sample rate, so the harmonics
/// it adds above half the sample rate are filtered away instead of folding
/// back (aliasing).
///
/// Built from linear-phase half-band FIR filters (Kaiser windowed sinc,
/// about 80 dB of stopband from 0.58 of the base Nyquist frequency), so the
/// delay is a whole number of frames at the base rate: latencyFrames().
/// Real-time safe: no allocation, any number of frames. Up to kMaxChannels
/// channels, each with its own memory.
///
/// Per frame and channel: upsample() gives factor() samples at the high
/// rate; work on them; downsample() takes them back to one.
class Oversampler {
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kMaxFactor = 4;

    Oversampler() noexcept;

    /// 2 or 4 (anything else: 4); clears the memory.
    void setFactor(int factor) noexcept;
    [[nodiscard]] int factor() const noexcept { return factor_; }
    /// The delay from the input of upsample() to the output of downsample(),
    /// at the base rate: 31 frames at 2x, 36 at 4x.
    [[nodiscard]] int latencyFrames() const noexcept { return factor_ == 2 ? kLatency2x : kLatency4x; }

    /// Writes factor() samples for input `sample` of `channel` to `out`.
    void upsample(int channel, float sample, float* out) noexcept;
    /// Takes factor() samples of `channel` from `in`, returns one.
    [[nodiscard]] float downsample(int channel, const float* in) noexcept;

    void reset() noexcept;

    static constexpr int kLatency2x = 31;
    static constexpr int kLatency4x = 36;

private:
    /// A half-band filter of 4 K + 3 taps: half of them are zero and the
    /// centre one is 1/2, so only the 2 K + 2 even taps are stored.
    template <int K> struct Stage {
        static constexpr int kEven = 2 * K + 2;
        std::array<double, kEven> even{}; ///< taps 0, 2, 4, ... of the filter

        /// The memory of one channel: the last kEven values, stored twice so
        /// they can be read without wrapping.
        struct History {
            std::array<double, 2 * kEven> values{};
            int at = 0;
            void push(double value) noexcept
            {
                at = at == 0 ? kEven - 1 : at - 1;
                values[static_cast<std::size_t>(at)] = value;
                values[static_cast<std::size_t>(at + kEven)] = value;
            }
            /// The value pushed `age` pushes before the newest (0: newest).
            [[nodiscard]] double operator[](int age) const noexcept
            {
                return values[static_cast<std::size_t>(at + age)];
            }
        };
        struct Channel {
            History up; ///< inputs to upsampling
            History downEven; ///< even samples into downsampling
            History downOdd; ///< odd samples into downsampling
        };
        std::array<Channel, kMaxChannels> channels{};

        void design(double beta) noexcept;
        void up(int channel, double x, double* out) noexcept;
        [[nodiscard]] double down(int channel, const double* in) noexcept;
        void clear() noexcept { channels = {}; }
    };

    Stage<15> first_; ///< base rate <-> 2x: 63 taps
    Stage<4> second_; ///< 2x <-> 4x: 19 taps
    /// One frame of delay at 2x in the 4x path, so its latency is whole.
    std::array<double, kMaxChannels> pad_{};
    int factor_ = 4;
};

} // namespace vloom::dsp
