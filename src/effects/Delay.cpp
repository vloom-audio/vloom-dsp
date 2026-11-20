// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/effects/Delay.hpp"

#include <algorithm>
#include <cmath>

namespace vloom::dsp {

namespace {

struct Note {
    const char* name;
    double beats;
};

// Shortest to longest. The index is saved in projects: append only.
constexpr std::array<Note, Delay::kNoteCount> kNotes{{
    {"1/32", 0.125},
    {"1/16T", 1.0 / 6.0},
    {"1/16", 0.25},
    {"1/8T", 1.0 / 3.0},
    {"1/16D", 0.375},
    {"1/8", 0.5},
    {"1/4T", 2.0 / 3.0},
    {"1/8D", 0.75},
    {"1/4", 1.0},
    {"1/2T", 4.0 / 3.0},
    {"1/4D", 1.5},
    {"1/2", 2.0},
    {"1/1T", 8.0 / 3.0},
    {"1/2D", 3.0},
    {"1/1", 4.0},
}};

constexpr double kMinCut = 20.0; // the low cut is out of the path here
constexpr double kMaxCut = 20000.0; // and the high cut here
constexpr float kTiny = 1.0e-15f;

} // namespace

const char* Delay::noteName(int index) noexcept
{
    return index >= 0 && index < kNoteCount ? kNotes[static_cast<std::size_t>(index)].name : "";
}

double Delay::noteBeats(int index) noexcept
{
    return kNotes[static_cast<std::size_t>(std::clamp(index, 0, kNoteCount - 1))].beats;
}

double Delay::delaySeconds(bool right) const noexcept
{
    const double left = value(kSync) >= 0.5 ? noteBeats(static_cast<int>(value(kNote))) * 60.0 / tempo_
                                            : value(kTime) / 1000.0;
    return right ? left * (1.0 + value(kStereoOffset) / 100.0) : left;
}

void Delay::prepare(double sampleRate, int /*maxBlockFrames*/)
{
    sampleRate_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 0.0;
    for (Side& side : sides_) {
        side.line.prepare(static_cast<int>(std::ceil(kMaxSeconds * sampleRate_)) + 2);
        side.frames.reset(sampleRate_, kTimeRampSeconds, 1.0f);
    }
    feedback_.reset(sampleRate_, kRampSeconds, 0.0f);
    width_.reset(sampleRate_, kRampSeconds, 1.0f);
    mix_.reset(sampleRate_, kRampSeconds, 0.0f);
    reset();
}

void Delay::setParameter(std::uint32_t id, double value) noexcept
{
    if (id >= kParameterCount || !std::isfinite(value))
        return;
    values_[id] = clampToSpec(kParameters[id], value);
    applyValues(false);
}

void Delay::setTempo(double bpm) noexcept
{
    if (!std::isfinite(bpm) || !(bpm > 0.0))
        return;
    tempo_ = std::clamp(bpm, 1.0, 1000.0);
    applyValues(false);
}

void Delay::applyValues(bool jump) noexcept
{
    const auto set = [jump](SmoothedValue& glide, double target) {
        if (jump)
            glide.setCurrentAndTarget(static_cast<float>(target));
        else
            glide.setTarget(static_cast<float>(target));
    };
    const double longest
        = std::max(1.0, std::min(sides_[0].line.capacity() - 1.0, kMaxSeconds * sampleRate_));
    for (int side = 0; side < kMaxChannels; ++side) {
        const double frames = std::round(delaySeconds(side == 1) * sampleRate_ * 64.0) / 64.0;
        set(sides_[static_cast<std::size_t>(side)].frames, std::clamp(frames, 1.0, longest));
    }
    set(feedback_, value(kFeedback) / 100.0);
    set(width_, value(kWidth) / 100.0);
    set(mix_, value(kMix) / 100.0);

    const bool lowCut = value(kLowCut) > kMinCut;
    const bool highCut = value(kHighCut) < kMaxCut;
    if (lowCut && !lowCutOn_)
        for (Side& side : sides_)
            side.lowCut.clear();
    if (highCut && !highCutOn_)
        for (Side& side : sides_)
            side.highCut.clear();
    lowCutOn_ = lowCut;
    highCutOn_ = highCut;
    constexpr double butterworth = 0.7071067811865476;
    lowCut_ = svf::design(value(kLowCut), sampleRate_, butterworth);
    highCut_ = svf::design(value(kHighCut), sampleRate_, butterworth);
}

void Delay::reset() noexcept
{
    applyValues(true);
    for (Side& side : sides_) {
        side.line.clear();
        side.lowCut.clear();
        side.highCut.clear();
    }
}

float Delay::feed(Side& side, float sample) noexcept
{
    double x = static_cast<double>(sample);
    if (lowCutOn_)
        x = side.lowCut.process(lowCut_, x).high;
    if (highCutOn_)
        x = side.highCut.process(highCut_, x).low;
    auto y = static_cast<float>(x);
    if (!(std::abs(y) >= kTiny)) // also catches NaN, which must not loop
        y = 0.0f;
    side.line.push(y);
    return y;
}

void Delay::process(float* const* channels, int numChannels, int numFrames) noexcept
{
    if (!(sampleRate_ > 0.0) || channels == nullptr || numChannels < 1)
        return;
    Side& left = sides_[0];
    Side& right = sides_[1];

    if (numChannels == 1) {
        float* const samples = channels[0];
        for (int i = 0; i < numFrames; ++i) {
            const float echo = left.line.tapFractional(static_cast<double>(left.frames.next()));
            (void)right.frames.next();
            const float feedback = feedback_.next();
            (void)width_.next();
            const float mix = mix_.next();
            const float dry = samples[i];
            (void)feed(left, dry + feedback * echo);
            samples[i] = dry + mix * (echo - dry);
        }
    } else {
        const bool pingPong = value(kMode) >= 0.5;
        float* const l = channels[0];
        float* const r = channels[1];
        for (int i = 0; i < numFrames; ++i) {
            const float echoL = left.line.tapFractional(static_cast<double>(left.frames.next()));
            const float echoR = right.line.tapFractional(static_cast<double>(right.frames.next()));
            const float feedback = feedback_.next();
            const float width = width_.next();
            const float mix = mix_.next();
            const float dryL = l[i];
            const float dryR = r[i];
            if (pingPong) {
                (void)feed(left, 0.5f * (dryL + dryR) + feedback * echoR);
                (void)feed(right, feedback * echoL);
            } else {
                (void)feed(left, dryL + feedback * echoL);
                (void)feed(right, dryR + feedback * echoR);
            }
            const float mid = 0.5f * (echoL + echoR);
            const float side = 0.5f * (echoL - echoR) * width;
            l[i] = dryL + mix * (mid + side - dryL);
            r[i] = dryR + mix * (mid - side - dryR);
        }
    }
    for (Side& side : sides_) {
        side.lowCut.flushDenormals();
        side.highCut.flushDenormals();
    }
}

} // namespace vloom::dsp
