// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include "vloom/dsp/DelayLine.hpp"
#include "vloom/dsp/ParameterValues.hpp"
#include "vloom/dsp/Processor.hpp"
#include "vloom/dsp/SmoothedValue.hpp"
#include "vloom/dsp/Svf.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vloom::dsp {

/// "vloom.reverb": an algorithmic reverb, a feedback delay network of eight
/// lines (ADR 0004).
///
/// - Each input side goes through a pre-delay and four diffusing all-passes,
///   then into the network: eight delay lines mixed by a Hadamard matrix
///   (lossless), each line slowly modulated against metallic ringing.
/// - Size scales the line lengths; Decay is the time to fall by 60 dB
///   (RT60) at low frequencies; Damping makes high frequencies die sooner.
/// - Freeze holds the tail as it is (no input, no loss) until switched off.
/// - The reverb (not the dry signal) goes through a low cut and a high cut
///   (12 dB per octave), then Width and the dry/wet Mix.
///
/// A mono input gives a mono output (both reverb sides summed). No latency
/// (the pre-delay is part of the sound). Processes up to two channels;
/// others pass through.
class Reverb {
public:
    // Parameter ids: saved in projects, so they never change.
    enum Id : std::uint32_t {
        kPreDelay,
        kSize,
        kDecay,
        kDiffusion,
        kFreeze,
        kDamping,
        kLowCut,
        kHighCut,
        kWidth,
        kMix,
        kParameterCount
    };

    static constexpr int kLines = 8;
    static constexpr int kDiffusers = 4;
    static constexpr double kMaxPreDelaySeconds = 0.5;
    static constexpr double kRampSeconds = 0.05;
    /// The decay and the line lengths follow size and decay changes this often.
    static constexpr int kControlFrames = 32;

    [[nodiscard]] static ProcessorSpec spec() noexcept { return {"vloom.reverb", "Reverb", "1.0"}; }
    [[nodiscard]] static std::span<const ParameterSpec> parameters() noexcept { return kParameters; }

    void prepare(double sampleRate, int maxBlockFrames);
    void setParameter(std::uint32_t id, double value) noexcept;
    void process(float* const* channels, int numChannels, int numFrames) noexcept;
    void reset() noexcept;
    [[nodiscard]] int latencyFrames() const noexcept { return 0; }

private:
    static constexpr std::array<ParameterSpec, kParameterCount> kParameters{
        ParameterSpec{kPreDelay, "Pre-Delay", "ms", 0.0, kMaxPreDelaySeconds * 1000.0, 10.0, false, "Space"},
        ParameterSpec{kSize, "Size", "%", 0.0, 100.0, 50.0, false, "Space"},
        ParameterSpec{kDecay, "Decay", "s", 0.1, 20.0, 2.0, false, "Space"},
        ParameterSpec{kDiffusion, "Diffusion", "%", 0.0, 100.0, 70.0, false, "Space"},
        ParameterSpec{kFreeze, "Freeze", "", 0.0, 1.0, 0.0, true, "Space"},
        ParameterSpec{kDamping, "Damping", "%", 0.0, 100.0, 50.0, false, "Tone"},
        ParameterSpec{kLowCut, "Low Cut", "Hz", 20.0, 1000.0, 80.0, false, "Tone"},
        ParameterSpec{kHighCut, "High Cut", "Hz", 1000.0, 20000.0, 12000.0, false, "Tone"},
        ParameterSpec{kWidth, "Width", "%", 0.0, 100.0, 100.0, false, "Output"},
        ParameterSpec{kMix, "Mix", "%", 0.0, 100.0, 30.0, false, "Output"},
    };

    /// A Schroeder all-pass: diffuses without colouring.
    struct Diffuser {
        DelayLine line;
        int frames = 1;
    };
    struct Input {
        DelayLine preDelay;
        std::array<Diffuser, kDiffusers> diffusers{};
    };
    struct Line {
        DelayLine line;
        double frames = 1.0; ///< length without modulation
        double gain = 0.0; ///< loss per trip, for the decay time
        double damped = 0.0; ///< the low-pass memory
        double cos = 1.0; ///< modulation phasor
        double sin = 0.0;
        double turnCos = 1.0; ///< the phasor's rotation per frame
        double turnSin = 0.0;
    };

    [[nodiscard]] double value(std::uint32_t id) const noexcept { return values_[id]; }
    void applyValues(bool jump) noexcept;
    /// Works out lengths, losses and damping from the glides.
    void control() noexcept;
    [[nodiscard]] float diffuse(Input& input, float sample, double coefficient) noexcept;

    std::array<double, kParameterCount> values_ = defaultValues(kParameters);
    double sampleRate_ = 0.0; ///< 0: not prepared, passes through
    std::array<Input, 2> inputs_{};
    std::array<Line, kLines> lines_{};
    std::array<SvfState, 2> lowCut_{};
    std::array<SvfState, 2> highCut_{};
    SvfCoefficients lowCutCoefficients_{};
    SvfCoefficients highCutCoefficients_{};
    SmoothedValue preDelay_{0.0f}; ///< frames
    SmoothedValue size_{1.0f}; ///< line length factor; per control step
    SmoothedValue freeze_{0.0f}; ///< 0 .. 1; per control step
    SmoothedValue diffusion_{0.0f};
    SmoothedValue width_{1.0f};
    SmoothedValue mix_{0.0f};
    double damping_ = 0.0; ///< low-pass factor in the loop
    double modulationFrames_ = 0.0;
    int untilControl_ = 0;
    bool changed_ = true;
};

static_assert(Effect<Reverb>);

} // namespace vloom::dsp
