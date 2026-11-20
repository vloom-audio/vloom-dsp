// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/effects/Chorus.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vloom::dsp {

void Chorus::prepare(double sampleRate, int /*maxBlockFrames*/)
{
    sampleRate_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 0.0;
    const double maxFrames = (kMaxDelayMs + kMaxSwingMs) * 0.001 * sampleRate_ + 4.0;
    for (detail::FractionalDelay& line : lines_)
        line.prepare(static_cast<int>(std::ceil(maxFrames)));
    for (SmoothedValue& gain : voiceGains_)
        gain.reset(sampleRate_, kRampSeconds, 0.0f);
    depth_.reset(sampleRate_, kRampSeconds, 0.0f);
    delay_.reset(sampleRate_, kDelayRampSeconds, 0.0f);
    feedback_.reset(sampleRate_, kRampSeconds, 0.0f);
    spread_.reset(sampleRate_, kRampSeconds, 0.0f);
    width_.reset(sampleRate_, kRampSeconds, 0.0f);
    mix_.reset(sampleRate_, kRampSeconds, 0.0f);
    reset();
}

void Chorus::setParameter(std::uint32_t id, double value) noexcept
{
    if (id >= kParameterCount || !std::isfinite(value))
        return;
    values_[id] = detail::clampToSpec(kParameters[id], value);
    applyTargets(false);
}

void Chorus::setTempo(double bpm) noexcept
{
    if (std::isfinite(bpm) && bpm > 0.0)
        tempo_ = clampTempo(bpm);
}

double Chorus::rateHz() const noexcept
{
    return detail::lfoRateHz(value(kSync) >= 0.5, value(kRate), static_cast<int>(value(kNote)), tempo_);
}

void Chorus::applyTargets(bool jump) noexcept
{
    const auto set = [jump](SmoothedValue& glide, double target) {
        if (jump)
            glide.setCurrentAndTarget(static_cast<float>(target));
        else
            glide.setTarget(static_cast<float>(target));
    };
    const int voices = static_cast<int>(value(kVoices));
    for (int v = 0; v < kMaxVoices; ++v)
        set(voiceGains_[static_cast<std::size_t>(v)], v < voices ? 1.0 : 0.0);
    set(depth_, value(kDepth) / 100.0);
    set(delay_, value(kDelay));
    set(feedback_, value(kFeedback) / 100.0);
    set(spread_, value(kSpread) / 100.0);
    set(width_, value(kWidth) / 100.0);
    set(mix_, value(kMix) / 100.0);
}

void Chorus::reset() noexcept
{
    applyTargets(true);
    for (detail::FractionalDelay& line : lines_)
        line.clear();
    lfo_.reset();
}

void Chorus::process(float* const* channels, int numChannels, int numFrames) noexcept
{
    if (!(sampleRate_ > 0.0) || channels == nullptr || numFrames <= 0)
        return;
    numChannels = std::min(numChannels, kMaxChannels);
    lfo_.setRate(rateHz(), sampleRate_);
    const double framesPerMs = sampleRate_ * 0.001;

    for (int i = 0; i < numFrames; ++i) {
        std::array<float, kMaxVoices> gains{};
        float gainSum = 0.0f;
        for (std::size_t v = 0; v < gains.size(); ++v) {
            gains[v] = voiceGains_[v].next();
            gainSum += gains[v];
        }
        // The voices add up like uncorrelated signals; feedback takes their
        // average, so it never exceeds the feedback amount.
        const float wetScale = gainSum > 0.0f ? 1.0f / std::sqrt(gainSum) : 0.0f;
        const float feedback = feedback_.next();
        const float feedbackScale = gainSum > 0.0f ? feedback / gainSum : 0.0f;
        const double depth = static_cast<double>(depth_.next()) * kMaxSwingMs;
        const double delay = static_cast<double>(delay_.next());
        const double spread = kMaxSpread * static_cast<double>(spread_.next());
        const float width = width_.next();
        const float mix = mix_.next();
        const double phase = lfo_.advance();

        std::array<float, kMaxChannels> wet{};
        for (int ch = 0; ch < numChannels; ++ch) {
            detail::FractionalDelay& line = lines_[static_cast<std::size_t>(ch)];
            const double channelPhase = phase + (ch == 1 ? spread : 0.0);
            float sum = 0.0f;
            for (std::size_t v = 0; v < gains.size(); ++v) {
                if (gains[v] == 0.0f)
                    continue;
                const double sweep
                    = 0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * (channelPhase + kVoicePhases[v]));
                sum += gains[v] * line.read((delay + depth * sweep) * framesPerMs);
            }
            const float in = channels[ch][i];
            line.push(detail::sanitize(in + feedbackScale * sum));
            wet[static_cast<std::size_t>(ch)] = sum * wetScale;
        }
        if (numChannels == 2) {
            const float mid = 0.5f * (wet[0] + wet[1]);
            const float side = 0.5f * (wet[0] - wet[1]) * width;
            wet[0] = mid + side;
            wet[1] = mid - side;
        }
        for (int ch = 0; ch < numChannels; ++ch) {
            float& sample = channels[ch][i];
            sample += mix * (wet[static_cast<std::size_t>(ch)] - sample);
        }
    }
}

} // namespace vloom::dsp
