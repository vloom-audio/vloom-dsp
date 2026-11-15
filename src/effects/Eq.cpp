// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/effects/Eq.hpp"

#include <algorithm>
#include <cmath>

namespace vloom::dsp {

namespace {

[[nodiscard]] float octaveOf(double frequency) noexcept { return static_cast<float>(std::log2(frequency)); }
[[nodiscard]] double frequencyOf(float octave) noexcept { return std::exp2(static_cast<double>(octave)); }

} // namespace

void Eq::prepare(double sampleRate, int /*maxBlockFrames*/)
{
    sampleRate_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 0.0;
    const double controlRate = sampleRate_ / kControlFrames;
    for (Band& band : bands_) {
        band.octave.reset(controlRate, kRampSeconds, 0.0f);
        band.gain.reset(controlRate, kRampSeconds, 0.0f);
        band.q.reset(controlRate, kRampSeconds, 1.0f);
    }
    for (Cut& cut : cuts_) {
        cut.octave.reset(controlRate, kRampSeconds, 0.0f);
        cut.mix.reset(sampleRate_, kRampSeconds, 0.0f);
    }
    reset(); // starts at the values, no glide from old ones
}

void Eq::setParameter(std::uint32_t id, double value) noexcept
{
    if (id >= kParameterCount || !std::isfinite(value))
        return;
    const ParameterSpec& spec = kParameters[id];
    value = std::clamp(value, spec.minValue, spec.maxValue);
    values_[id] = spec.stepped ? std::round(value) : value;
    applyTargets(false);
    changed_ = true;
}

void Eq::reset() noexcept
{
    applyTargets(true);
    for (Band& band : bands_) {
        for (BiquadState& state : band.states)
            state.clear();
        update(band, true);
    }
    for (Cut& cut : cuts_) {
        for (BiquadState& state : cut.states)
            state.clear();
        update(cut, true);
    }
    untilControl_ = 0;
    changed_ = false;
}

void Eq::applyTargets(bool jump) noexcept
{
    const auto set = [jump](SmoothedValue& glide, float target) {
        if (jump)
            glide.setCurrentAndTarget(target);
        else
            glide.setTarget(target);
    };
    for (Band& band : bands_) {
        set(band.octave, octaveOf(value(band.frequencyId)));
        set(band.gain, static_cast<float>(value(band.gainId)));
        if (band.shape == Shape::Bell)
            set(band.q, static_cast<float>(value(band.qId)));
    }
    for (Cut& cut : cuts_) {
        set(cut.octave, octaveOf(value(cut.frequencyId)));
        set(cut.mix, value(cut.onId) >= 0.5 ? 1.0f : 0.0f);
    }
}

BiquadCoefficients Eq::design(const Band& band, double frequency, double gain, double q) const noexcept
{
    switch (band.shape) {
    case Shape::LowShelf:
        return biquad::lowShelf(frequency, sampleRate_, gain);
    case Shape::HighShelf:
        return biquad::highShelf(frequency, sampleRate_, gain);
    case Shape::Bell:
        break;
    }
    return biquad::peak(frequency, sampleRate_, gain, q);
}

BiquadCoefficients Eq::design(const Cut& cut, double frequency) const noexcept
{
    return cut.highPass ? biquad::highPass(frequency, sampleRate_) : biquad::lowPass(frequency, sampleRate_);
}

void Eq::update(Band& band, bool moved) noexcept
{
    const bool active = band.gain.current() != 0.0f || band.gain.target() != 0.0f;
    if (!active && band.active)
        for (BiquadState& state : band.states)
            state.clear();
    band.active = active;
    if (!moved || !active)
        return;
    const bool gliding = band.octave.isSmoothing() || band.gain.isSmoothing() || band.q.isSmoothing();
    // At rest: the exact values, not the glides' single precision.
    band.coefficients = gliding
        ? design(band, frequencyOf(band.octave.current()), band.gain.current(), band.q.current())
        : design(band, value(band.frequencyId), value(band.gainId), value(band.qId));
}

void Eq::update(Cut& cut, bool moved) noexcept
{
    if (moved)
        cut.coefficients = cut.octave.isSmoothing() ? design(cut, frequencyOf(cut.octave.current()))
                                                    : design(cut, value(cut.frequencyId));
}

void Eq::advance() noexcept
{
    for (Band& band : bands_) {
        const bool moving = band.octave.isSmoothing() || band.gain.isSmoothing() || band.q.isSmoothing();
        (void)band.octave.next();
        (void)band.gain.next();
        (void)band.q.next();
        // Off (0 dB) and switched on since: its filter is not designed yet.
        update(band, moving || changed_ || !band.active);
    }
    for (Cut& cut : cuts_) {
        const bool moving = cut.octave.isSmoothing();
        (void)cut.octave.next();
        update(cut, moving || changed_);
    }
    changed_ = false;
}

void Eq::run(Band& band, float* const* channels, int numChannels, int start, int count) noexcept
{
    if (!band.active)
        return;
    const BiquadCoefficients c = band.coefficients;
    for (int ch = 0; ch < numChannels; ++ch) {
        BiquadState state = band.states[static_cast<std::size_t>(ch)];
        float* const samples = channels[ch] + start;
        for (int i = 0; i < count; ++i)
            samples[i] = static_cast<float>(state.process(c, static_cast<double>(samples[i])));
        state.flushDenormals();
        band.states[static_cast<std::size_t>(ch)] = state;
    }
}

void Eq::run(Cut& cut, float* const* channels, int numChannels, int start, int count) noexcept
{
    const bool wanted = cut.mix.current() != 0.0f || cut.mix.target() != 0.0f;
    if (!wanted) {
        if (cut.active)
            for (BiquadState& state : cut.states)
                state.clear();
        cut.active = false;
        return;
    }
    cut.active = true;
    const BiquadCoefficients c = cut.coefficients;
    for (int i = 0; i < count; ++i) {
        const double mix = static_cast<double>(cut.mix.next());
        for (int ch = 0; ch < numChannels; ++ch) {
            float& sample = channels[ch][start + i];
            const double dry = static_cast<double>(sample);
            const double wet = cut.states[static_cast<std::size_t>(ch)].process(c, dry);
            sample = static_cast<float>(dry + mix * (wet - dry));
        }
    }
    for (int ch = 0; ch < numChannels; ++ch)
        cut.states[static_cast<std::size_t>(ch)].flushDenormals();
}

void Eq::process(float* const* channels, int numChannels, int numFrames) noexcept
{
    if (!(sampleRate_ > 0.0) || channels == nullptr)
        return;
    numChannels = std::min(numChannels, kMaxChannels);
    int start = 0;
    while (start < numFrames) {
        if (untilControl_ == 0) {
            advance();
            untilControl_ = kControlFrames;
        }
        const int count = std::min(numFrames - start, untilControl_);
        run(cuts_[0], channels, numChannels, start, count);
        for (Band& band : bands_)
            run(band, channels, numChannels, start, count);
        run(cuts_[1], channels, numChannels, start, count);
        start += count;
        untilControl_ -= count;
    }
}

double Eq::magnitudeDb(double frequency) const
{
    if (!(sampleRate_ > 0.0))
        return 0.0;
    double db = 0.0;
    for (const Band& band : bands_)
        if (value(band.gainId) != 0.0)
            db += design(band, value(band.frequencyId), value(band.gainId), value(band.qId))
                      .magnitudeDb(frequency, sampleRate_);
    for (const Cut& cut : cuts_)
        if (value(cut.onId) >= 0.5)
            db += design(cut, value(cut.frequencyId)).magnitudeDb(frequency, sampleRate_);
    return db;
}

} // namespace vloom::dsp
