// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/effects/Reverb.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vloom::dsp {

namespace {

// Line lengths at a size factor of 1, in ms: spread out and with no simple
// ratios between them, so their echoes do not line up.
constexpr std::array<double, Reverb::kLines> kLineMs{29.71, 33.83, 37.97, 41.03, 44.69, 49.31, 53.87, 59.11};
// The modulation rate of each line, in Hz.
constexpr std::array<double, Reverb::kLines> kModulationHz{0.37, 0.43, 0.51, 0.59, 0.67, 0.73, 0.83, 0.97};
// The input all-passes of each side, in ms.
constexpr std::array<std::array<double, Reverb::kDiffusers>, 2> kDiffuserMs{{
    {4.771, 3.595, 12.73, 9.307},
    {4.539, 3.731, 12.07, 9.887},
}};
constexpr double kMinScale = 0.3; // size 0 %
constexpr double kMaxScale = 2.0; // size 100 %
constexpr double kModulationSeconds = 0.0003;
constexpr double kMaxDiffusion = 0.7; // the all-pass coefficient at 100 %
constexpr double kOutputGain = 0.6;
// The damping low-pass in the loop: from this frequency at 0 % ...
constexpr double kDampingFromHz = 20000.0;
// ... to this one at 100 %.
constexpr double kDampingToHz = 1500.0;
constexpr float kTiny = 1.0e-15f;

[[nodiscard]] float flushed(double value) noexcept
{
    const auto v = static_cast<float>(value);
    return std::abs(v) >= kTiny ? v : 0.0f; // also turns NaN into 0
}

[[nodiscard]] double scaleOf(double sizePercent) noexcept
{
    return kMinScale * std::pow(kMaxScale / kMinScale, sizePercent / 100.0);
}

} // namespace

void Reverb::prepare(double sampleRate, int /*maxBlockFrames*/)
{
    sampleRate_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 0.0;
    for (std::size_t side = 0; side < inputs_.size(); ++side) {
        Input& input = inputs_[side];
        input.preDelay.prepare(static_cast<int>(std::ceil(kMaxPreDelaySeconds * sampleRate_)) + 2);
        for (std::size_t k = 0; k < kDiffusers; ++k) {
            Diffuser& diffuser = input.diffusers[k];
            diffuser.frames
                = std::max(1, static_cast<int>(std::lround(kDiffuserMs[side][k] / 1000.0 * sampleRate_)));
            diffuser.line.prepare(diffuser.frames + 1);
        }
    }
    const double longest = kLineMs.back() / 1000.0 * kMaxScale + 2.0 * kModulationSeconds;
    for (std::size_t i = 0; i < kLines; ++i) {
        Line& line = lines_[i];
        line.line.prepare(static_cast<int>(std::ceil(longest * sampleRate_)) + 4);
        const double turn = sampleRate_ > 0.0 ? 2.0 * std::numbers::pi * kModulationHz[i] / sampleRate_ : 0.0;
        line.turnCos = std::cos(turn);
        line.turnSin = std::sin(turn);
    }
    const double controlRate = sampleRate_ / kControlFrames;
    preDelay_.reset(sampleRate_, kRampSeconds, 0.0f);
    size_.reset(controlRate, 0.2, 1.0f);
    freeze_.reset(controlRate, kRampSeconds, 0.0f);
    diffusion_.reset(sampleRate_, kRampSeconds, 0.0f);
    width_.reset(sampleRate_, kRampSeconds, 1.0f);
    mix_.reset(sampleRate_, kRampSeconds, 0.0f);
    reset();
}

void Reverb::setParameter(std::uint32_t id, double value) noexcept
{
    if (id >= kParameterCount || !std::isfinite(value))
        return;
    values_[id] = clampToSpec(kParameters[id], value);
    applyValues(false);
}

void Reverb::applyValues(bool jump) noexcept
{
    const auto set = [jump](SmoothedValue& glide, double target) {
        if (jump)
            glide.setCurrentAndTarget(static_cast<float>(target));
        else
            glide.setTarget(static_cast<float>(target));
    };
    set(preDelay_, value(kPreDelay) / 1000.0 * sampleRate_);
    set(size_, scaleOf(value(kSize)));
    set(freeze_, value(kFreeze) >= 0.5 ? 1.0 : 0.0);
    set(diffusion_, value(kDiffusion) / 100.0 * kMaxDiffusion);
    set(width_, value(kWidth) / 100.0);
    set(mix_, value(kMix) / 100.0);
    constexpr double butterworth = 0.7071067811865476;
    lowCutCoefficients_ = svf::design(value(kLowCut), sampleRate_, butterworth);
    highCutCoefficients_ = svf::design(value(kHighCut), sampleRate_, butterworth);
    changed_ = true;
}

void Reverb::reset() noexcept
{
    applyValues(true);
    for (Input& input : inputs_) {
        input.preDelay.clear();
        for (Diffuser& diffuser : input.diffusers)
            diffuser.line.clear();
    }
    for (Line& line : lines_) {
        line.line.clear();
        line.damped = 0.0;
        line.cos = 1.0;
        line.sin = 0.0;
    }
    for (SvfState& state : lowCut_)
        state.clear();
    for (SvfState& state : highCut_)
        state.clear();
    untilControl_ = 0;
    control();
}

void Reverb::control() noexcept
{
    const bool moving = size_.isSmoothing() || freeze_.isSmoothing();
    const double scale = static_cast<double>(size_.next());
    const double freeze = static_cast<double>(freeze_.next());
    if (!moving && !changed_)
        return;
    changed_ = false;
    const double decay = value(kDecay);
    for (std::size_t i = 0; i < kLines; ++i) {
        Line& line = lines_[i];
        line.frames = std::max(1.0, std::round(kLineMs[i] / 1000.0 * scale * sampleRate_));
        // Falls by 60 dB in `decay` seconds: -60 dB * (length / decay) per trip.
        const double loss = std::pow(10.0, -3.0 * line.frames / (sampleRate_ * decay));
        line.gain = loss + freeze * (1.0 - loss);
    }
    const double cutoff = kDampingFromHz * std::pow(kDampingToHz / kDampingFromHz, value(kDamping) / 100.0);
    damping_ = std::exp(-2.0 * std::numbers::pi * cutoff / sampleRate_) * (1.0 - freeze);
    modulationFrames_ = kModulationSeconds * sampleRate_ * (1.0 - freeze);
}

float Reverb::diffuse(Input& input, float sample, double coefficient) noexcept
{
    double x = static_cast<double>(sample);
    for (Diffuser& diffuser : input.diffusers) {
        const double delayed = static_cast<double>(diffuser.line.tap(diffuser.frames));
        const double v = x + coefficient * delayed;
        x = delayed - coefficient * v;
        diffuser.line.push(flushed(v));
    }
    return static_cast<float>(x);
}

void Reverb::process(float* const* channels, int numChannels, int numFrames) noexcept
{
    if (!(sampleRate_ > 0.0) || channels == nullptr || numChannels < 1)
        return;
    const bool stereo = numChannels >= 2;
    const double norm = 1.0 / std::sqrt(static_cast<double>(kLines));

    for (int i = 0; i < numFrames; ++i) {
        if (untilControl_ == 0) {
            control();
            untilControl_ = kControlFrames;
        }
        --untilControl_;

        const double preDelay = 1.0 + static_cast<double>(preDelay_.next());
        const double diffusion = static_cast<double>(diffusion_.next());
        const float width = width_.next();
        const float mix = mix_.next();
        const double open = 1.0 - static_cast<double>(freeze_.current()); // input while freezing: none

        const float dryL = channels[0][i];
        const float dryR = stereo ? channels[1][i] : dryL;
        std::array<double, 2> fed{};
        for (std::size_t side = 0; side < 2; ++side) {
            Input& input = inputs_[side];
            input.preDelay.push(flushed(side == 0 ? dryL : dryR));
            const float delayed = input.preDelay.tapFractional(preDelay);
            fed[side] = static_cast<double>(diffuse(input, delayed, diffusion)) * open;
        }

        std::array<double, kLines> v{};
        for (std::size_t k = 0; k < kLines; ++k) {
            Line& line = lines_[k];
            const double read = line.frames + modulationFrames_ * (1.0 + line.sin);
            const double out = static_cast<double>(line.line.tapFractional(read));
            line.damped = out + damping_ * (line.damped - out);
            v[k] = line.damped * line.gain;
            const double c = line.cos * line.turnCos - line.sin * line.turnSin;
            line.sin = line.sin * line.turnCos + line.cos * line.turnSin;
            line.cos = c;
        }
        double wetL = 0.0;
        double wetR = 0.0;
        for (std::size_t k = 0; k < kLines; ++k) {
            wetL += (k & 1U) != 0 ? -v[k] : v[k];
            wetR += (k & 2U) != 0 ? -v[k] : v[k];
        }
        // Mixes the lines (a fast Walsh-Hadamard transform: lossless).
        for (std::size_t h = 1; h < kLines; h *= 2)
            for (std::size_t a = 0; a < kLines; a += 2 * h)
                for (std::size_t b = a; b < a + h; ++b) {
                    const double x = v[b];
                    const double y = v[b + h];
                    v[b] = x + y;
                    v[b + h] = x - y;
                }
        for (std::size_t k = 0; k < kLines; ++k) {
            const double sign = (k & 2U) != 0 ? -1.0 : 1.0;
            lines_[k].line.push(flushed(v[k] * norm + sign * fed[k & 1U]));
        }

        wetL *= kOutputGain * norm;
        wetR *= kOutputGain * norm;
        wetL = highCut_[0]
                   .process(highCutCoefficients_, lowCut_[0].process(lowCutCoefficients_, wetL).high)
                   .low;
        wetR = highCut_[1]
                   .process(highCutCoefficients_, lowCut_[1].process(lowCutCoefficients_, wetR).high)
                   .low;
        const double mid = 0.5 * (wetL + wetR);
        if (stereo) {
            const double side = 0.5 * (wetL - wetR) * static_cast<double>(width);
            channels[0][i] = dryL + mix * (static_cast<float>(mid + side) - dryL);
            channels[1][i] = dryR + mix * (static_cast<float>(mid - side) - dryR);
        } else {
            channels[0][i] = dryL + mix * (static_cast<float>(mid) - dryL);
        }
    }

    for (Line& line : lines_) {
        if (std::abs(line.damped) < 1.0e-20)
            line.damped = 0.0;
        // Keeps the phasor on the unit circle.
        const double length = std::sqrt(line.cos * line.cos + line.sin * line.sin);
        line.cos /= length;
        line.sin /= length;
    }
    for (SvfState& state : lowCut_)
        state.flushDenormals();
    for (SvfState& state : highCut_)
        state.flushDenormals();
}

} // namespace vloom::dsp
