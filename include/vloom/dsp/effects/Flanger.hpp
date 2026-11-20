// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include "vloom/dsp/NoteValue.hpp"
#include "vloom/dsp/Processor.hpp"
#include "vloom/dsp/SmoothedValue.hpp"
#include "vloom/dsp/effects/detail/Modulation.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vloom::dsp {

/// "vloom.flanger": the input mixed with a copy delayed by a short time that
/// a sine LFO sweeps, which moves a comb of notches up and down.
///
/// - The delay sweeps from Delay up to Delay + Depth x kMaxSwingMs.
/// - Feedback (-95 .. +95 %) sends the delayed signal back into the line:
///   positive values sharpen the peaks, negative ones move them by half a
///   step and give a hollower sound.
/// - Stereo Phase offsets the right channel's LFO (0 .. 180 degrees).
/// - The LFO runs free in Hz or, with Sync on, at the length of a note at the
///   tempo setTempo() gives (kDefaultTempo before that). It does not follow
///   the host's transport position.
///
/// At Mix 50 % the notches are deepest; Mix 0 passes the input through bit
/// for bit. Processes the first kMaxChannels channels; any others pass
/// through. No latency.
class Flanger {
public:
    // Parameter ids: saved in projects, so they never change.
    static constexpr std::uint32_t kRate = 0;
    static constexpr std::uint32_t kSync = 1;
    static constexpr std::uint32_t kNote = 2;
    static constexpr std::uint32_t kDepth = 3;
    static constexpr std::uint32_t kDelay = 4;
    static constexpr std::uint32_t kFeedback = 5;
    static constexpr std::uint32_t kStereoPhase = 6;
    static constexpr std::uint32_t kMix = 7;
    static constexpr std::size_t kParameterCount = 8;

    static constexpr int kMaxChannels = 2;
    static constexpr double kMinDelayMs = 0.1;
    static constexpr double kMaxDelayMs = 10.0;
    static constexpr double kMaxSwingMs = 5.0; ///< the sweep at 100 % depth
    static constexpr double kMaxFeedback = 95.0; ///< %
    static constexpr double kRampSeconds = 0.02;
    static constexpr double kDelayRampSeconds = 0.1;

    [[nodiscard]] static ProcessorSpec spec() noexcept { return {"vloom.flanger", "Flanger", "1.0"}; }
    [[nodiscard]] static std::span<const ParameterSpec> parameters() noexcept { return kParameters; }

    void prepare(double sampleRate, int maxBlockFrames);
    void setParameter(std::uint32_t id, double value) noexcept;
    void setTempo(double bpm) noexcept;
    void process(float* const* channels, int numChannels, int numFrames) noexcept;
    void reset() noexcept;
    [[nodiscard]] int latencyFrames() const noexcept { return 0; }

    [[nodiscard]] double tempo() const noexcept { return tempo_; }
    /// The LFO rate now in Hz (free or synced).
    [[nodiscard]] double rateHz() const noexcept;

private:
    static constexpr std::array<ParameterSpec, kParameterCount> kParameters{
        ParameterSpec{kRate, "Rate", "Hz", 0.01, 10.0, 0.2, false, "LFO"},
        ParameterSpec{kSync, "Sync", "", 0.0, 1.0, 0.0, true, "LFO"},
        ParameterSpec{kNote, "Note", "", 0.0, kNoteValueCount - 1.0, note::kTwoBars, true, "LFO"},
        ParameterSpec{kDepth, "Depth", "%", 0.0, 100.0, 70.0, false, "LFO"},
        ParameterSpec{kDelay, "Delay", "ms", kMinDelayMs, kMaxDelayMs, 1.0, false, "Delay"},
        ParameterSpec{kFeedback, "Feedback", "%", -kMaxFeedback, kMaxFeedback, 50.0, false, "Delay"},
        ParameterSpec{kStereoPhase, "Stereo Phase", "deg", 0.0, 180.0, 90.0, false, "Stereo"},
        ParameterSpec{kMix, "Mix", "%", 0.0, 100.0, 50.0, false, "Output"},
    };

    [[nodiscard]] double value(std::uint32_t id) const noexcept { return values_[id]; }
    void applyTargets(bool jump) noexcept;

    std::array<double, kParameterCount> values_ = defaults();
    std::array<detail::FractionalDelay, kMaxChannels> lines_{};
    SmoothedValue depth_;
    SmoothedValue delay_;
    SmoothedValue feedback_;
    SmoothedValue stereoPhase_;
    SmoothedValue mix_;
    detail::LfoPhase lfo_;
    double tempo_ = kDefaultTempo;
    double sampleRate_ = 0.0; ///< 0: not prepared, passes through

    [[nodiscard]] static constexpr std::array<double, kParameterCount> defaults() noexcept
    {
        std::array<double, kParameterCount> values{};
        for (std::size_t i = 0; i < kParameterCount; ++i)
            values[i] = kParameters[i].defaultValue;
        return values;
    }
};

static_assert(Effect<Flanger>);
static_assert(TempoAware<Flanger>);

} // namespace vloom::dsp
