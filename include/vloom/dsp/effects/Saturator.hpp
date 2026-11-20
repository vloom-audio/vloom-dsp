// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include "vloom/dsp/DelayLine.hpp"
#include "vloom/dsp/Oversampler.hpp"
#include "vloom/dsp/ParameterValues.hpp"
#include "vloom/dsp/Processor.hpp"
#include "vloom/dsp/SmoothedValue.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vloom::dsp {

/// "vloom.saturator": waveshaping distortion.
///
/// - Drive (dB) pushes the signal into a curve: Soft (tanh), Tube
///   (asymmetric, so it adds even harmonics too), Tape (arctangent, the
///   gentlest) or Hard (clipping at +-1). Every curve has a slope of 1 at
///   zero, so quiet signals keep their level.
/// - The curve runs at 2x or 4x the sample rate (Oversampler), so the
///   harmonics it adds do not fold back as aliasing. This adds latency:
///   latencyFrames() (31 frames at 2x, 36 at 4x). Changing Oversampling
///   changes the latency, and the host must ask again then.
/// - A DC blocker after the curve, a tilt Tone (-100 %: darker, +100 %:
///   brighter, +-6 dB at the ends around 1 kHz), Output gain, and a dry/wet
///   Mix whose dry side is delayed to line up with the wet one.
///
/// Processes up to kMaxChannels channels; others pass through, undelayed.
class Saturator {
public:
    // Parameter ids: saved in projects, so they never change.
    enum Id : std::uint32_t { kDrive, kCurve, kTone, kOutput, kMix, kOversampling, kParameterCount };

    /// The value of kCurve.
    enum class Curve { Soft, Tube, Tape, Hard };
    /// The value of kOversampling.
    enum class Oversampling { TwoTimes, FourTimes };

    static constexpr int kMaxChannels = Oversampler::kMaxChannels;
    static constexpr double kRampSeconds = 0.02;
    static constexpr double kToneDb = 6.0; ///< the tilt at +-100 %, each side
    static constexpr double kToneHz = 1000.0;
    static constexpr double kDcBlockHz = 5.0;

    [[nodiscard]] static ProcessorSpec spec() noexcept { return {"vloom.saturator", "Saturator", "1.0"}; }
    [[nodiscard]] static std::span<const ParameterSpec> parameters() noexcept { return kParameters; }

    void prepare(double sampleRate, int maxBlockFrames);
    void setParameter(std::uint32_t id, double value) noexcept;
    void process(float* const* channels, int numChannels, int numFrames) noexcept;
    void reset() noexcept;
    [[nodiscard]] int latencyFrames() const noexcept { return oversampler_.latencyFrames(); }

    /// The curve itself, without drive or oversampling.
    [[nodiscard]] static float shape(Curve curve, float x) noexcept;

private:
    static constexpr std::array<ParameterSpec, kParameterCount> kParameters{
        ParameterSpec{kDrive, "Drive", "dB", 0.0, 36.0, 6.0, false, "Drive"},
        ParameterSpec{kCurve, "Curve", "", 0.0, 3.0, 0.0, true, "Drive"},
        ParameterSpec{kTone, "Tone", "%", -100.0, 100.0, 0.0, false, "Tone"},
        ParameterSpec{kOutput, "Output", "dB", -24.0, 12.0, 0.0, false, "Output"},
        ParameterSpec{kMix, "Mix", "%", 0.0, 100.0, 100.0, false, "Output"},
        ParameterSpec{kOversampling, "Oversampling", "", 0.0, 1.0, 1.0, true, "Quality"},
    };

    struct Channel {
        DelayLine dry;
        double dcIn = 0.0; ///< DC blocker memory
        double dcOut = 0.0;
        double low = 0.0; ///< tone: the low-pass memory
    };

    [[nodiscard]] double value(std::uint32_t id) const noexcept { return values_[id]; }
    void applyValues(bool jump) noexcept;

    std::array<double, kParameterCount> values_ = defaultValues(kParameters);
    double sampleRate_ = 0.0; ///< 0: not prepared, passes through
    Oversampler oversampler_;
    std::array<Channel, kMaxChannels> channels_{};
    double dcFactor_ = 0.0;
    double toneFactor_ = 0.0;
    SmoothedValue drive_{1.0f};
    SmoothedValue lowGain_{1.0f};
    SmoothedValue highGain_{1.0f};
    SmoothedValue output_{1.0f};
    SmoothedValue mix_{1.0f};
};

static_assert(Effect<Saturator>);

} // namespace vloom::dsp
