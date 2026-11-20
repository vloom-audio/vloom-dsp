// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/effects/Gate.hpp"

#include <algorithm>
#include <cmath>

namespace vloom::dsp {

void Gate::prepare(double sampleRate, int /*maxBlockFrames*/)
{
    sampleRate_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 0.0;
    sidechain_.prepare(sampleRate_ > 0.0 ? sampleRate_ : 48000.0);
    detectorFactor_ = onePoleFactor(kDetectorReleaseSeconds, sampleRate_);
    reset();
}

void Gate::setParameter(std::uint32_t id, double value) noexcept
{
    if (id >= kParameterCount || !std::isfinite(value))
        return;
    values_[id] = clampToSpec(kParameters[id], value);
    applyValues();
}

void Gate::applyValues() noexcept
{
    openLevel_ = decibelsToGain(value(kThreshold));
    closeLevel_ = decibelsToGain(value(kThreshold) - value(kHysteresis));
    closedGain_ = value(kRange) > kMinRangeDb ? decibelsToGain(value(kRange)) : 0.0;
    attackFactor_ = onePoleFactor(value(kAttack) / 1000.0, sampleRate_);
    releaseFactor_ = onePoleFactor(value(kRelease) / 1000.0, sampleRate_);
    holdFrames_ = static_cast<int>(std::lround(value(kHold) / 1000.0 * sampleRate_));
    sidechain_.set(value(kSidechainFilter) >= 0.5, value(kSidechainFrequency));
}

void Gate::reset() noexcept
{
    applyValues();
    sidechain_.clear();
    envelope_ = 0.0;
    gateOpen_ = false; // starts closed: the first sound opens it
    gain_ = closedGain_;
    holdLeft_ = 0;
    gainReduction_.store(0.0f);
    open_.store(false);
}

void Gate::process(float* const* channels, int numChannels, int numFrames) noexcept
{
    run(channels, nullptr, numChannels, 0, numFrames);
}

void Gate::processWithSidechain(float* const* channels, const float* const* key, int numChannels,
    int numKeyChannels, int numFrames) noexcept
{
    run(channels, key, numChannels, numKeyChannels, numFrames);
}

void Gate::run(float* const* channels, const float* const* key, int numChannels, int numKeyChannels,
    int numFrames) noexcept
{
    if (!(sampleRate_ > 0.0) || channels == nullptr || numChannels < 1)
        return;
    numChannels = std::min(numChannels, kMaxChannels);
    if (SidechainFilter::keyChannel(key, numKeyChannels, 0) == nullptr)
        numKeyChannels = 0;
    const int detectors = numKeyChannels > 0 ? std::min(numKeyChannels, kMaxChannels) : numChannels;
    const auto source = [&](int ch) -> const float* {
        return numKeyChannels > 0 ? SidechainFilter::keyChannel(key, numKeyChannels, ch) : channels[ch];
    };
    const bool listen = value(kSidechainListen) >= 0.5;
    double lowest = 1.0; // the lowest gain this block

    for (int i = 0; i < numFrames; ++i) {
        std::array<float, kMaxChannels> keyed{};
        double peak = 0.0;
        for (int ch = 0; ch < detectors; ++ch) {
            const float in = source(ch)[i];
            keyed[static_cast<std::size_t>(ch)] = sidechain_.process(ch, std::isfinite(in) ? in : 0.0f);
            peak = std::max(peak, static_cast<double>(std::abs(keyed[static_cast<std::size_t>(ch)])));
        }
        envelope_ = peak > envelope_ ? peak : peak + detectorFactor_ * (envelope_ - peak);

        if (envelope_ > openLevel_) {
            gateOpen_ = true;
            holdLeft_ = holdFrames_;
        } else if (gateOpen_) {
            if (envelope_ >= closeLevel_)
                holdLeft_ = holdFrames_;
            else if (holdLeft_ > 0)
                --holdLeft_;
            else
                gateOpen_ = false;
        }
        const double target = gateOpen_ ? 1.0 : closedGain_;
        gain_ = target + (target > gain_ ? attackFactor_ : releaseFactor_) * (gain_ - target);
        if (std::abs(gain_ - target) < 1.0e-9)
            gain_ = target; // lands, and never decays into denormals
        lowest = std::min(lowest, gain_);

        const auto gain = static_cast<float>(gain_);
        for (int ch = 0; ch < numChannels; ++ch) {
            float& sample = channels[ch][i];
            sample = listen ? keyed[static_cast<std::size_t>(std::min(ch, detectors - 1))] : sample * gain;
        }
    }
    if (envelope_ < 1.0e-30)
        envelope_ = 0.0;
    sidechain_.flushDenormals();
    const double reduction = lowest > 0.0 ? -20.0 * std::log10(lowest) : -kMinRangeDb;
    gainReduction_.store(static_cast<float>(std::min(reduction, -kMinRangeDb)));
    open_.store(gateOpen_);
}

} // namespace vloom::dsp
