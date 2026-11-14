// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/effects/Gain.hpp"

#include <algorithm>
#include <cmath>

namespace vloom::dsp {

void Gain::prepare(double sampleRate, int /*maxBlockFrames*/)
{
    gain_.reset(sampleRate, kRampSeconds, linear(db_)); // no ramp from an old value
}

void Gain::setParameter(std::uint32_t id, double value) noexcept
{
    if (id != kGain || !std::isfinite(value))
        return;
    db_ = std::clamp(value, kMinDb, kMaxDb);
    gain_.setTarget(linear(db_));
}

void Gain::process(float* const* channels, int numChannels, int numFrames) noexcept
{
    if (!gain_.isSmoothing()) {
        const float gain = gain_.current();
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numFrames; ++i)
                channels[ch][i] *= gain;
        return;
    }
    for (int i = 0; i < numFrames; ++i) {
        const float gain = gain_.next();
        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] *= gain;
    }
}

void Gain::reset() noexcept { gain_.setCurrentAndTarget(linear(db_)); }

float Gain::linear(double db) noexcept
{
    if (!(db > kMinDb))
        return 0.0f;
    return static_cast<float>(std::pow(10.0, std::min(db, kMaxDb) / 20.0));
}

} // namespace vloom::dsp
