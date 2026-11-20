// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/effects/Flanger.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vloom::dsp {

void Flanger::prepare(double sampleRate, int /*maxBlockFrames*/)
{
    sampleRate_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 0.0;
    const double maxFrames = (kMaxDelayMs + kMaxSwingMs) * 0.001 * sampleRate_ + 4.0;
    for (detail::FractionalDelay& line : lines_)
        line.prepare(static_cast<int>(std::ceil(maxFrames)));
    depth_.reset(sampleRate_, kRampSeconds, 0.0f);
    delay_.reset(sampleRate_, kDelayRampSeconds, 0.0f);
    feedback_.reset(sampleRate_, kRampSeconds, 0.0f);
    stereoPhase_.reset(sampleRate_, kRampSeconds, 0.0f);
    mix_.reset(sampleRate_, kRampSeconds, 0.0f);
    reset();
}

void Flanger::setParameter(std::uint32_t id, double value) noexcept
{
    if (id >= kParameterCount || !std::isfinite(value))
        return;
    values_[id] = detail::clampToSpec(kParameters[id], value);
    applyTargets(false);
}

void Flanger::setTempo(double bpm) noexcept
{
    if (std::isfinite(bpm) && bpm > 0.0)
        tempo_ = clampTempo(bpm);
}

double Flanger::rateHz() const noexcept
{
    return detail::lfoRateHz(value(kSync) >= 0.5, value(kRate), static_cast<int>(value(kNote)), tempo_);
}

void Flanger::applyTargets(bool jump) noexcept
{
    const auto set = [jump](SmoothedValue& glide, double target) {
        if (jump)
            glide.setCurrentAndTarget(static_cast<float>(target));
        else
            glide.setTarget(static_cast<float>(target));
    };
    set(depth_, value(kDepth) / 100.0);
    set(delay_, value(kDelay));
    set(feedback_, value(kFeedback) / 100.0);
    set(stereoPhase_, value(kStereoPhase) / 360.0);
    set(mix_, value(kMix) / 100.0);
}

void Flanger::reset() noexcept
{
    applyTargets(true);
    for (detail::FractionalDelay& line : lines_)
        line.clear();
    lfo_.reset();
}

void Flanger::process(float* const* channels, int numChannels, int numFrames) noexcept
{
    if (!(sampleRate_ > 0.0) || channels == nullptr || numFrames <= 0)
        return;
    numChannels = std::min(numChannels, kMaxChannels);
    lfo_.setRate(rateHz(), sampleRate_);
    const double framesPerMs = sampleRate_ * 0.001;

    for (int i = 0; i < numFrames; ++i) {
        const double depth = static_cast<double>(depth_.next()) * kMaxSwingMs;
        const double delay = static_cast<double>(delay_.next());
        const float feedback = feedback_.next();
        const double offset = static_cast<double>(stereoPhase_.next());
        const float mix = mix_.next();
        const double phase = lfo_.advance();
        for (int ch = 0; ch < numChannels; ++ch) {
            detail::FractionalDelay& line = lines_[static_cast<std::size_t>(ch)];
            const double channelPhase = phase + (ch == 1 ? offset : 0.0);
            const double sweep = 0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * channelPhase);
            const float wet = line.read((delay + depth * sweep) * framesPerMs);
            float& sample = channels[ch][i];
            line.push(detail::sanitize(sample + feedback * wet));
            sample += mix * (wet - sample);
        }
    }
}

} // namespace vloom::dsp
