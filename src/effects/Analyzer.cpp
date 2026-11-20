// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/effects/Analyzer.hpp"

#include <algorithm>
#include <cmath>

namespace vloom::dsp {

void Analyzer::prepare(double sampleRate, int /*maxBlockFrames*/)
{
    sampleRate_ = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 0.0;
    reset();
}

void Analyzer::setParameter(std::uint32_t id, double value) noexcept
{
    if (id >= kParameterCount || !std::isfinite(value))
        return;
    values_[id] = clampToSpec(kParameters[id], value);
}

void Analyzer::reset() noexcept
{
    peakDb_ = kFloorDb;
    holdLeft_ = 0.0;
    readouts_.clear();
    if (sampleRate_ > 0.0)
        readouts_.store(kReadoutPeak, static_cast<float>(kFloorDb));
}

void Analyzer::process(float* const* channels, int numChannels, int numFrames) noexcept
{
    if (!(sampleRate_ > 0.0) || channels == nullptr || numChannels < 1 || numFrames < 1)
        return;
    if (value(kFreeze) >= 0.5)
        return; // the meter stands still with the picture

    const auto channel = static_cast<Channel>(static_cast<int>(value(kChannel)));
    const int firstChannel = channel == Channel::Right && numChannels > 1 ? 1 : 0;
    const int lastChannel = channel == Channel::Both ? std::min(numChannels, 2) - 1 : firstChannel;
    float most = 0.0f;
    for (int ch = firstChannel; ch <= lastChannel; ++ch) {
        for (int i = 0; i < numFrames; ++i) {
            const float sample = std::abs(channels[ch][i]);
            if (std::isfinite(sample))
                most = std::max(most, sample);
        }
    }
    const double blockDb
        = most > 0.0f ? std::max(kFloorDb, 20.0 * std::log10(static_cast<double>(most))) : kFloorDb;

    // Held, then falling: a new high resets the hold.
    double seconds = static_cast<double>(numFrames) / sampleRate_;
    if (blockDb >= peakDb_) {
        peakDb_ = blockDb;
        holdLeft_ = kPeakHoldSeconds;
    } else {
        const double held = std::min(holdLeft_, seconds);
        holdLeft_ -= held;
        seconds -= held;
        peakDb_ = std::max({blockDb, kFloorDb, peakDb_ - kPeakFallDbPerSecond * seconds});
    }
    readouts_.store(kReadoutPeak, static_cast<float>(peakDb_));
}

double Analyzer::tiltDb(double hz, double dbPerOctave) noexcept
{
    if (!(hz > 0.0) || !std::isfinite(hz) || !std::isfinite(dbPerOctave))
        return 0.0;
    return dbPerOctave * std::log2(hz / kTiltPivotHz);
}

} // namespace vloom::dsp
