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

/// "vloom.tremolo": an LFO that moves the level (tremolo) or the stereo
/// position (auto-pan).
///
/// - Tremolo: the gain swings between 1 and 1 - Depth; Stereo Phase offsets
///   the right channel's LFO (0 .. 180 degrees).
/// - Auto-pan: the sound moves between left and right; at the far side the
///   other channel is at 1 - Depth, in the middle both are at 1 (it never
///   boosts). Stereo Phase is not used; a mono signal gets a tremolo.
/// - Shapes: sine, triangle and a square with rounded edges. Changing the
///   mode or the shape fades from one to the other.
/// - The LFO runs free in Hz or, with Sync on, at the length of a note at the
///   tempo setTempo() gives (kDefaultTempo before that). It does not follow
///   the host's transport position.
///
/// Depth 0 passes the input through bit for bit. Processes the first
/// kMaxChannels channels; any others pass through. No latency.
class Tremolo {
public:
    // Parameter ids: saved in projects, so they never change.
    static constexpr std::uint32_t kMode = 0;
    static constexpr std::uint32_t kRate = 1;
    static constexpr std::uint32_t kSync = 2;
    static constexpr std::uint32_t kNote = 3;
    static constexpr std::uint32_t kShape = 4;
    static constexpr std::uint32_t kDepth = 5;
    static constexpr std::uint32_t kStereoPhase = 6;
    static constexpr std::size_t kParameterCount = 7;

    /// The value of the Mode parameter.
    enum class Mode { Tremolo, AutoPan };
    /// The value of the Shape parameter.
    enum class Shape { Sine, Triangle, Square };
    static constexpr int kShapes = 3;

    static constexpr int kMaxChannels = 2;
    static constexpr double kRampSeconds = 0.02;

    [[nodiscard]] static ProcessorSpec spec() noexcept { return {"vloom.tremolo", "Tremolo", "1.0"}; }
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
        ParameterSpec{kMode, "Mode", "", 0.0, 1.0, 0.0, true, "Mode"},
        ParameterSpec{kRate, "Rate", "Hz", 0.1, 20.0, 4.0, false, "LFO"},
        ParameterSpec{kSync, "Sync", "", 0.0, 1.0, 0.0, true, "LFO"},
        ParameterSpec{kNote, "Note", "", 0.0, kNoteValueCount - 1.0, note::kEighth, true, "LFO"},
        ParameterSpec{kShape, "Shape", "", 0.0, kShapes - 1.0, 0.0, true, "LFO"},
        ParameterSpec{kDepth, "Depth", "%", 0.0, 100.0, 50.0, false, "LFO"},
        ParameterSpec{kStereoPhase, "Stereo Phase", "deg", 0.0, 180.0, 0.0, false, "Stereo"},
    };

    [[nodiscard]] double value(std::uint32_t id) const noexcept { return values_[id]; }
    void applyTargets(bool jump) noexcept;

    std::array<double, kParameterCount> values_ = defaults();
    /// 1 for the shape in use.
    std::array<SmoothedValue, kShapes> shapeWeights_{SmoothedValue{}, SmoothedValue{}, SmoothedValue{}};
    SmoothedValue autoPan_; ///< 0: tremolo, 1: auto-pan
    SmoothedValue depth_;
    SmoothedValue stereoPhase_;
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

static_assert(Effect<Tremolo>);
static_assert(TempoAware<Tremolo>);

} // namespace vloom::dsp
