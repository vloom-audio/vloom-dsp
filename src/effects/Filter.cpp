// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/effects/Filter.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

namespace vloom::dsp {

namespace {

// The Qs of a fourth-order Butterworth filter, as two sections.
constexpr double kButterworthFirstQ = 0.5411961001461970;
constexpr double kButterworthSecondQ = 1.3065629648763766;
constexpr double kDriveFadeDb = 6.0;

struct Mix {
    float low;
    float band;
    float high;
};

[[nodiscard]] Mix mixOf(Filter::Type type) noexcept
{
    switch (type) {
    case Filter::Type::LowPass:
        return {1.0f, 0.0f, 0.0f};
    case Filter::Type::HighPass:
        return {0.0f, 0.0f, 1.0f};
    case Filter::Type::BandPass:
        return {0.0f, 1.0f, 0.0f};
    case Filter::Type::Notch:
        break;
    }
    return {1.0f, 0.0f, 1.0f};
}

[[nodiscard]] double qOf(double resonancePercent) noexcept
{
    return Filter::kMinQ * std::pow(Filter::kMaxQ / Filter::kMinQ, resonancePercent / 100.0);
}

} // namespace

Filter::Design Filter::design(double frequency, double q) const noexcept
{
    return {svf::design(frequency, sampleRate_, q), svf::design(frequency, sampleRate_, kButterworthFirstQ),
        svf::design(frequency, sampleRate_, kButterworthSecondQ * q / kMinQ)};
}

void Filter::prepare(double sampleRate, int /*maxBlockFrames*/)
{
    sampleRate_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 0.0;
    for (SmoothedValue& glide : glides_)
        glide.reset(sampleRate_, kRampSeconds, 0.0f);
    reset();
}

void Filter::setParameter(std::uint32_t id, double value) noexcept
{
    if (id >= kParameterCount || !std::isfinite(value))
        return;
    values_[id] = clampToSpec(kParameters[id], value);
    applyValues(false);
}

void Filter::applyValues(bool jump) noexcept
{
    const Mix mix = mixOf(static_cast<Type>(static_cast<int>(value(kType))));
    const double drive = value(kDrive);
    std::array<float, kGlides> t{};
    t[kOctave] = static_cast<float>(std::log2(value(kCutoff)));
    t[kQ] = static_cast<float>(qOf(value(kResonance)));
    t[kLow] = mix.low;
    t[kBand] = mix.band;
    t[kHigh] = mix.high;
    t[kSteep] = value(kSlope) >= 0.5 ? 1.0f : 0.0f;
    t[kDriveAmount] = static_cast<float>(std::min(drive / kDriveFadeDb, 1.0));
    t[kDriveGain] = static_cast<float>(decibelsToGain(drive));
    for (std::size_t g = 0; g < kGlides; ++g) {
        if (jump)
            glides_[g].setCurrentAndTarget(t[g]);
        else
            glides_[g].setTarget(t[g]);
    }
    if (jump || !(glides_[kOctave].isSmoothing() || glides_[kQ].isSmoothing()))
        design_ = design(value(kCutoff), qOf(value(kResonance)));
}

void Filter::reset() noexcept
{
    applyValues(true);
    for (Channel& channel : channels_)
        channel = {};
}

void Filter::process(float* const* channels, int numChannels, int numFrames) noexcept
{
    if (!(sampleRate_ > 0.0) || channels == nullptr || numChannels < 1)
        return;
    numChannels = std::min(numChannels, kMaxChannels);

    for (int i = 0; i < numFrames; ++i) {
        const bool moving = glides_[kOctave].isSmoothing() || glides_[kQ].isSmoothing();
        std::array<double, kGlides> v{};
        for (std::size_t g = 0; g < kGlides; ++g)
            v[g] = static_cast<double>(glides_[g].next());
        if (moving) {
            // At rest: the exact values, not the glides' single precision.
            design_ = glides_[kOctave].isSmoothing() || glides_[kQ].isSmoothing()
                ? design(std::exp2(v[kOctave]), v[kQ])
                : design(value(kCutoff), qOf(value(kResonance)));
        }
        const bool steep = v[kSteep] > 0.0;
        const bool shallow = v[kSteep] < 1.0;
        for (int ch = 0; ch < numChannels; ++ch) {
            Channel& channel = channels_[static_cast<std::size_t>(ch)];
            float& sample = channels[ch][i];
            double x = std::isfinite(sample) ? static_cast<double>(sample) : 0.0;
            if (v[kDriveAmount] > 0.0)
                x += v[kDriveAmount] * (std::tanh(v[kDriveGain] * x) - x);

            const auto combine = [&](const SvfOutputs& o, const SvfCoefficients& c) {
                return v[kLow] * o.low + v[kBand] * c.k * o.band + v[kHigh] * o.high;
            };
            double y12 = 0.0;
            double y24 = 0.0;
            if (shallow)
                y12 = combine(channel.single.process(design_.single, x), design_.single);
            if (steep) {
                const double half = combine(channel.first.process(design_.first, x), design_.first);
                y24 = combine(channel.second.process(design_.second, half), design_.second);
            }
            sample = static_cast<float>(y12 + v[kSteep] * (y24 - y12));
        }
    }
    const bool steep = glides_[kSteep].current() > 0.0f;
    const bool shallow = glides_[kSteep].current() < 1.0f;
    for (Channel& channel : channels_) {
        // A section out of use starts from silence when it comes back.
        if (!shallow)
            channel.single.clear();
        if (!steep) {
            channel.first.clear();
            channel.second.clear();
        }
        channel.single.flushDenormals();
        channel.first.flushDenormals();
        channel.second.flushDenormals();
    }
}

double Filter::magnitudeDb(double frequency) const
{
    if (!(sampleRate_ > 0.0))
        return 0.0;
    const Mix mix = mixOf(static_cast<Type>(static_cast<int>(value(kType))));
    const Design d = design(value(kCutoff), qOf(value(kResonance)));
    const auto section = [&](const SvfCoefficients& c) {
        const svf::Response r = svf::response(c, frequency, sampleRate_);
        return static_cast<double>(mix.low) * r.low + static_cast<double>(mix.band) * c.k * r.band
            + static_cast<double>(mix.high) * r.high;
    };
    const std::complex<double> h
        = value(kSlope) >= 0.5 ? section(d.first) * section(d.second) : section(d.single);
    return 20.0 * std::log10(std::max(std::abs(h), 1.0e-12));
}

} // namespace vloom::dsp
