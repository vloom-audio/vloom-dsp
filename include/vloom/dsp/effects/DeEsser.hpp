// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include "vloom/dsp/Biquad.hpp"
#include "vloom/dsp/Processor.hpp"
#include "vloom/dsp/SmoothedValue.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace vloom::dsp {

/// "vloom.deesser": turns sibilance down when it gets louder than a
/// threshold.
///
/// - Detection: a high-pass filter (12 dB per octave) at Frequency picks the
///   sibilant band of all channels together; its peak level, followed with a
///   kAttackSeconds attack and a kReleaseSeconds release, is compared with
///   Threshold. Everything above the threshold is taken off, up to Range dB.
/// - Mode Wideband turns the whole signal down; Split-band turns down only
///   the band above Frequency, with a high shelf at Frequency that cuts by
///   the reduction (a dynamic EQ; at 0 dB it passes through bit for bit).
/// - Listen plays the detected band alone, to find the frequency.
///
/// gainReductionDb() gives the reduction for a meter. Processes the first
/// kMaxChannels channels, linked; any others pass through. No latency.
class DeEsser {
public:
    // Parameter ids: saved in projects, so they never change.
    static constexpr std::uint32_t kFrequency = 0;
    static constexpr std::uint32_t kThreshold = 1;
    static constexpr std::uint32_t kListen = 2;
    static constexpr std::uint32_t kRange = 3;
    static constexpr std::uint32_t kMode = 4;
    static constexpr std::size_t kParameterCount = 5;

    /// The value of the Mode parameter.
    enum class Mode { Wideband, SplitBand };

    static constexpr int kMaxChannels = 8;
    static constexpr double kAttackSeconds = 0.0005;
    static constexpr double kReleaseSeconds = 0.06;
    static constexpr double kRampSeconds = 0.02;
    /// Moving settings are worked out once per this many frames.
    static constexpr int kControlFrames = 16;

    [[nodiscard]] static ProcessorSpec spec() noexcept { return {"vloom.deesser", "De-esser", "1.0"}; }
    [[nodiscard]] static std::span<const ParameterSpec> parameters() noexcept { return kParameters; }

    void prepare(double sampleRate, int maxBlockFrames);
    void setParameter(std::uint32_t id, double value) noexcept;
    void process(float* const* channels, int numChannels, int numFrames) noexcept;
    void reset() noexcept;
    [[nodiscard]] int latencyFrames() const noexcept { return 0; }

    /// How far the sibilant band (or, in Wideband mode, the signal) was
    /// turned down at the end of the last process() call, in dB (0 or more).
    /// Safe to read from any thread.
    [[nodiscard]] float gainReductionDb() const noexcept
    {
        return reductionDb_.load(std::memory_order_relaxed);
    }

private:
    static constexpr std::array<ParameterSpec, kParameterCount> kParameters{
        ParameterSpec{kFrequency, "Frequency", "Hz", 2000.0, 16000.0, 6000.0, false, "Detection"},
        ParameterSpec{kThreshold, "Threshold", "dB", -60.0, 0.0, -30.0, false, "Detection"},
        ParameterSpec{kListen, "Listen", "", 0.0, 1.0, 0.0, true, "Detection"},
        ParameterSpec{kRange, "Range", "dB", 0.0, 24.0, 6.0, false, "Reduction"},
        ParameterSpec{kMode, "Mode", "", 0.0, 1.0, 1.0, true, "Reduction"},
    };

    [[nodiscard]] double value(std::uint32_t id) const noexcept { return values_[id]; }
    void applyTargets(bool jump) noexcept;
    void updateFilter(bool force) noexcept;

    std::array<double, kParameterCount> values_ = defaults();
    std::array<BiquadState, kMaxChannels> states_{}; ///< detection
    std::array<BiquadState, kMaxChannels> shelfStates_{};
    BiquadCoefficients highPass_{};
    BiquadCoefficients shelf_{}; ///< the split-band cut; per control step
    SmoothedValue octave_; ///< log2 of the frequency; per control step
    SmoothedValue threshold_;
    SmoothedValue range_;
    SmoothedValue split_; ///< 0: wideband, 1: split-band
    SmoothedValue listen_;
    double envelope_ = 0.0; ///< the detected band's level (linear)
    double reduction_ = 0.0; ///< dB, now
    double frequency_ = 0.0; ///< Hz, now
    double attack_ = 0.0;
    double release_ = 0.0;
    double sampleRate_ = 0.0; ///< 0: not prepared, passes through
    int untilControl_ = 0;
    bool changed_ = false;
    std::atomic<float> reductionDb_{0.0f};

    [[nodiscard]] static constexpr std::array<double, kParameterCount> defaults() noexcept
    {
        std::array<double, kParameterCount> values{};
        for (std::size_t i = 0; i < kParameterCount; ++i)
            values[i] = kParameters[i].defaultValue;
        return values;
    }
};

static_assert(Effect<DeEsser>);

} // namespace vloom::dsp
