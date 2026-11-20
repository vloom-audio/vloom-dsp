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

/// "vloom.chorus": up to kMaxVoices copies of the input, each delayed by a
/// time that a sine LFO sweeps, mixed back with the input.
///
/// - Each voice reads the same delay line at its own LFO phase (voice 1 at 0,
///   voice 2 at 180, 3 at 60, 4 at 240 degrees); a voice that is switched on
///   or off fades. The delay sweeps from Delay up to Delay + Depth x
///   kMaxSwingMs.
/// - Spread offsets the right channel's LFO (100 %: 90 degrees, which keeps
///   the two sides apart for every voice count); Width narrows the wet
///   signal towards mono (0 %).
/// - Feedback sends the average of the voices back into the line.
/// - The LFO runs free in Hz or, with Sync on, at the length of a note at the
///   tempo setTempo() gives (kDefaultTempo before that). It does not follow
///   the host's transport position.
///
/// Mix 0 passes the input through bit for bit. Processes the first
/// kMaxChannels channels (one: mono, two: stereo); any others pass through.
/// No latency.
class Chorus {
public:
    // Parameter ids: saved in projects, so they never change.
    static constexpr std::uint32_t kRate = 0;
    static constexpr std::uint32_t kSync = 1;
    static constexpr std::uint32_t kNote = 2;
    static constexpr std::uint32_t kDepth = 3;
    static constexpr std::uint32_t kDelay = 4;
    static constexpr std::uint32_t kVoices = 5;
    static constexpr std::uint32_t kFeedback = 6;
    static constexpr std::uint32_t kSpread = 7;
    static constexpr std::uint32_t kWidth = 8;
    static constexpr std::uint32_t kMix = 9;
    static constexpr std::size_t kParameterCount = 10;

    static constexpr int kMaxChannels = 2;
    static constexpr int kMaxVoices = 4;
    static constexpr double kMaxDelayMs = 40.0;
    static constexpr double kMaxSwingMs = 8.0; ///< the sweep at 100 % depth
    static constexpr double kRampSeconds = 0.02;
    static constexpr double kDelayRampSeconds = 0.1; ///< slower: a delay glide bends the pitch

    [[nodiscard]] static ProcessorSpec spec() noexcept { return {"vloom.chorus", "Chorus", "1.0"}; }
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
        ParameterSpec{kRate, "Rate", "Hz", 0.01, 10.0, 0.8, false, "LFO"},
        ParameterSpec{kSync, "Sync", "", 0.0, 1.0, 0.0, true, "LFO"},
        ParameterSpec{kNote, "Note", "", 0.0, kNoteValueCount - 1.0, note::kHalf, true, "LFO"},
        ParameterSpec{kDepth, "Depth", "%", 0.0, 100.0, 50.0, false, "LFO"},
        ParameterSpec{kDelay, "Delay", "ms", 1.0, kMaxDelayMs, 10.0, false, "Voices"},
        ParameterSpec{kVoices, "Voices", "", 1.0, kMaxVoices, 2.0, true, "Voices"},
        ParameterSpec{kFeedback, "Feedback", "%", 0.0, 90.0, 0.0, false, "Voices"},
        ParameterSpec{kSpread, "Spread", "%", 0.0, 100.0, 50.0, false, "Stereo"},
        ParameterSpec{kWidth, "Width", "%", 0.0, 100.0, 100.0, false, "Stereo"},
        ParameterSpec{kMix, "Mix", "%", 0.0, 100.0, 50.0, false, "Output"},
    };

    /// Each voice's LFO phase, in cycles; fixed, so changing the voice count
    /// never makes a running voice jump.
    static constexpr std::array<double, kMaxVoices> kVoicePhases{0.0, 0.5, 1.0 / 6.0, 2.0 / 3.0};
    static constexpr double kMaxSpread = 0.25; ///< cycles, at 100 %

    [[nodiscard]] double value(std::uint32_t id) const noexcept { return values_[id]; }
    void applyTargets(bool jump) noexcept;

    std::array<double, kParameterCount> values_ = defaults();
    std::array<detail::FractionalDelay, kMaxChannels> lines_{};
    std::array<SmoothedValue, kMaxVoices> voiceGains_{
        SmoothedValue{}, SmoothedValue{}, SmoothedValue{}, SmoothedValue{}};
    SmoothedValue depth_;
    SmoothedValue delay_;
    SmoothedValue feedback_;
    SmoothedValue spread_;
    SmoothedValue width_;
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

static_assert(Effect<Chorus>);
static_assert(TempoAware<Chorus>);

} // namespace vloom::dsp
