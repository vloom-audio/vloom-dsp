// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/effects/Compressor.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vloom::dsp {

namespace {

constexpr double kDbPerNeper = 20.0 / std::numbers::ln10; // 20 / ln 10
constexpr double kTinyDb = 1.0e-6;

} // namespace

double Compressor::staticGainDb(double inputDb, double thresholdDb, double ratio, double kneeDb) noexcept
{
    const double slope = 1.0 / std::max(ratio, 1.0) - 1.0; // <= 0
    const double over = inputDb - thresholdDb;
    if (kneeDb > 0.0 && 2.0 * std::abs(over) <= kneeDb) {
        const double into = over + kneeDb / 2.0;
        return slope * into * into / (2.0 * kneeDb);
    }
    return over > 0.0 ? slope * over : 0.0;
}

double Compressor::makeupDb() const noexcept
{
    double db = value(kMakeup);
    if (value(kAutoMakeup) >= 0.5)
        db -= 0.5 * staticGainDb(0.0, value(kThreshold), value(kRatio), value(kKnee));
    return db;
}

double Compressor::transferDb(double inputDb) const noexcept
{
    return inputDb + staticGainDb(inputDb, value(kThreshold), value(kRatio), value(kKnee)) + makeupDb();
}

void Compressor::prepare(double sampleRate, int /*maxBlockFrames*/)
{
    sampleRate_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 0.0;
    sidechain_.prepare(sampleRate_ > 0.0 ? sampleRate_ : 48000.0);
    makeup_.reset(sampleRate_, kRampSeconds, 0.0f);
    mix_.reset(sampleRate_, kRampSeconds, 1.0f);
    rmsFactor_ = onePoleFactor(kRmsSeconds, sampleRate_);
    reset();
}

void Compressor::setParameter(std::uint32_t id, double value) noexcept
{
    if (id >= kParameterCount || !std::isfinite(value))
        return;
    values_[id] = clampToSpec(kParameters[id], value);
    applyValues(false);
}

void Compressor::applyValues(bool jump) noexcept
{
    attackFactor_ = onePoleFactor(value(kAttack) / 1000.0, sampleRate_);
    releaseFactor_ = onePoleFactor(value(kRelease) / 1000.0, sampleRate_);
    kneeStartGain_ = decibelsToGain(value(kThreshold) - value(kKnee) / 2.0);
    sidechain_.set(value(kSidechainFilter) >= 0.5, value(kSidechainFrequency));
    const auto makeup = static_cast<float>(makeupDb());
    const auto mix = static_cast<float>(value(kMix) / 100.0);
    if (jump) {
        makeup_.setCurrentAndTarget(makeup);
        mix_.setCurrentAndTarget(mix);
    } else {
        makeup_.setTarget(makeup);
        mix_.setTarget(mix);
    }
}

void Compressor::reset() noexcept
{
    applyValues(true);
    sidechain_.clear();
    meanSquare_ = {};
    reductionDb_ = {};
    gainReduction_.store(0.0f);
}

void Compressor::process(float* const* channels, int numChannels, int numFrames) noexcept
{
    run(channels, nullptr, numChannels, 0, numFrames);
}

void Compressor::processWithSidechain(float* const* channels, const float* const* key, int numChannels,
    int numKeyChannels, int numFrames) noexcept
{
    run(channels, key, numChannels, numKeyChannels, numFrames);
}

void Compressor::run(float* const* channels, const float* const* key, int numChannels, int numKeyChannels,
    int numFrames) noexcept
{
    if (!(sampleRate_ > 0.0) || channels == nullptr || numChannels < 1)
        return;
    numChannels = std::min(numChannels, kMaxChannels);
    if (SidechainFilter::keyChannel(key, numKeyChannels, 0) == nullptr)
        numKeyChannels = 0;
    // The detector has as many channels as the input or the key, whichever
    // has more (up to two), so a stereo key also drives a mono track fully.
    const int detectors
        = numKeyChannels > 0 ? std::min(std::max(numChannels, numKeyChannels), kMaxChannels) : numChannels;
    const auto source = [&](int ch) -> const float* {
        if (numKeyChannels > 0)
            return SidechainFilter::keyChannel(key, numKeyChannels, ch);
        return channels[std::min(ch, numChannels - 1)];
    };

    const double threshold = value(kThreshold);
    const double ratio = value(kRatio);
    const double knee = value(kKnee);
    const double link = value(kStereoLink) / 100.0;
    const bool rms = value(kDetector) >= 0.5;
    const bool listen = value(kSidechainListen) >= 0.5;
    double most = 0.0; // the deepest reduction this block, dB > 0

    for (int i = 0; i < numFrames; ++i) {
        std::array<double, kMaxChannels> keyed{};
        std::array<double, kMaxChannels> level{};
        double loudest = 0.0;
        for (int ch = 0; ch < detectors; ++ch) {
            const auto c = static_cast<std::size_t>(ch);
            const float in = source(ch)[i];
            keyed[c] = static_cast<double>(sidechain_.process(ch, std::isfinite(in) ? in : 0.0f));
            if (rms) {
                const double square = keyed[c] * keyed[c];
                meanSquare_[c] = square + rmsFactor_ * (meanSquare_[c] - square);
                level[c] = std::sqrt(meanSquare_[c]);
            } else {
                level[c] = std::abs(keyed[c]);
            }
            loudest = std::max(loudest, level[c]);
        }
        const double makeup = static_cast<double>(makeup_.next());
        const auto mix = static_cast<double>(mix_.next());
        for (int ch = 0; ch < numChannels; ++ch) {
            const auto c = static_cast<std::size_t>(ch);
            const double own = detectors > ch ? level[c] : level[0];
            const double linked = own + link * (loudest - own);
            const double target = linked > kneeStartGain_
                ? staticGainDb(kDbPerNeper * std::log(linked), threshold, ratio, knee)
                : 0.0;
            double& reduction = reductionDb_[c];
            const double factor = target < reduction ? attackFactor_ : releaseFactor_;
            reduction = target + factor * (reduction - target);
            most = std::max(most, -reduction);

            float& sample = channels[ch][i];
            if (listen) {
                sample = static_cast<float>(detectors > ch ? keyed[c] : keyed[0]);
                continue;
            }
            const double dry = static_cast<double>(sample);
            const double wet = dry * std::exp((reduction + makeup) / kDbPerNeper);
            sample = static_cast<float>(dry + mix * (wet - dry));
        }
    }

    for (double& reduction : reductionDb_)
        if (std::abs(reduction) < kTinyDb)
            reduction = 0.0;
    for (double& square : meanSquare_)
        if (square < 1.0e-30)
            square = 0.0;
    sidechain_.flushDenormals();
    gainReduction_.store(static_cast<float>(most));
}

} // namespace vloom::dsp
