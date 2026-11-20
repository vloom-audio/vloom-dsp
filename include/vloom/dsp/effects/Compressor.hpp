// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include "vloom/dsp/ParameterValues.hpp"
#include "vloom/dsp/Processor.hpp"
#include "vloom/dsp/SharedValue.hpp"
#include "vloom/dsp/SidechainFilter.hpp"
#include "vloom/dsp/SmoothedValue.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vloom::dsp {

/// "vloom.compressor": a feed-forward compressor.
///
/// - Static curve: threshold, ratio and a soft knee (staticGainDb()).
/// - Detector: peak or RMS (10 ms), per channel; stereo link blends each
///   channel's level towards the louder one (100 %: both get the same gain).
///   The gain reduction moves at the attack and release times (in dB).
/// - Sidechain (SidechainEffect): processWithSidechain() keys the detector
///   off an external signal; plain process() keys it off the input. Either
///   key can go through a high-pass filter first, and Listen plays the key
///   instead of the output, to set that filter up.
/// - Output: makeup gain (auto makeup adds half the reduction at 0 dBFS)
///   and a dry/wet mix for parallel compression.
///
/// At a ratio of 1:1, 0 dB makeup and 100 % mix it passes its input through
/// unchanged. No latency. Processes up to kMaxChannels channels; others pass
/// through.
class Compressor {
public:
    // Parameter ids: saved in projects, so they never change.
    enum Id : std::uint32_t {
        kThreshold,
        kRatio,
        kAttack,
        kRelease,
        kKnee,
        kDetector,
        kStereoLink,
        kSidechainFilter,
        kSidechainFrequency,
        kSidechainListen,
        kMakeup,
        kAutoMakeup,
        kMix,
        kParameterCount
    };

    /// The value of kDetector.
    enum class Detector { Peak, Rms };

    static constexpr int kMaxChannels = 2;
    static constexpr double kRmsSeconds = 0.01;
    static constexpr double kRampSeconds = 0.02;

    [[nodiscard]] static ProcessorSpec spec() noexcept { return {"vloom.compressor", "Compressor", "1.0"}; }
    [[nodiscard]] static std::span<const ParameterSpec> parameters() noexcept { return kParameters; }

    void prepare(double sampleRate, int maxBlockFrames);
    void setParameter(std::uint32_t id, double value) noexcept;
    void process(float* const* channels, int numChannels, int numFrames) noexcept;
    /// process() with the detector listening to `key` (SidechainEffect).
    void processWithSidechain(float* const* channels, const float* const* key, int numChannels,
        int numKeyChannels, int numFrames) noexcept;
    void reset() noexcept;
    [[nodiscard]] int latencyFrames() const noexcept { return 0; }

    /// The most the gain was turned down during the last block, in dB (>= 0),
    /// for a meter. Safe to call from any thread.
    [[nodiscard]] float gainReductionDb() const noexcept { return gainReduction_.load(); }

    /// The gain change of the static curve for a level of `inputDb`, in dB
    /// (<= 0): `thresholdDb`, `ratio` (>= 1) and a knee `kneeDb` wide.
    [[nodiscard]] static double staticGainDb(
        double inputDb, double thresholdDb, double ratio, double kneeDb) noexcept;
    /// The output level for a steady input level of `inputDb` with the
    /// current settings, makeup included: what a curve display draws.
    [[nodiscard]] double transferDb(double inputDb) const noexcept;

private:
    static constexpr std::array<ParameterSpec, kParameterCount> kParameters{
        ParameterSpec{kThreshold, "Threshold", "dB", -60.0, 0.0, -18.0, false, "Dynamics"},
        ParameterSpec{kRatio, "Ratio", ":1", 1.0, 20.0, 4.0, false, "Dynamics"},
        ParameterSpec{kAttack, "Attack", "ms", 0.05, 250.0, 10.0, false, "Dynamics"},
        ParameterSpec{kRelease, "Release", "ms", 5.0, 2500.0, 100.0, false, "Dynamics"},
        ParameterSpec{kKnee, "Knee", "dB", 0.0, 24.0, 6.0, false, "Dynamics"},
        ParameterSpec{kDetector, "Detector", "", 0.0, 1.0, 0.0, true, "Detector"},
        ParameterSpec{kStereoLink, "Stereo Link", "%", 0.0, 100.0, 100.0, false, "Detector"},
        ParameterSpec{kSidechainFilter, "Sidechain Filter", "", 0.0, 1.0, 0.0, true, "Sidechain"},
        ParameterSpec{
            kSidechainFrequency, "Sidechain Frequency", "Hz", 20.0, 500.0, 100.0, false, "Sidechain"},
        ParameterSpec{kSidechainListen, "Sidechain Listen", "", 0.0, 1.0, 0.0, true, "Sidechain"},
        ParameterSpec{kMakeup, "Makeup", "dB", -12.0, 24.0, 0.0, false, "Output"},
        ParameterSpec{kAutoMakeup, "Auto Makeup", "", 0.0, 1.0, 0.0, true, "Output"},
        ParameterSpec{kMix, "Mix", "%", 0.0, 100.0, 100.0, false, "Output"},
    };

    [[nodiscard]] double value(std::uint32_t id) const noexcept { return values_[id]; }
    [[nodiscard]] double makeupDb() const noexcept;
    void applyValues(bool jump) noexcept;
    void run(float* const* channels, const float* const* key, int numChannels, int numKeyChannels,
        int numFrames) noexcept;

    std::array<double, kParameterCount> values_ = defaultValues(kParameters);
    double sampleRate_ = 0.0; ///< 0: not prepared, passes through
    double attackFactor_ = 0.0;
    double releaseFactor_ = 0.0;
    double rmsFactor_ = 0.0;
    double kneeStartGain_ = 0.0; ///< below this level (linear) there is no gain change
    SidechainFilter sidechain_;
    SmoothedValue makeup_{0.0f}; ///< dB
    SmoothedValue mix_{1.0f};
    std::array<double, kMaxChannels> meanSquare_{};
    std::array<double, kMaxChannels> reductionDb_{}; ///< the smoothed gain change, <= 0
    SharedValue<float> gainReduction_{0.0f};
};

static_assert(SidechainEffect<Compressor>);

} // namespace vloom::dsp
