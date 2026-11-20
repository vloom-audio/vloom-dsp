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

/// "vloom.phaser": the input mixed with itself sent through a chain of
/// first-order all-pass filters whose frequency a sine LFO sweeps. Mixed
/// 50/50, every two stages make one notch.
///
/// - Stages: 2 .. kMaxStages, even (an odd value rounds up). Changing it
///   fades between the two chain lengths.
/// - The stages sweep around Frequency by up to kMaxSweepOctaves either way
///   at 100 % depth (kept between kMinFrequency and kMaxFrequency).
/// - Feedback (-95 .. +95 %) sends the chain's output back to its input,
///   which sharpens the notches and the peaks between them.
/// - Stereo Phase offsets the right channel's LFO (0 .. 180 degrees).
/// - The LFO runs free in Hz or, with Sync on, at the length of a note at the
///   tempo setTempo() gives (kDefaultTempo before that). It does not follow
///   the host's transport position.
///
/// Mix 0 passes the input through bit for bit. Processes the first
/// kMaxChannels channels; any others pass through. No latency.
class Phaser {
public:
    // Parameter ids: saved in projects, so they never change.
    static constexpr std::uint32_t kRate = 0;
    static constexpr std::uint32_t kSync = 1;
    static constexpr std::uint32_t kNote = 2;
    static constexpr std::uint32_t kDepth = 3;
    static constexpr std::uint32_t kStages = 4;
    static constexpr std::uint32_t kFrequency = 5;
    static constexpr std::uint32_t kFeedback = 6;
    static constexpr std::uint32_t kStereoPhase = 7;
    static constexpr std::uint32_t kMix = 8;
    static constexpr std::size_t kParameterCount = 9;

    static constexpr int kMaxChannels = 2;
    static constexpr int kMinStages = 2;
    static constexpr int kMaxStages = 12;
    static constexpr double kMinFrequency = 20.0;
    static constexpr double kMaxFrequency = 20000.0;
    static constexpr double kMaxSweepOctaves = 2.0;
    static constexpr double kMaxFeedback = 95.0; ///< %
    static constexpr double kRampSeconds = 0.02;

    [[nodiscard]] static ProcessorSpec spec() noexcept { return {"vloom.phaser", "Phaser", "1.0"}; }
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
    /// The stage count in use (even).
    [[nodiscard]] int stages() const noexcept { return static_cast<int>(value(kStages)); }

private:
    static constexpr std::array<ParameterSpec, kParameterCount> kParameters{
        ParameterSpec{kRate, "Rate", "Hz", 0.01, 10.0, 0.5, false, "LFO"},
        ParameterSpec{kSync, "Sync", "", 0.0, 1.0, 0.0, true, "LFO"},
        ParameterSpec{kNote, "Note", "", 0.0, kNoteValueCount - 1.0, note::kWhole, true, "LFO"},
        ParameterSpec{kDepth, "Depth", "%", 0.0, 100.0, 70.0, false, "LFO"},
        ParameterSpec{kStages, "Stages", "", kMinStages, kMaxStages, 4.0, true, "Filter"},
        ParameterSpec{kFrequency, "Frequency", "Hz", 100.0, 8000.0, 800.0, false, "Filter"},
        ParameterSpec{kFeedback, "Feedback", "%", -kMaxFeedback, kMaxFeedback, 30.0, false, "Filter"},
        ParameterSpec{kStereoPhase, "Stereo Phase", "deg", 0.0, 180.0, 90.0, false, "Stereo"},
        ParameterSpec{kMix, "Mix", "%", 0.0, 100.0, 50.0, false, "Output"},
    };

    struct Channel {
        std::array<detail::AllpassStage, kMaxStages> stages{};
        float last = 0.0f; ///< the chain's last output, for feedback
    };

    [[nodiscard]] double value(std::uint32_t id) const noexcept { return values_[id]; }
    void applyTargets(bool jump) noexcept;

    std::array<double, kParameterCount> values_ = defaults();
    std::array<Channel, kMaxChannels> channels_{};
    SmoothedValue depth_;
    SmoothedValue stages_; ///< glides through the even counts in between
    SmoothedValue octave_; ///< log2 of the frequency
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

static_assert(Effect<Phaser>);
static_assert(TempoAware<Phaser>);

} // namespace vloom::dsp
