// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/effects/Phaser.hpp"

#include <algorithm>
#include <cmath>

namespace vloom::dsp {

void Phaser::prepare(double sampleRate, int /*maxBlockFrames*/)
{
    sampleRate_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 0.0;
    depth_.reset(sampleRate_, kRampSeconds, 0.0f);
    stages_.reset(sampleRate_, kRampSeconds, 0.0f);
    octave_.reset(sampleRate_, kRampSeconds, 0.0f);
    feedback_.reset(sampleRate_, kRampSeconds, 0.0f);
    stereoPhase_.reset(sampleRate_, kRampSeconds, 0.0f);
    mix_.reset(sampleRate_, kRampSeconds, 0.0f);
    reset();
}

void Phaser::setParameter(std::uint32_t id, double value) noexcept
{
    if (id >= kParameterCount || !std::isfinite(value))
        return;
    value = detail::clampToSpec(kParameters[id], value);
    if (id == kStages)
        value = 2.0 * std::ceil(value / 2.0); // even: an odd count rounds up
    values_[id] = value;
    applyTargets(false);
}

void Phaser::setTempo(double bpm) noexcept
{
    if (std::isfinite(bpm) && bpm > 0.0)
        tempo_ = clampTempo(bpm);
}

double Phaser::rateHz() const noexcept
{
    return detail::lfoRateHz(value(kSync) >= 0.5, value(kRate), static_cast<int>(value(kNote)), tempo_);
}

void Phaser::applyTargets(bool jump) noexcept
{
    const auto set = [jump](SmoothedValue& glide, double target) {
        if (jump)
            glide.setCurrentAndTarget(static_cast<float>(target));
        else
            glide.setTarget(static_cast<float>(target));
    };
    set(depth_, value(kDepth) / 100.0);
    set(stages_, value(kStages));
    set(octave_, std::log2(value(kFrequency)));
    set(feedback_, value(kFeedback) / 100.0);
    set(stereoPhase_, value(kStereoPhase) / 360.0);
    set(mix_, value(kMix) / 100.0);
}

void Phaser::reset() noexcept
{
    applyTargets(true);
    for (Channel& channel : channels_) {
        for (detail::AllpassStage& stage : channel.stages)
            stage.clear();
        channel.last = 0.0f;
    }
    lfo_.reset();
}

void Phaser::process(float* const* channels, int numChannels, int numFrames) noexcept
{
    if (!(sampleRate_ > 0.0) || channels == nullptr || numFrames <= 0)
        return;
    numChannels = std::min(numChannels, kMaxChannels);
    lfo_.setRate(rateHz(), sampleRate_);
    const double lowest = std::log2(kMinFrequency);
    const double highest = std::log2(std::min(kMaxFrequency, 0.45 * sampleRate_));

    for (int i = 0; i < numFrames; ++i) {
        const double sweep = static_cast<double>(depth_.next()) * kMaxSweepOctaves;
        const double octave = static_cast<double>(octave_.next());
        const float feedback = feedback_.next();
        const double offset = static_cast<double>(stereoPhase_.next());
        const float mix = mix_.next();
        // Between two even counts, the output fades from one tap to the next.
        const float stageCount = stages_.next();
        const int low = std::clamp(2 * static_cast<int>(stageCount / 2.0f), kMinStages, kMaxStages);
        const int high = std::min(low + 2, kMaxStages);
        const float toHigh = std::clamp((stageCount - static_cast<float>(low)) * 0.5f, 0.0f, 1.0f);
        const double phase = lfo_.advance();

        for (int ch = 0; ch < numChannels; ++ch) {
            Channel& channel = channels_[static_cast<std::size_t>(ch)];
            const double lfo = detail::lfoValue(detail::LfoShape::Sine, phase + (ch == 1 ? offset : 0.0));
            const double frequency = std::exp2(std::clamp(octave + sweep * lfo, lowest, highest));
            const float a = detail::AllpassStage::coefficient(frequency, sampleRate_);
            float& sample = channels[ch][i];
            float signal = detail::sanitize(sample + feedback * channel.last);
            float lowTap = 0.0f;
            float highTap = 0.0f;
            for (int s = 0; s < kMaxStages; ++s) {
                signal = channel.stages[static_cast<std::size_t>(s)].process(a, signal);
                if (s + 1 == low)
                    lowTap = signal;
                if (s + 1 == high)
                    highTap = signal;
            }
            const float wet = lowTap + toHigh * (highTap - lowTap);
            channel.last = detail::sanitize(wet);
            sample += mix * (wet - sample);
        }
    }
}

} // namespace vloom::dsp
