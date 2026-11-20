// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include "vloom/dsp/ParameterValues.hpp"
#include "vloom/dsp/Processor.hpp"
#include "vloom/dsp/SmoothedValue.hpp"
#include "vloom/dsp/Svf.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vloom::dsp {

/// "vloom.filter": a resonant multimode filter, built from state variable
/// filters (Svf.hpp) so cutoff and resonance can move every sample, from
/// automation or modulation, without zipper noise or blowing up.
///
/// - Type: low-pass, high-pass, band-pass (0 dB at the cutoff) or notch.
/// - Slope: 12 dB per octave (one section, Q from the resonance) or 24 dB
///   (two sections: a Butterworth pair at no resonance, the second one's Q
///   raised with it). At no resonance both are -3 dB at the cutoff.
/// - Resonance: 0 .. 100 % gives Q kMinQ .. kMaxQ (exponentially).
/// - Drive: soft saturation (tanh) before the filter, faded in over the
///   first 6 dB so 0 dB is exactly linear. Not oversampled.
///
/// Type and slope changes cross-fade, cutoff glides in octaves and
/// resonance glides, all over kRampSeconds. No latency. Processes up to
/// kMaxChannels channels; others pass through.
class Filter {
public:
    // Parameter ids: saved in projects, so they never change.
    enum Id : std::uint32_t { kType, kCutoff, kResonance, kSlope, kDrive, kParameterCount };

    /// The value of kType.
    enum class Type { LowPass, HighPass, BandPass, Notch };
    /// The value of kSlope.
    enum class Slope { Db12, Db24 };

    static constexpr int kMaxChannels = 2;
    static constexpr double kMinQ = 0.7071067811865476;
    static constexpr double kMaxQ = 25.0;
    static constexpr double kRampSeconds = 0.02;

    [[nodiscard]] static ProcessorSpec spec() noexcept { return {"vloom.filter", "Filter", "1.0"}; }
    [[nodiscard]] static std::span<const ParameterSpec> parameters() noexcept { return kParameters; }

    void prepare(double sampleRate, int maxBlockFrames);
    void setParameter(std::uint32_t id, double value) noexcept;
    void process(float* const* channels, int numChannels, int numFrames) noexcept;
    void reset() noexcept;
    [[nodiscard]] int latencyFrames() const noexcept { return 0; }

    /// The gain of the filter at `frequency` (Hz), in dB, for the settings
    /// it is gliding to, drive left out: what a response curve draws. 0
    /// before prepare(). Not for the audio thread.
    [[nodiscard]] double magnitudeDb(double frequency) const;

private:
    static constexpr std::array<ParameterSpec, kParameterCount> kParameters{
        ParameterSpec{kType, "Type", "", 0.0, 3.0, 0.0, true, "Filter"},
        ParameterSpec{kCutoff, "Cutoff", "Hz", 20.0, 20000.0, 1000.0, false, "Filter"},
        ParameterSpec{kResonance, "Resonance", "%", 0.0, 100.0, 0.0, false, "Filter"},
        ParameterSpec{kSlope, "Slope", "", 0.0, 1.0, 0.0, true, "Filter"},
        ParameterSpec{kDrive, "Drive", "dB", 0.0, 24.0, 0.0, false, "Drive"},
    };

    /// The filter's settings for one sample.
    struct Design {
        SvfCoefficients single; ///< 12 dB
        SvfCoefficients first; ///< 24 dB, first section
        SvfCoefficients second;
    };

    /// How much of each output of a section a type takes.
    enum Glide : std::size_t { kOctave, kQ, kLow, kBand, kHigh, kSteep, kDriveAmount, kDriveGain, kGlides };

    struct Channel {
        SvfState single;
        SvfState first;
        SvfState second;
    };

    [[nodiscard]] double value(std::uint32_t id) const noexcept { return values_[id]; }
    void applyValues(bool jump) noexcept;
    [[nodiscard]] Design design(double frequency, double q) const noexcept;

    std::array<double, kParameterCount> values_ = defaultValues(kParameters);
    double sampleRate_ = 0.0; ///< 0: not prepared, passes through
    std::array<SmoothedValue, kGlides> glides_;
    std::array<Channel, kMaxChannels> channels_{};
    Design design_{};
};

static_assert(Effect<Filter>);

} // namespace vloom::dsp
