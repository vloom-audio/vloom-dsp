// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/effects/DeEsser.hpp"

#include <algorithm>
#include <cmath>

namespace vloom::dsp {

namespace {

/// The one-pole coefficient that covers ~63 % of a step in `seconds`.
[[nodiscard]] double follow(double seconds, double sampleRate) noexcept
{
    return 1.0 - std::exp(-1.0 / (seconds * sampleRate));
}

} // namespace

void DeEsser::prepare(double sampleRate, int /*maxBlockFrames*/)
{
    sampleRate_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 0.0;
    const double controlRate = sampleRate_ / kControlFrames;
    octave_.reset(controlRate, kRampSeconds, 0.0f);
    threshold_.reset(sampleRate_, kRampSeconds, 0.0f);
    range_.reset(sampleRate_, kRampSeconds, 0.0f);
    split_.reset(sampleRate_, kRampSeconds, 0.0f);
    listen_.reset(sampleRate_, kRampSeconds, 0.0f);
    attack_ = sampleRate_ > 0.0 ? follow(kAttackSeconds, sampleRate_) : 0.0;
    release_ = sampleRate_ > 0.0 ? follow(kReleaseSeconds, sampleRate_) : 0.0;
    reset();
}

void DeEsser::setParameter(std::uint32_t id, double value) noexcept
{
    if (id >= kParameterCount || !std::isfinite(value))
        return;
    const ParameterSpec& spec = kParameters[id];
    value = std::clamp(value, spec.minValue, spec.maxValue);
    values_[id] = spec.stepped ? std::round(value) : value;
    applyTargets(false);
    changed_ = true;
}

void DeEsser::applyTargets(bool jump) noexcept
{
    const auto set = [jump](SmoothedValue& glide, double target) {
        if (jump)
            glide.setCurrentAndTarget(static_cast<float>(target));
        else
            glide.setTarget(static_cast<float>(target));
    };
    set(octave_, std::log2(value(kFrequency)));
    set(threshold_, value(kThreshold));
    set(range_, value(kRange));
    set(split_, value(kMode) >= 0.5 ? 1.0 : 0.0);
    set(listen_, value(kListen) >= 0.5 ? 1.0 : 0.0);
}

void DeEsser::updateFilter(bool force) noexcept
{
    const bool moving = octave_.isSmoothing();
    (void)octave_.next();
    if (moving || force || changed_) {
        // At rest: the exact frequency, not the glide's single precision.
        frequency_
            = octave_.isSmoothing() ? std::exp2(static_cast<double>(octave_.current())) : value(kFrequency);
        highPass_ = biquad::highPass(frequency_, sampleRate_);
        changed_ = false;
    }
    // At 0 dB the shelf's numerator and denominator are the same numbers, so
    // it passes its input through exactly.
    shelf_ = biquad::highShelf(frequency_, sampleRate_, -reduction_);
}

void DeEsser::reset() noexcept
{
    applyTargets(true);
    for (BiquadState& state : states_)
        state.clear();
    for (BiquadState& state : shelfStates_)
        state.clear();
    envelope_ = 0.0;
    reduction_ = 0.0;
    untilControl_ = 0;
    if (sampleRate_ > 0.0)
        updateFilter(true);
    reductionDb_.store(0.0f, std::memory_order_relaxed);
}

void DeEsser::process(float* const* channels, int numChannels, int numFrames) noexcept
{
    if (!(sampleRate_ > 0.0) || channels == nullptr || numFrames <= 0)
        return;
    numChannels = std::min(numChannels, kMaxChannels);

    for (int i = 0; i < numFrames; ++i) {
        if (untilControl_ == 0) {
            updateFilter(false);
            untilControl_ = kControlFrames;
        }
        --untilControl_;

        std::array<double, kMaxChannels> band{};
        double level = 0.0;
        for (int ch = 0; ch < numChannels; ++ch) {
            const auto in = static_cast<double>(channels[ch][i]);
            const double x = std::isfinite(in) ? in : 0.0; // a bad sample must not stick in the filter
            const double y = states_[static_cast<std::size_t>(ch)].process(highPass_, x);
            band[static_cast<std::size_t>(ch)] = y;
            level = std::max(level, std::abs(y));
        }
        envelope_ += (level > envelope_ ? attack_ : release_) * (level - envelope_);
        if (envelope_ < 1.0e-12)
            envelope_ = 0.0;

        const auto threshold = static_cast<double>(threshold_.next());
        const auto range = static_cast<double>(range_.next());
        reduction_ = 0.0;
        if (envelope_ > 1.0e-6) // -120 dB
            reduction_ = std::clamp(20.0 * std::log10(envelope_) - threshold, 0.0, range);
        const double gain = reduction_ > 0.0 ? std::pow(10.0, -reduction_ / 20.0) : 1.0;
        const auto split = static_cast<double>(split_.next());
        const auto listen = static_cast<double>(listen_.next());

        for (int ch = 0; ch < numChannels; ++ch) {
            float& sample = channels[ch][i];
            const auto x = static_cast<double>(sample);
            const double shelved
                = shelfStates_[static_cast<std::size_t>(ch)].process(shelf_, std::isfinite(x) ? x : 0.0);
            const double wide = gain * x;
            double out = split == 1.0 ? shelved : split == 0.0 ? wide : wide + split * (shelved - wide);
            if (listen != 0.0)
                out += listen * (band[static_cast<std::size_t>(ch)] - out);
            sample = static_cast<float>(out);
        }
    }
    for (int ch = 0; ch < numChannels; ++ch) {
        states_[static_cast<std::size_t>(ch)].flushDenormals();
        shelfStates_[static_cast<std::size_t>(ch)].flushDenormals();
    }
    reductionDb_.store(static_cast<float>(reduction_), std::memory_order_relaxed);
}

} // namespace vloom::dsp
