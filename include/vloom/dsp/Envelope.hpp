// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include <algorithm>
#include <cmath>

namespace vloom::dsp {

/// The times and level of an attack, hold, decay, sustain, release envelope,
/// worked out for one sample rate. Real-time safe.
struct EnvelopeShape {
    static constexpr float kMinAttackSeconds = 0.0005f; ///< shorter clicks
    static constexpr float kMinReleaseSeconds = 0.002f;

    float attackStep = 1.0f; ///< added per frame
    int holdFrames = 0;
    float decayFactor = 0.0f; ///< the distance to the sustain level shrinks by this per frame
    float sustain = 1.0f;
    float releaseFactor = 0.0f;

    /// Times in seconds, `sustain` 0 .. 1. The attack rises in a straight
    /// line; decay and release fall (to -43 dB) in the time given.
    [[nodiscard]] static EnvelopeShape make(
        float sampleRate, float attack, float hold, float decay, float sustain, float release) noexcept
    {
        const auto factor = [sampleRate](float time) { return std::exp(-5.0f / (time * sampleRate)); };
        EnvelopeShape shape;
        shape.attackStep = std::min(1.0f, 1.0f / (std::max(attack, kMinAttackSeconds) * sampleRate));
        shape.holdFrames = static_cast<int>(hold * sampleRate);
        shape.decayFactor = factor(std::max(decay, kMinReleaseSeconds));
        shape.sustain = sustain;
        shape.releaseFactor = factor(std::max(release, kMinReleaseSeconds));
        return shape;
    }
};

/// One running attack, hold, decay, sustain, release envelope (0 .. 1).
/// Real-time safe: call next() once per frame.
struct Envelope {
    static constexpr float kSilent = 1.0e-4f; ///< an envelope this low has ended (-80 dB)

    enum class Stage { Idle, Attack, Hold, Decay, Sustain, Release };
    Stage stage = Stage::Idle;
    float level = 0.0f;
    int holdLeft = 0;

    /// From wherever the level is, so a restarted note does not click.
    void start() noexcept { stage = Stage::Attack; }
    void release() noexcept
    {
        if (stage != Stage::Idle)
            stage = Stage::Release;
    }
    [[nodiscard]] bool idle() const noexcept { return stage == Stage::Idle; }

    [[nodiscard]] float next(const EnvelopeShape& shape) noexcept
    {
        switch (stage) {
        case Stage::Idle:
            return 0.0f;
        case Stage::Attack:
            level += shape.attackStep;
            if (level >= 1.0f) {
                level = 1.0f;
                holdLeft = shape.holdFrames;
                stage = holdLeft > 0 ? Stage::Hold : Stage::Decay;
            }
            break;
        case Stage::Hold:
            if (--holdLeft <= 0)
                stage = Stage::Decay;
            break;
        case Stage::Decay:
            level = shape.sustain + (level - shape.sustain) * shape.decayFactor;
            if (level - shape.sustain < kSilent) {
                level = shape.sustain;
                stage = Stage::Sustain;
            }
            break;
        case Stage::Sustain:
            level = shape.sustain; // follows the knob
            break;
        case Stage::Release:
            level *= shape.releaseFactor;
            if (level < kSilent) {
                level = 0.0f;
                stage = Stage::Idle;
            }
            break;
        }
        return level;
    }
};

} // namespace vloom::dsp
