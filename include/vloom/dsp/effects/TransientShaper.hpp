// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include "vloom/dsp/Processor.hpp"
#include "vloom/dsp/SmoothedValue.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vloom::dsp {

/// "vloom.transient": makes the start of each sound (attack) and what rings
/// on after it (sustain) louder or quieter, whatever the level.
///
/// Envelope-difference design, on the peak level of all channels together:
/// - Attack: a fast envelope minus one with a slower attack, in dB, is above
///   0 while a sound starts. Attack (-100 .. +100 %) turns it into a boost or
///   a cut of up to that many dB.
/// - Sustain: an envelope with a slower release minus the fast one, in dB, is
///   above 0 while a sound dies away; Sustain does the same with it.
/// - Speed sets how long counts as "the start" and how soon "the tail"
///   begins: at 0 % the times below, at 100 % kSpeedRange times shorter.
///   Fast suits clicks, slow a fuller punch.
/// - Each part is capped at kMaxShapeDb, the sum at +-kMaxShapeDb.
///
/// Working in dB, the result does not depend on the input level. Output
/// sets the level after shaping, Mix blends it with the input (Mix 100 %,
/// Attack and Sustain at 0 and Output at 0 dB pass the input through bit
/// for bit). Processes the first kMaxChannels channels, linked; any others
/// pass through. No latency.
class TransientShaper {
public:
    // Parameter ids: saved in projects, so they never change.
    static constexpr std::uint32_t kAttack = 0;
    static constexpr std::uint32_t kSustain = 1;
    static constexpr std::uint32_t kSpeed = 2;
    static constexpr std::uint32_t kOutput = 3;
    static constexpr std::uint32_t kMix = 4;
    static constexpr std::size_t kParameterCount = 5;

    static constexpr int kMaxChannels = 8;
    static constexpr double kMaxShapeDb = 18.0;
    static constexpr double kFastAttackSeconds = 0.0002;
    /// The slow attack and the releases at Speed 0 %; at 100 % they are
    /// kSpeedRange times shorter.
    static constexpr double kSlowAttackSeconds = 0.04;
    static constexpr double kFastReleaseSeconds = 0.08;
    static constexpr double kSlowReleaseSeconds = 0.6;
    static constexpr double kSpeedRange = 8.0;
    static constexpr double kRampSeconds = 0.02;

    [[nodiscard]] static ProcessorSpec spec() noexcept
    {
        return {"vloom.transient", "Transient Shaper", "1.0"};
    }
    [[nodiscard]] static std::span<const ParameterSpec> parameters() noexcept { return kParameters; }

    void prepare(double sampleRate, int maxBlockFrames);
    void setParameter(std::uint32_t id, double value) noexcept;
    void process(float* const* channels, int numChannels, int numFrames) noexcept;
    void reset() noexcept;
    [[nodiscard]] int latencyFrames() const noexcept { return 0; }

private:
    static constexpr std::array<ParameterSpec, kParameterCount> kParameters{
        ParameterSpec{kAttack, "Attack", "%", -100.0, 100.0, 0.0, false, "Shape"},
        ParameterSpec{kSustain, "Sustain", "%", -100.0, 100.0, 0.0, false, "Shape"},
        ParameterSpec{kSpeed, "Speed", "%", 0.0, 100.0, 50.0, false, "Shape"},
        ParameterSpec{kOutput, "Output", "dB", -24.0, 24.0, 0.0, false, "Output"},
        ParameterSpec{kMix, "Mix", "%", 0.0, 100.0, 100.0, false, "Output"},
    };

    [[nodiscard]] double value(std::uint32_t id) const noexcept { return values_[id]; }
    void applyTargets(bool jump) noexcept;
    void updateTimes() noexcept;

    /// One peak envelope follower (linear level).
    struct Follower {
        double level = 0.0;
        double attack = 0.0;
        double release = 0.0;
        double next(double in) noexcept
        {
            level += (in > level ? attack : release) * (in - level);
            if (level < 1.0e-12)
                level = 0.0;
            return level;
        }
    };

    std::array<double, kParameterCount> values_ = defaults();
    Follower fast_; ///< fast attack, fast release
    Follower slowAttack_; ///< slow attack, fast release
    Follower slowRelease_; ///< fast attack, slow release
    SmoothedValue attack_;
    SmoothedValue sustain_;
    SmoothedValue output_;
    SmoothedValue mix_;
    double sampleRate_ = 0.0; ///< 0: not prepared, passes through

    [[nodiscard]] static constexpr std::array<double, kParameterCount> defaults() noexcept
    {
        std::array<double, kParameterCount> values{};
        for (std::size_t i = 0; i < kParameterCount; ++i)
            values[i] = kParameters[i].defaultValue;
        return values;
    }
};

static_assert(Effect<TransientShaper>);

} // namespace vloom::dsp
