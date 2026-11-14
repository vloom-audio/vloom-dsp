// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include "vloom/dsp/Processor.hpp"
#include "vloom/dsp/SmoothedValue.hpp"

#include <array>

namespace vloom::dsp {

/// "vloom.gain": turns the level up or down by kGain dB (-60 .. +12; at -60
/// it is silent). Changes glide over kRampSeconds, so they do not click.
class Gain {
public:
    static constexpr std::uint32_t kGain = 0; ///< the parameter id
    static constexpr double kMinDb = -60.0;
    static constexpr double kMaxDb = 12.0;
    static constexpr double kRampSeconds = 0.005;

    [[nodiscard]] static ProcessorSpec spec() noexcept { return {"vloom.gain", "Gain", "1.0"}; }
    [[nodiscard]] static std::span<const ParameterSpec> parameters() noexcept { return kParameters; }

    void prepare(double sampleRate, int maxBlockFrames);
    void setParameter(std::uint32_t id, double value) noexcept;
    void process(float* const* channels, int numChannels, int numFrames) noexcept;
    void reset() noexcept;
    [[nodiscard]] int latencyFrames() const noexcept { return 0; }

    /// The gain as a factor for a level in dB (0 at kMinDb and below).
    [[nodiscard]] static float linear(double db) noexcept;

private:
    static constexpr std::array<ParameterSpec, 1> kParameters{
        ParameterSpec{kGain, "Gain", "dB", kMinDb, kMaxDb, 0.0, false}};

    double db_ = 0.0;
    SmoothedValue gain_{1.0f};
};

static_assert(Effect<Gain>);

} // namespace vloom::dsp
