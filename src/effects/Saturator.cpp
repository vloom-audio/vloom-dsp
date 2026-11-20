// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/effects/Saturator.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vloom::dsp {

namespace {

// Tube: the negative half clips this much later than the positive one.
constexpr float kTubeAsymmetry = 0.6f;

} // namespace

float Saturator::shape(Curve curve, float x) noexcept
{
    switch (curve) {
    case Curve::Soft:
        return std::tanh(x);
    case Curve::Tube:
        return x >= 0.0f ? std::tanh(x) : std::tanh(x * kTubeAsymmetry) / kTubeAsymmetry;
    case Curve::Tape:
        return std::atan(x * std::numbers::pi_v<float> / 2.0f) * (2.0f / std::numbers::pi_v<float>);
    case Curve::Hard:
        break;
    }
    return std::clamp(x, -1.0f, 1.0f);
}

void Saturator::prepare(double sampleRate, int /*maxBlockFrames*/)
{
    sampleRate_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 0.0;
    for (Channel& channel : channels_)
        channel.dry.prepare(Oversampler::kLatency4x + 2);
    dcFactor_ = std::exp(-2.0 * std::numbers::pi * kDcBlockHz / std::max(sampleRate_, 1.0));
    const double g = std::tan(std::numbers::pi * std::min(kToneHz / std::max(sampleRate_, 1.0), 0.49));
    toneFactor_ = g / (1.0 + g);
    drive_.reset(sampleRate_, kRampSeconds, 1.0f);
    lowGain_.reset(sampleRate_, kRampSeconds, 1.0f);
    highGain_.reset(sampleRate_, kRampSeconds, 1.0f);
    output_.reset(sampleRate_, kRampSeconds, 1.0f);
    mix_.reset(sampleRate_, kRampSeconds, 1.0f);
    reset();
}

void Saturator::setParameter(std::uint32_t id, double value) noexcept
{
    if (id >= kParameterCount || !std::isfinite(value))
        return;
    const double before = values_[kOversampling];
    values_[id] = clampToSpec(kParameters[id], value);
    applyValues(false);
    if (values_[kOversampling] != before) {
        // A new latency: start the oversampler and the dry delay afresh.
        oversampler_.setFactor(values_[kOversampling] >= 0.5 ? 4 : 2);
        for (Channel& channel : channels_)
            channel.dry.clear();
    }
}

void Saturator::applyValues(bool jump) noexcept
{
    const auto set = [jump](SmoothedValue& glide, double target) {
        if (jump)
            glide.setCurrentAndTarget(static_cast<float>(target));
        else
            glide.setTarget(static_cast<float>(target));
    };
    set(drive_, decibelsToGain(value(kDrive)));
    const double tilt = value(kTone) / 100.0 * kToneDb;
    set(lowGain_, decibelsToGain(-tilt));
    set(highGain_, decibelsToGain(tilt));
    set(output_, decibelsToGain(value(kOutput)));
    set(mix_, value(kMix) / 100.0);
}

void Saturator::reset() noexcept
{
    applyValues(true);
    oversampler_.setFactor(value(kOversampling) >= 0.5 ? 4 : 2);
    for (Channel& channel : channels_) {
        channel.dry.clear();
        channel.dcIn = 0.0;
        channel.dcOut = 0.0;
        channel.low = 0.0;
    }
}

void Saturator::process(float* const* channels, int numChannels, int numFrames) noexcept
{
    if (!(sampleRate_ > 0.0) || channels == nullptr || numChannels < 1)
        return;
    numChannels = std::min(numChannels, kMaxChannels);
    const auto curve = static_cast<Curve>(static_cast<int>(value(kCurve)));
    const int factor = oversampler_.factor();
    const int delay = oversampler_.latencyFrames();

    for (int i = 0; i < numFrames; ++i) {
        const float drive = drive_.next();
        const auto lowGain = static_cast<double>(lowGain_.next());
        const auto highGain = static_cast<double>(highGain_.next());
        const auto output = static_cast<double>(output_.next());
        const float mix = mix_.next();
        for (int ch = 0; ch < numChannels; ++ch) {
            Channel& channel = channels_[static_cast<std::size_t>(ch)];
            float& sample = channels[ch][i];
            const float in = std::isfinite(sample) ? sample : 0.0f;
            channel.dry.push(in);
            const float dry = channel.dry.tap(delay + 1);

            std::array<float, Oversampler::kMaxFactor> fast{};
            oversampler_.upsample(ch, in * drive, fast.data());
            for (int k = 0; k < factor; ++k)
                fast[static_cast<std::size_t>(k)] = shape(curve, fast[static_cast<std::size_t>(k)]);
            const auto shaped = static_cast<double>(oversampler_.downsample(ch, fast.data()));

            // DC blocker: the tube curve is lopsided.
            const double blocked = shaped - channel.dcIn + dcFactor_ * channel.dcOut;
            channel.dcIn = shaped;
            channel.dcOut = blocked;
            // Tilt: a low-pass (one pole, trapezoidal: no gain left at
            // Nyquist) and what it leaves, each with its own gain.
            const double v = (blocked - channel.low) * toneFactor_;
            const double low = v + channel.low;
            channel.low = low + v;
            const double tilted = lowGain * low + highGain * (blocked - low);

            const auto wet = static_cast<float>(tilted * output);
            sample = dry + mix * (wet - dry);
        }
    }
    for (Channel& channel : channels_) {
        if (std::abs(channel.dcOut) < 1.0e-20)
            channel.dcOut = 0.0;
        if (std::abs(channel.low) < 1.0e-20)
            channel.low = 0.0;
    }
}

} // namespace vloom::dsp
