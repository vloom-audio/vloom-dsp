// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/effects/Tremolo.hpp"

#include <algorithm>
#include <cmath>

namespace vloom::dsp {

namespace {

constexpr std::array<detail::LfoShape, Tremolo::kShapes> kLfoShapes{
    detail::LfoShape::Sine, detail::LfoShape::Triangle, detail::LfoShape::Square};

} // namespace

void Tremolo::prepare(double sampleRate, int /*maxBlockFrames*/)
{
    sampleRate_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 0.0;
    for (SmoothedValue& weight : shapeWeights_)
        weight.reset(sampleRate_, kRampSeconds, 0.0f);
    autoPan_.reset(sampleRate_, kRampSeconds, 0.0f);
    depth_.reset(sampleRate_, kRampSeconds, 0.0f);
    stereoPhase_.reset(sampleRate_, kRampSeconds, 0.0f);
    reset();
}

void Tremolo::setParameter(std::uint32_t id, double value) noexcept
{
    if (id >= kParameterCount || !std::isfinite(value))
        return;
    values_[id] = detail::clampToSpec(kParameters[id], value);
    applyTargets(false);
}

void Tremolo::setTempo(double bpm) noexcept
{
    if (std::isfinite(bpm) && bpm > 0.0)
        tempo_ = clampTempo(bpm);
}

double Tremolo::rateHz() const noexcept
{
    return detail::lfoRateHz(value(kSync) >= 0.5, value(kRate), static_cast<int>(value(kNote)), tempo_);
}

void Tremolo::applyTargets(bool jump) noexcept
{
    const auto set = [jump](SmoothedValue& glide, double target) {
        if (jump)
            glide.setCurrentAndTarget(static_cast<float>(target));
        else
            glide.setTarget(static_cast<float>(target));
    };
    const int shape = static_cast<int>(value(kShape));
    for (int s = 0; s < kShapes; ++s)
        set(shapeWeights_[static_cast<std::size_t>(s)], s == shape ? 1.0 : 0.0);
    set(autoPan_, value(kMode) >= 0.5 ? 1.0 : 0.0);
    set(depth_, value(kDepth) / 100.0);
    set(stereoPhase_, value(kStereoPhase) / 360.0);
}

void Tremolo::reset() noexcept
{
    applyTargets(true);
    lfo_.reset();
}

void Tremolo::process(float* const* channels, int numChannels, int numFrames) noexcept
{
    if (!(sampleRate_ > 0.0) || channels == nullptr || numFrames <= 0)
        return;
    numChannels = std::min(numChannels, kMaxChannels);
    lfo_.setRate(rateHz(), sampleRate_);

    const auto lfoAt = [](const std::array<float, kShapes>& weights, double phase) {
        float sum = 0.0f;
        for (std::size_t s = 0; s < weights.size(); ++s)
            if (weights[s] != 0.0f)
                sum += weights[s] * detail::lfoValue(kLfoShapes[s], phase);
        return sum;
    };

    for (int i = 0; i < numFrames; ++i) {
        std::array<float, kShapes> weights{};
        for (std::size_t s = 0; s < weights.size(); ++s)
            weights[s] = shapeWeights_[s].next();
        const float panMix = autoPan_.next();
        const float autoPan = numChannels == 2 ? panMix : 0.0f; // mono: a tremolo
        const float depth = depth_.next();
        const double offset = static_cast<double>(stereoPhase_.next());
        const double phase = lfo_.advance();

        const float left = lfoAt(weights, phase);
        std::array<float, kMaxChannels> gains{1.0f - depth * (0.5f - 0.5f * left), 1.0f};
        if (numChannels == 2) {
            const float right = offset == 0.0 ? left : lfoAt(weights, phase + offset);
            gains[1] = 1.0f - depth * (0.5f - 0.5f * right);
            if (autoPan != 0.0f) {
                // Balance: the side the sound moves away from goes down.
                const float pan = depth * left;
                const float panLeft = std::min(1.0f, 1.0f - pan);
                const float panRight = std::min(1.0f, 1.0f + pan);
                gains[0] += autoPan * (panLeft - gains[0]);
                gains[1] += autoPan * (panRight - gains[1]);
            }
        }
        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] *= gains[static_cast<std::size_t>(ch)];
    }
}

} // namespace vloom::dsp
