// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include "vloom/dsp/Biquad.hpp"
#include "vloom/dsp/Processor.hpp"
#include "vloom/dsp/SmoothedValue.hpp"
#include "vloom/dsp/effects/detail/Modulation.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vloom::dsp {

/// "vloom.bitcrusher": fewer bits and a lower sample rate, on purpose.
///
/// - Drive turns the input up before crushing.
/// - Bit Depth (1 .. 24, fractional values too): the signal is clipped to
///   -1 .. 1 and rounded to steps of 2 / 2^bits (a mid-tread quantizer: at
///   1 bit, -1, 0 and 1). At 24 bits it is left alone.
/// - Downsample (1 .. 64, fractional too): each sample is held for that many
///   frames. Jitter makes each hold up to half as long or half again as long,
///   at random. Anti-Alias puts a 24 dB per octave low-pass at the reduced
///   rate's Nyquist frequency before the hold (as a real converter would).
/// - Dither adds triangular noise of up to one step before rounding.
/// - Mix blends with the input, then Output sets the level.
///
/// At its defaults (24 bits, Downsample 1, Mix 100 %) and at Mix 0 it passes
/// the input through bit for bit. Processes the first kMaxChannels channels;
/// any others pass through. No latency.
class Bitcrusher {
public:
    // Parameter ids: saved in projects, so they never change.
    static constexpr std::uint32_t kDrive = 0;
    static constexpr std::uint32_t kBits = 1;
    static constexpr std::uint32_t kDownsample = 2;
    static constexpr std::uint32_t kJitter = 3;
    static constexpr std::uint32_t kDither = 4;
    static constexpr std::uint32_t kAntiAlias = 5;
    static constexpr std::uint32_t kOutput = 6;
    static constexpr std::uint32_t kMix = 7;
    static constexpr std::size_t kParameterCount = 8;

    static constexpr int kMaxChannels = 8;
    static constexpr double kMinBits = 1.0;
    static constexpr double kMaxBits = 24.0;
    static constexpr double kMaxDownsample = 64.0;
    static constexpr double kRampSeconds = 0.02;
    /// The anti-alias filter is worked out once per this many frames.
    static constexpr int kControlFrames = 16;

    [[nodiscard]] static ProcessorSpec spec() noexcept { return {"vloom.bitcrusher", "Bitcrusher", "1.0"}; }
    [[nodiscard]] static std::span<const ParameterSpec> parameters() noexcept { return kParameters; }

    void prepare(double sampleRate, int maxBlockFrames);
    void setParameter(std::uint32_t id, double value) noexcept;
    void process(float* const* channels, int numChannels, int numFrames) noexcept;
    void reset() noexcept;
    [[nodiscard]] int latencyFrames() const noexcept { return 0; }

    /// The quantizer's step for `bits`: 2 / 2^bits.
    [[nodiscard]] static double stepFor(double bits) noexcept;

private:
    static constexpr std::array<ParameterSpec, kParameterCount> kParameters{
        ParameterSpec{kDrive, "Drive", "dB", 0.0, 24.0, 0.0, false, "Input"},
        ParameterSpec{kBits, "Bit Depth", "bits", kMinBits, kMaxBits, kMaxBits, false, "Crush"},
        ParameterSpec{kDownsample, "Downsample", "x", 1.0, kMaxDownsample, 1.0, false, "Crush"},
        ParameterSpec{kJitter, "Jitter", "%", 0.0, 100.0, 0.0, false, "Crush"},
        ParameterSpec{kDither, "Dither", "%", 0.0, 100.0, 0.0, false, "Crush"},
        ParameterSpec{kAntiAlias, "Anti-Alias", "", 0.0, 1.0, 0.0, true, "Crush"},
        ParameterSpec{kOutput, "Output", "dB", -24.0, 12.0, 0.0, false, "Output"},
        ParameterSpec{kMix, "Mix", "%", 0.0, 100.0, 100.0, false, "Output"},
    };

    struct Channel {
        std::array<BiquadState, 2> filter{};
        float held = 0.0f;
    };

    [[nodiscard]] double value(std::uint32_t id) const noexcept { return values_[id]; }
    void applyTargets(bool jump) noexcept;
    void updateFilter() noexcept;

    std::array<double, kParameterCount> values_ = defaults();
    std::array<Channel, kMaxChannels> channels_{};
    std::array<BiquadCoefficients, 2> filter_{};
    SmoothedValue drive_;
    SmoothedValue bits_;
    SmoothedValue downsample_;
    SmoothedValue jitter_;
    SmoothedValue dither_;
    SmoothedValue antiAlias_; ///< 0: off, 1: on
    SmoothedValue output_;
    SmoothedValue mix_;
    detail::Random random_;
    double hold_ = 0.0; ///< frames since the last new sample
    double holdFrames_ = 1.0; ///< how long the current sample is held
    double sampleRate_ = 0.0; ///< 0: not prepared, passes through
    int untilControl_ = 0;

    [[nodiscard]] static constexpr std::array<double, kParameterCount> defaults() noexcept
    {
        std::array<double, kParameterCount> values{};
        for (std::size_t i = 0; i < kParameterCount; ++i)
            values[i] = kParameters[i].defaultValue;
        return values;
    }
};

static_assert(Effect<Bitcrusher>);

} // namespace vloom::dsp
