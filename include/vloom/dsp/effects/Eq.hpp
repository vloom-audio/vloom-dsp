// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include "vloom/dsp/Biquad.hpp"
#include "vloom/dsp/Processor.hpp"
#include "vloom/dsp/SmoothedValue.hpp"

#include <array>
#include <cstddef>

namespace vloom::dsp {

/// "vloom.eq": a parametric equalizer with six sections in series.
///
/// - Low cut and high cut: 12 dB per octave, each with a switch.
/// - Low shelf and high shelf: frequency and gain.
/// - Low mid and high mid bells: frequency, gain and Q.
///
/// A section at 0 dB, or a cut that is off, is skipped, so the default
/// settings pass the input through bit for bit. Changes glide over
/// kRampSeconds (a cut fades in and out), so they do not click. No latency.
///
/// Processes the first kMaxChannels channels; any others pass through.
class Eq {
public:
    // Parameter ids: saved in projects, so they never change.
    static constexpr std::uint32_t kLowCutOn = 0;
    static constexpr std::uint32_t kLowCutFrequency = 1;
    static constexpr std::uint32_t kLowShelfFrequency = 2;
    static constexpr std::uint32_t kLowShelfGain = 3;
    static constexpr std::uint32_t kLowMidFrequency = 4;
    static constexpr std::uint32_t kLowMidGain = 5;
    static constexpr std::uint32_t kLowMidQ = 6;
    static constexpr std::uint32_t kHighMidFrequency = 7;
    static constexpr std::uint32_t kHighMidGain = 8;
    static constexpr std::uint32_t kHighMidQ = 9;
    static constexpr std::uint32_t kHighShelfFrequency = 10;
    static constexpr std::uint32_t kHighShelfGain = 11;
    static constexpr std::uint32_t kHighCutOn = 12;
    static constexpr std::uint32_t kHighCutFrequency = 13;
    static constexpr std::size_t kParameterCount = 14;

    static constexpr int kMaxChannels = 8;
    static constexpr double kMaxGainDb = 18.0; ///< every gain: -18 .. +18 dB
    static constexpr double kMinQ = 0.1;
    static constexpr double kMaxQ = 18.0;
    static constexpr double kRampSeconds = 0.02;
    /// Moving settings are worked out once per this many frames.
    static constexpr int kControlFrames = 16;

    [[nodiscard]] static ProcessorSpec spec() noexcept { return {"vloom.eq", "EQ", "1.0"}; }
    [[nodiscard]] static std::span<const ParameterSpec> parameters() noexcept { return kParameters; }

    void prepare(double sampleRate, int maxBlockFrames);
    void setParameter(std::uint32_t id, double value) noexcept;
    void process(float* const* channels, int numChannels, int numFrames) noexcept;
    void reset() noexcept;
    [[nodiscard]] int latencyFrames() const noexcept { return 0; }

    /// The gain of the whole equalizer at `frequency` (Hz), in dB, for the
    /// settings it is gliding to: what a response curve draws. 0 before
    /// prepare(). Not for the audio thread.
    [[nodiscard]] double magnitudeDb(double frequency) const;

private:
    static constexpr std::array<ParameterSpec, kParameterCount> kParameters{
        ParameterSpec{kLowCutOn, "Low Cut", "", 0.0, 1.0, 0.0, true, "Low Cut"},
        ParameterSpec{kLowCutFrequency, "Low Cut Frequency", "Hz", 20.0, 1000.0, 80.0, false, "Low Cut"},
        ParameterSpec{
            kLowShelfFrequency, "Low Shelf Frequency", "Hz", 20.0, 2000.0, 120.0, false, "Low Shelf"},
        ParameterSpec{
            kLowShelfGain, "Low Shelf Gain", "dB", -kMaxGainDb, kMaxGainDb, 0.0, false, "Low Shelf"},
        ParameterSpec{kLowMidFrequency, "Low Mid Frequency", "Hz", 20.0, 20000.0, 400.0, false, "Low Mid"},
        ParameterSpec{kLowMidGain, "Low Mid Gain", "dB", -kMaxGainDb, kMaxGainDb, 0.0, false, "Low Mid"},
        ParameterSpec{kLowMidQ, "Low Mid Q", "", kMinQ, kMaxQ, 1.0, false, "Low Mid"},
        ParameterSpec{
            kHighMidFrequency, "High Mid Frequency", "Hz", 20.0, 20000.0, 2500.0, false, "High Mid"},
        ParameterSpec{kHighMidGain, "High Mid Gain", "dB", -kMaxGainDb, kMaxGainDb, 0.0, false, "High Mid"},
        ParameterSpec{kHighMidQ, "High Mid Q", "", kMinQ, kMaxQ, 1.0, false, "High Mid"},
        ParameterSpec{
            kHighShelfFrequency, "High Shelf Frequency", "Hz", 1000.0, 20000.0, 8000.0, false, "High Shelf"},
        ParameterSpec{
            kHighShelfGain, "High Shelf Gain", "dB", -kMaxGainDb, kMaxGainDb, 0.0, false, "High Shelf"},
        ParameterSpec{kHighCutOn, "High Cut", "", 0.0, 1.0, 0.0, true, "High Cut"},
        ParameterSpec{
            kHighCutFrequency, "High Cut Frequency", "Hz", 1000.0, 20000.0, 12000.0, false, "High Cut"},
    };

    using States = std::array<BiquadState, kMaxChannels>;

    enum class Shape { LowShelf, Bell, HighShelf };

    /// A shelf or a bell. Its frequency glides in octaves (log2 Hz).
    struct Band {
        Shape shape = Shape::Bell;
        std::uint32_t frequencyId = 0;
        std::uint32_t gainId = 0;
        std::uint32_t qId = kLowMidQ; ///< shelves: unused
        SmoothedValue octave{0.0f};
        SmoothedValue gain{0.0f};
        SmoothedValue q{1.0f};
        BiquadCoefficients coefficients{};
        States states{};
        bool active = false; ///< not at 0 dB: in the signal path
    };

    /// A low or high cut. Switching it fades between the input and the filter.
    struct Cut {
        bool highPass = true;
        std::uint32_t onId = 0;
        std::uint32_t frequencyId = 0;
        SmoothedValue octave{0.0f};
        SmoothedValue mix{0.0f}; ///< 0: out, 1: in; per frame
        BiquadCoefficients coefficients{};
        States states{};
        bool active = false;
    };

    [[nodiscard]] double value(std::uint32_t id) const noexcept { return values_[id]; }
    [[nodiscard]] BiquadCoefficients design(
        const Band& band, double frequency, double gain, double q) const noexcept;
    [[nodiscard]] BiquadCoefficients design(const Cut& cut, double frequency) const noexcept;
    /// Sends the values to the glides; `jump`: no glide.
    void applyTargets(bool jump) noexcept;
    /// One control step: moves the glides and the filters that follow them.
    void advance() noexcept;
    void update(Band& band, bool moved) noexcept;
    void update(Cut& cut, bool moved) noexcept;
    void run(Band& band, float* const* channels, int numChannels, int start, int count) noexcept;
    void run(Cut& cut, float* const* channels, int numChannels, int start, int count) noexcept;

    std::array<double, kParameterCount> values_ = defaults();
    std::array<Band, 4> bands_{
        Band{.shape = Shape::LowShelf, .frequencyId = kLowShelfFrequency, .gainId = kLowShelfGain},
        Band{.shape = Shape::Bell, .frequencyId = kLowMidFrequency, .gainId = kLowMidGain, .qId = kLowMidQ},
        Band{
            .shape = Shape::Bell, .frequencyId = kHighMidFrequency, .gainId = kHighMidGain, .qId = kHighMidQ},
        Band{.shape = Shape::HighShelf, .frequencyId = kHighShelfFrequency, .gainId = kHighShelfGain},
    };
    std::array<Cut, 2> cuts_{
        Cut{.highPass = true, .onId = kLowCutOn, .frequencyId = kLowCutFrequency},
        Cut{.highPass = false, .onId = kHighCutOn, .frequencyId = kHighCutFrequency},
    };
    double sampleRate_ = 0.0; ///< 0: not prepared, passes through
    int untilControl_ = 0; ///< frames until the next control step
    bool changed_ = false; ///< a value was set since the last control step

    [[nodiscard]] static constexpr std::array<double, kParameterCount> defaults() noexcept
    {
        std::array<double, kParameterCount> values{};
        for (std::size_t i = 0; i < kParameterCount; ++i)
            values[i] = kParameters[i].defaultValue;
        return values;
    }
};

static_assert(Effect<Eq>);

} // namespace vloom::dsp
