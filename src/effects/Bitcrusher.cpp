// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/effects/Bitcrusher.hpp"

#include <algorithm>
#include <cmath>

namespace vloom::dsp {

namespace {

// The two sections of a 4th-order Butterworth low-pass.
constexpr double kButterworthQ1 = 0.54119610014619698;
constexpr double kButterworthQ2 = 1.3065629648763766;

} // namespace

double Bitcrusher::stepFor(double bits) noexcept
{
    return std::exp2(1.0 - std::clamp(std::isfinite(bits) ? bits : kMaxBits, kMinBits, kMaxBits));
}

void Bitcrusher::prepare(double sampleRate, int /*maxBlockFrames*/)
{
    sampleRate_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 0.0;
    drive_.reset(sampleRate_, kRampSeconds, 1.0f);
    bits_.reset(sampleRate_, kRampSeconds, 24.0f);
    downsample_.reset(sampleRate_, kRampSeconds, 1.0f);
    jitter_.reset(sampleRate_, kRampSeconds, 0.0f);
    dither_.reset(sampleRate_, kRampSeconds, 0.0f);
    antiAlias_.reset(sampleRate_, kRampSeconds, 0.0f);
    output_.reset(sampleRate_, kRampSeconds, 1.0f);
    mix_.reset(sampleRate_, kRampSeconds, 1.0f);
    reset();
}

void Bitcrusher::setParameter(std::uint32_t id, double value) noexcept
{
    if (id >= kParameterCount || !std::isfinite(value))
        return;
    values_[id] = detail::clampToSpec(kParameters[id], value);
    applyTargets(false);
}

void Bitcrusher::applyTargets(bool jump) noexcept
{
    const auto set = [jump](SmoothedValue& glide, double target) {
        if (jump)
            glide.setCurrentAndTarget(static_cast<float>(target));
        else
            glide.setTarget(static_cast<float>(target));
    };
    set(drive_, std::pow(10.0, value(kDrive) / 20.0));
    set(bits_, value(kBits));
    set(downsample_, value(kDownsample));
    set(jitter_, value(kJitter) / 100.0);
    set(dither_, value(kDither) / 100.0);
    set(antiAlias_, value(kAntiAlias) >= 0.5 ? 1.0 : 0.0);
    set(output_, std::pow(10.0, value(kOutput) / 20.0));
    set(mix_, value(kMix) / 100.0);
}

void Bitcrusher::updateFilter() noexcept
{
    const double factor = std::max(1.0, static_cast<double>(downsample_.current()));
    // Just below the Nyquist frequency of the reduced rate.
    const double cutoff = std::max(20.0, 0.45 * sampleRate_ / factor);
    filter_[0] = biquad::lowPass(cutoff, sampleRate_, kButterworthQ1);
    filter_[1] = biquad::lowPass(cutoff, sampleRate_, kButterworthQ2);
}

void Bitcrusher::reset() noexcept
{
    applyTargets(true);
    for (Channel& channel : channels_)
        channel = {};
    random_.reset();
    holdFrames_ = std::max(1.0, value(kDownsample));
    hold_ = holdFrames_ - 1.0; // the first frame takes a new sample
    untilControl_ = 0;
    if (sampleRate_ > 0.0)
        updateFilter();
}

void Bitcrusher::process(float* const* channels, int numChannels, int numFrames) noexcept
{
    if (!(sampleRate_ > 0.0) || channels == nullptr || numFrames <= 0)
        return;
    numChannels = std::min(numChannels, kMaxChannels);

    for (int i = 0; i < numFrames; ++i) {
        if (untilControl_ == 0) {
            updateFilter();
            untilControl_ = kControlFrames;
        }
        --untilControl_;

        const float drive = drive_.next();
        const float bits = bits_.next();
        const auto factor = static_cast<double>(downsample_.next());
        const auto jitter = static_cast<double>(jitter_.next());
        const float dither = dither_.next();
        const float antiAlias = antiAlias_.next();
        const bool filtering = antiAlias != 0.0f || antiAlias_.target() != 0.0f;
        const float output = output_.next();
        const float mix = mix_.next();

        // A new sample once the hold is over.
        hold_ += 1.0;
        const bool take = hold_ >= holdFrames_;
        if (take) {
            hold_ = std::min(hold_ - holdFrames_, 1.0);
            const double spread = jitter * (static_cast<double>(random_.next()) - 0.5);
            holdFrames_ = std::max(1.0, factor) * (1.0 + spread);
        }
        const bool quantize = bits < static_cast<float>(kMaxBits);
        const auto step = static_cast<float>(stepFor(static_cast<double>(bits)));

        for (int ch = 0; ch < numChannels; ++ch) {
            Channel& channel = channels_[static_cast<std::size_t>(ch)];
            float& sample = channels[ch][i];
            const float dry = sample;
            if (filtering) {
                double x = static_cast<double>(detail::sanitize(dry * drive));
                const double filtered
                    = channel.filter[1].process(filter_[1], channel.filter[0].process(filter_[0], x));
                x += static_cast<double>(antiAlias) * (filtered - x);
                if (take)
                    channel.held = static_cast<float>(x);
            } else if (take) {
                channel.held = dry * drive;
            }
            if (take && quantize) {
                float x = std::clamp(channel.held, -1.0f, 1.0f); // NaN stays NaN; fine, it is not kept
                if (dither != 0.0f)
                    x += dither * step * (random_.next() - random_.next());
                channel.held = std::clamp(std::round(x / step) * step, -1.0f, 1.0f);
            }
            sample = (dry + mix * (channel.held - dry)) * output;
        }
    }
    for (Channel& channel : channels_)
        for (BiquadState& state : channel.filter)
            state.flushDenormals();
    if (antiAlias_.current() == 0.0f && antiAlias_.target() == 0.0f)
        for (Channel& channel : channels_)
            for (BiquadState& state : channel.filter)
                state.clear();
}

} // namespace vloom::dsp
