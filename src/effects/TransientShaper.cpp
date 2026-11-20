// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/effects/TransientShaper.hpp"

#include <algorithm>
#include <cmath>

namespace vloom::dsp {

namespace {

/// The one-pole coefficient that covers ~63 % of a step in `seconds`.
[[nodiscard]] double follow(double seconds, double sampleRate) noexcept
{
    return 1.0 - std::exp(-1.0 / (seconds * sampleRate));
}

/// A level in dB, floored far below anything audible so silence stays
/// finite (and every envelope agrees on it: no shaping).
[[nodiscard]] double toDb(double level) noexcept { return 20.0 * std::log10(std::max(level, 1.0e-9)); }

} // namespace

void TransientShaper::prepare(double sampleRate, int /*maxBlockFrames*/)
{
    sampleRate_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 0.0;
    attack_.reset(sampleRate_, kRampSeconds, 0.0f);
    sustain_.reset(sampleRate_, kRampSeconds, 0.0f);
    output_.reset(sampleRate_, kRampSeconds, 1.0f);
    mix_.reset(sampleRate_, kRampSeconds, 1.0f);
    reset();
}

void TransientShaper::setParameter(std::uint32_t id, double value) noexcept
{
    if (id >= kParameterCount || !std::isfinite(value))
        return;
    const ParameterSpec& spec = kParameters[id];
    values_[id] = std::clamp(value, spec.minValue, spec.maxValue);
    applyTargets(false);
    if (id == kSpeed)
        updateTimes();
}

void TransientShaper::applyTargets(bool jump) noexcept
{
    const auto set = [jump](SmoothedValue& glide, double target) {
        if (jump)
            glide.setCurrentAndTarget(static_cast<float>(target));
        else
            glide.setTarget(static_cast<float>(target));
    };
    set(attack_, value(kAttack) / 100.0);
    set(sustain_, value(kSustain) / 100.0);
    set(output_, std::pow(10.0, value(kOutput) / 20.0));
    set(mix_, value(kMix) / 100.0);
}

void TransientShaper::updateTimes() noexcept
{
    if (!(sampleRate_ > 0.0))
        return;
    const double shorter = std::pow(kSpeedRange, -value(kSpeed) / 100.0);
    const double fastAttack = follow(kFastAttackSeconds, sampleRate_);
    const double fastRelease = follow(kFastReleaseSeconds * shorter, sampleRate_);
    fast_.attack = fastAttack;
    fast_.release = fastRelease;
    slowAttack_.attack = follow(kSlowAttackSeconds * shorter, sampleRate_);
    slowAttack_.release = fastRelease;
    slowRelease_.attack = fastAttack;
    slowRelease_.release = follow(kSlowReleaseSeconds * shorter, sampleRate_);
}

void TransientShaper::reset() noexcept
{
    applyTargets(true);
    fast_.level = 0.0;
    slowAttack_.level = 0.0;
    slowRelease_.level = 0.0;
    updateTimes();
}

void TransientShaper::process(float* const* channels, int numChannels, int numFrames) noexcept
{
    if (!(sampleRate_ > 0.0) || channels == nullptr || numFrames <= 0)
        return;
    numChannels = std::min(numChannels, kMaxChannels);

    for (int i = 0; i < numFrames; ++i) {
        double peak = 0.0;
        for (int ch = 0; ch < numChannels; ++ch) {
            const double size = std::abs(static_cast<double>(channels[ch][i]));
            if (size > peak) // NaN is never larger: ignored
                peak = size;
        }
        peak = std::min(peak, 1.0e6);
        const double fast = toDb(fast_.next(peak));
        const double slowAttack = toDb(slowAttack_.next(peak));
        const double slowRelease = toDb(slowRelease_.next(peak));
        const double onset = std::clamp(fast - slowAttack, 0.0, kMaxShapeDb);
        const double tail = std::clamp(slowRelease - fast, 0.0, kMaxShapeDb);

        const auto attack = static_cast<double>(attack_.next());
        const auto sustain = static_cast<double>(sustain_.next());
        const double shapeDb = std::clamp(attack * onset + sustain * tail, -kMaxShapeDb, kMaxShapeDb);
        const float output = output_.next();
        const float mix = mix_.next();
        const float gain = static_cast<float>(std::pow(10.0, shapeDb / 20.0));

        for (int ch = 0; ch < numChannels; ++ch) {
            float& sample = channels[ch][i];
            const float wet = sample * gain;
            sample = (sample + mix * (wet - sample)) * output;
        }
    }
}

} // namespace vloom::dsp
