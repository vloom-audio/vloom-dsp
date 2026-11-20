// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include "vloom/dsp/ParameterValues.hpp"
#include "vloom/dsp/Processor.hpp"
#include "vloom/dsp/SmoothedValue.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vloom::dsp {

/// "vloom.utility": the small fixes a mix needs.
///
/// In this order: a DC blocker (5 Hz high-pass), swapping left and right,
/// inverting the polarity of either side, stereo width (mid/side: 0 % is
/// mono, 200 % doubles the side signal), Mono (width 0 % whatever Width
/// says), gain (-60 .. +24 dB; silent at -60) and pan (a balance control:
/// the far side is turned down, constant power, the near side stays).
///
/// Every switch fades over kRampSeconds, so nothing clicks. At the defaults
/// it passes its input through bit for bit. On a mono input (one channel)
/// only the DC blocker, Invert Left and the gain work. No latency.
class Utility {
public:
    // Parameter ids: saved in projects, so they never change.
    enum Id : std::uint32_t {
        kGain,
        kPan,
        kWidth,
        kMono,
        kInvertLeft,
        kInvertRight,
        kSwap,
        kDcBlock,
        kParameterCount
    };

    static constexpr double kMinGainDb = -60.0;
    static constexpr double kRampSeconds = 0.01;
    static constexpr double kDcBlockHz = 5.0;

    [[nodiscard]] static ProcessorSpec spec() noexcept { return {"vloom.utility", "Utility", "1.0"}; }
    [[nodiscard]] static std::span<const ParameterSpec> parameters() noexcept { return kParameters; }

    void prepare(double sampleRate, int maxBlockFrames);
    void setParameter(std::uint32_t id, double value) noexcept;
    void process(float* const* channels, int numChannels, int numFrames) noexcept;
    void reset() noexcept;
    [[nodiscard]] int latencyFrames() const noexcept { return 0; }

private:
    static constexpr std::array<ParameterSpec, kParameterCount> kParameters{
        ParameterSpec{kGain, "Gain", "dB", kMinGainDb, 24.0, 0.0, false, "Gain"},
        ParameterSpec{kPan, "Pan", "%", -100.0, 100.0, 0.0, false, "Gain"},
        ParameterSpec{kWidth, "Width", "%", 0.0, 200.0, 100.0, false, "Stereo"},
        ParameterSpec{kMono, "Mono", "", 0.0, 1.0, 0.0, true, "Stereo"},
        ParameterSpec{kInvertLeft, "Invert Left", "", 0.0, 1.0, 0.0, true, "Channels"},
        ParameterSpec{kInvertRight, "Invert Right", "", 0.0, 1.0, 0.0, true, "Channels"},
        ParameterSpec{kSwap, "Swap", "", 0.0, 1.0, 0.0, true, "Channels"},
        ParameterSpec{kDcBlock, "DC Block", "", 0.0, 1.0, 0.0, true, "Channels"},
    };

    /// The settings that glide, in processing order.
    enum Glide : std::size_t {
        kDcMix,
        kSwapMix,
        kSignLeft,
        kSignRight,
        kWidthGlide,
        kGainGlide,
        kPanLeft,
        kPanRight,
        kGlides
    };

    [[nodiscard]] double value(std::uint32_t id) const noexcept { return values_[id]; }
    void applyValues(bool jump) noexcept;
    [[nodiscard]] std::array<float, kGlides> targets() const noexcept;

    std::array<double, kParameterCount> values_ = defaultValues(kParameters);
    double sampleRate_ = 0.0; ///< 0: not prepared, passes through
    double dcFactor_ = 0.0;
    std::array<SmoothedValue, kGlides> glides_;
    std::array<double, 2> dcIn_{};
    std::array<double, 2> dcOut_{};
};

static_assert(Effect<Utility>);

} // namespace vloom::dsp
