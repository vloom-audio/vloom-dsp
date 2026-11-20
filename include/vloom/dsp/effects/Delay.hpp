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

/// "vloom.delay": a stereo or ping-pong echo.
///
/// - Time: in milliseconds, or a note value at the host's tempo (Sync; the
///   host calls setTempo(), TempoAware). Stereo Offset makes the right side
///   longer or shorter than the left. Times are kept up to kMaxSeconds;
///   a new time glides there over kTimeRampSeconds, like tape.
/// - Stereo: each side echoes itself. Ping-pong: both sides go in on the
///   left, and the echoes bounce between the sides.
/// - Feedback (0 .. 100 %), with a low cut and a high cut (12 dB per
///   octave) on everything that goes into the delay, so each repeat is
///   thinner than the last. At 20 Hz and 20 kHz the cuts are out of the path.
/// - Width of the echoes (0 %: mono), and a dry/wet mix.
///
/// A mono input (one channel) echoes in mono. No latency. Processes up to
/// kMaxChannels channels; others pass through.
class Delay {
public:
    // Parameter ids: saved in projects, so they never change.
    enum Id : std::uint32_t {
        kMode,
        kSync,
        kTime,
        kNote,
        kStereoOffset,
        kFeedback,
        kLowCut,
        kHighCut,
        kWidth,
        kMix,
        kParameterCount
    };

    /// The value of kMode.
    enum class Mode { Stereo, PingPong };

    static constexpr int kMaxChannels = 2;
    static constexpr double kMaxSeconds = 4.0;
    static constexpr double kTimeRampSeconds = 0.1;
    static constexpr double kRampSeconds = 0.02;
    static constexpr double kDefaultTempo = 120.0;
    static constexpr int kNoteCount = 15;

    [[nodiscard]] static ProcessorSpec spec() noexcept { return {"vloom.delay", "Delay", "1.0"}; }
    [[nodiscard]] static std::span<const ParameterSpec> parameters() noexcept { return kParameters; }

    void prepare(double sampleRate, int maxBlockFrames);
    void setParameter(std::uint32_t id, double value) noexcept;
    /// The host's tempo in beats per minute (TempoAware); 120 until called.
    void setTempo(double bpm) noexcept;
    void process(float* const* channels, int numChannels, int numFrames) noexcept;
    void reset() noexcept;
    [[nodiscard]] int latencyFrames() const noexcept { return 0; }

    /// The name of note value `index` (the value of kNote), e.g. "1/8D";
    /// "" outside 0 .. kNoteCount - 1.
    [[nodiscard]] static const char* noteName(int index) noexcept;
    /// The length of note value `index` in beats (quarter notes).
    [[nodiscard]] static double noteBeats(int index) noexcept;
    /// The delay of the left (`right` false) or right side, in seconds, for
    /// the current settings and tempo, before the kMaxSeconds limit.
    [[nodiscard]] double delaySeconds(bool right) const noexcept;

private:
    static constexpr std::array<ParameterSpec, kParameterCount> kParameters{
        ParameterSpec{kMode, "Mode", "", 0.0, 1.0, 0.0, true, "Delay"},
        ParameterSpec{kSync, "Sync", "", 0.0, 1.0, 1.0, true, "Time"},
        ParameterSpec{kTime, "Time", "ms", 1.0, kMaxSeconds * 1000.0, 375.0, false, "Time"},
        ParameterSpec{kNote, "Note", "", 0.0, kNoteCount - 1.0, 5.0, true, "Time"},
        ParameterSpec{kStereoOffset, "Stereo Offset", "%", -50.0, 50.0, 0.0, false, "Time"},
        ParameterSpec{kFeedback, "Feedback", "%", 0.0, 100.0, 35.0, false, "Feedback"},
        ParameterSpec{kLowCut, "Low Cut", "Hz", 20.0, 2000.0, 80.0, false, "Feedback"},
        ParameterSpec{kHighCut, "High Cut", "Hz", 1000.0, 20000.0, 12000.0, false, "Feedback"},
        ParameterSpec{kWidth, "Width", "%", 0.0, 100.0, 100.0, false, "Output"},
        ParameterSpec{kMix, "Mix", "%", 0.0, 100.0, 30.0, false, "Output"},
    };

    struct Side {
        DelayLine line;
        SmoothedValue frames{1.0f}; ///< the delay, in frames
        SvfState lowCut;
        SvfState highCut;
    };

    [[nodiscard]] double value(std::uint32_t id) const noexcept { return values_[id]; }
    void applyValues(bool jump) noexcept;
    [[nodiscard]] float feed(Side& side, float sample) noexcept;

    std::array<double, kParameterCount> values_ = defaultValues(kParameters);
    double sampleRate_ = 0.0; ///< 0: not prepared, passes through
    double tempo_ = kDefaultTempo;
    std::array<Side, kMaxChannels> sides_{};
    SvfCoefficients lowCut_{};
    SvfCoefficients highCut_{};
    bool lowCutOn_ = false;
    bool highCutOn_ = false;
    SmoothedValue feedback_{0.0f};
    SmoothedValue width_{1.0f};
    SmoothedValue mix_{0.0f};
};

static_assert(Effect<Delay>);
static_assert(TempoAware<Delay>);

} // namespace vloom::dsp
