// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include "vloom/dsp/DelayLine.hpp"
#include "vloom/dsp/ParameterValues.hpp"
#include "vloom/dsp/Processor.hpp"
#include "vloom/dsp/SharedValue.hpp"
#include "vloom/dsp/SmoothedValue.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vloom::dsp {

/// "vloom.limiter": a brickwall look-ahead limiter.
///
/// The signal is delayed by the look-ahead time while the gain it needs is
/// worked out ahead of it: the lowest gain over the look-ahead window (held,
/// then released at the release time), smoothed by a moving average over the
/// same window, so the gain is already down when a peak arrives and never
/// steps. That keeps every sample at or below the ceiling; a final clip at
/// the ceiling catches rounding.
///
/// True Peak also looks between samples (4x interpolation) so the peaks a
/// converter would make are limited too (within a few tenths of a dB); it
/// adds kTruePeakFrames of latency.
///
/// Latency: latencyFrames() = look-ahead (+ kTruePeakFrames). It changes
/// when Lookahead or True Peak change, and the host must ask again then.
/// Below the ceiling, with 0 dB input gain, the output is the input delayed
/// by the latency, bit for bit. Processes up to kMaxChannels channels;
/// others pass through undelayed.
class Limiter {
public:
    // Parameter ids: saved in projects, so they never change.
    enum Id : std::uint32_t {
        kInputGain,
        kRelease,
        kLookahead,
        kStereoLink,
        kTruePeak,
        kCeiling,
        kParameterCount
    };

    static constexpr int kMaxChannels = 2;
    static constexpr double kMaxLookaheadMs = 10.0;
    static constexpr int kTruePeakFrames = 4;
    static constexpr double kRampSeconds = 0.02;

    [[nodiscard]] static ProcessorSpec spec() noexcept { return {"vloom.limiter", "Limiter", "1.0"}; }
    [[nodiscard]] static std::span<const ParameterSpec> parameters() noexcept { return kParameters; }

    Limiter() noexcept;

    void prepare(double sampleRate, int maxBlockFrames);
    void setParameter(std::uint32_t id, double value) noexcept;
    void process(float* const* channels, int numChannels, int numFrames) noexcept;
    void reset() noexcept;
    /// 0 before prepare().
    [[nodiscard]] int latencyFrames() const noexcept
    {
        return sampleRate_ > 0.0 ? lookahead_ + (truePeak_ ? kTruePeakFrames : 0) : 0;
    }

    /// The most the gain was turned down during the last block, in dB (>= 0),
    /// for a meter. Safe to call from any thread.
    [[nodiscard]] float gainReductionDb() const noexcept { return gainReduction_.load(); }

private:
    static constexpr std::array<ParameterSpec, kParameterCount> kParameters{
        ParameterSpec{kInputGain, "Input Gain", "dB", -12.0, 24.0, 0.0, false, "Input"},
        ParameterSpec{kRelease, "Release", "ms", 1.0, 1000.0, 50.0, false, "Limiter"},
        ParameterSpec{kLookahead, "Lookahead", "ms", 0.5, kMaxLookaheadMs, 2.0, false, "Limiter"},
        ParameterSpec{kStereoLink, "Stereo Link", "%", 0.0, 100.0, 100.0, false, "Limiter"},
        ParameterSpec{kTruePeak, "True Peak", "", 0.0, 1.0, 1.0, true, "Limiter"},
        ParameterSpec{kCeiling, "Ceiling", "dB", -24.0, 0.0, -0.3, false, "Output"},
    };

    static constexpr int kInterpolationTaps = 8;
    static constexpr int kInterpolationPhases = 3; ///< 1/4, 1/2, 3/4 between samples

    /// The running minimum of the last `window` values (a monotonic queue).
    struct MinimumWindow {
        std::vector<double> values;
        std::vector<long long> stamps;
        std::size_t head = 0;
        std::size_t size = 0;
        void clear() noexcept { head = size = 0; }
        [[nodiscard]] double push(double value, long long now, int window) noexcept;
    };

    struct Channel {
        DelayLine audio;
        std::array<float, kInterpolationTaps> recent{}; ///< the last inputs, for true peak
        int recentAt = 0;
        double lastBetween = 0.0; ///< the true peak between the previous two samples
        MinimumWindow minimum;
        double released = 1.0; ///< the held minimum after the release
        std::vector<double> average; ///< the last lookahead + 1 gains
        double sum = 0.0;
        std::size_t averageAt = 0;
    };

    [[nodiscard]] double value(std::uint32_t id) const noexcept { return values_[id]; }
    void applyValues(bool jump) noexcept;
    /// Starts the gain path over for the current look-ahead, going on from
    /// the gain of the old one, `before` frames (keeps the audio).
    void restartGain(int before) noexcept;
    /// The peak level of `channel` for its sample `kTruePeakFrames` ago.
    [[nodiscard]] double truePeak(Channel& channel, float sample) const noexcept;

    std::array<double, kParameterCount> values_ = defaultValues(kParameters);
    double sampleRate_ = 0.0; ///< 0: not prepared, passes through
    int lookahead_ = 0; ///< frames
    bool truePeak_ = true;
    double ceiling_ = 1.0;
    double releaseFactor_ = 0.0;
    long long now_ = 0;
    SmoothedValue inputGain_{1.0f};
    std::array<Channel, kMaxChannels> channels_{};
    std::array<std::array<double, kInterpolationTaps>, kInterpolationPhases> interpolation_{};
    SharedValue<float> gainReduction_{0.0f};
};

static_assert(Effect<Limiter>);

} // namespace vloom::dsp
