// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/effects/Limiter.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vloom::dsp {

double Limiter::MinimumWindow::push(double value, long long now, int window) noexcept
{
    const std::size_t capacity = values.size();
    while (size > 0 && values[(head + size - 1) % capacity] >= value)
        --size;
    values[(head + size) % capacity] = value;
    stamps[(head + size) % capacity] = now;
    ++size;
    while (stamps[head] <= now - window) {
        head = (head + 1) % capacity;
        --size;
    }
    return values[head];
}

Limiter::Limiter() noexcept
{
    // Windowed sinc (Hann, 8 taps) at 1/4, 1/2 and 3/4 of the way between
    // the middle two of the last eight samples.
    for (int p = 0; p < kInterpolationPhases; ++p) {
        const double position = 3.0 + static_cast<double>(p + 1) / 4.0;
        double sum = 0.0;
        auto& taps = interpolation_[static_cast<std::size_t>(p)];
        for (int j = 0; j < kInterpolationTaps; ++j) {
            const double t = position - static_cast<double>(j);
            const double x = std::numbers::pi * t;
            const double sinc = std::sin(x) / x; // t is never whole
            const double window = 0.5 * (1.0 + std::cos(std::numbers::pi * t / 4.0));
            taps[static_cast<std::size_t>(j)] = sinc * window;
            sum += sinc * window;
        }
        for (double& tap : taps)
            tap /= sum;
    }
}

void Limiter::prepare(double sampleRate, int /*maxBlockFrames*/)
{
    sampleRate_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 0.0;
    const int maxLookahead = static_cast<int>(std::ceil(kMaxLookaheadMs / 1000.0 * sampleRate_)) + 1;
    for (Channel& channel : channels_) {
        channel.audio.prepare(maxLookahead + kTruePeakFrames + 2);
        channel.minimum.values.assign(static_cast<std::size_t>(maxLookahead) + 2, 1.0);
        channel.minimum.stamps.assign(static_cast<std::size_t>(maxLookahead) + 2, 0);
        channel.average.assign(static_cast<std::size_t>(maxLookahead) + 1, 1.0);
    }
    inputGain_.reset(sampleRate_, kRampSeconds, 1.0f);
    lookahead_ = -1; // worked out again below
    reset();
}

void Limiter::setParameter(std::uint32_t id, double value) noexcept
{
    if (id >= kParameterCount || !std::isfinite(value))
        return;
    values_[id] = clampToSpec(kParameters[id], value);
    applyValues(false);
}

void Limiter::applyValues(bool jump) noexcept
{
    ceiling_ = decibelsToGain(value(kCeiling));
    releaseFactor_ = onePoleFactor(value(kRelease) / 1000.0, sampleRate_);
    const auto gain = static_cast<float>(decibelsToGain(value(kInputGain)));
    if (jump)
        inputGain_.setCurrentAndTarget(gain);
    else
        inputGain_.setTarget(gain);

    const int capacity = channels_[0].average.empty() ? 1 : static_cast<int>(channels_[0].average.size()) - 1;
    const int lookahead
        = std::clamp(static_cast<int>(std::lround(value(kLookahead) / 1000.0 * sampleRate_)), 1, capacity);
    const bool truePeak = value(kTruePeak) >= 0.5;
    if (lookahead != lookahead_ || truePeak != truePeak_) {
        const int before = lookahead_;
        lookahead_ = lookahead;
        truePeak_ = truePeak;
        if (!jump && before > 0)
            restartGain(before);
    }
}

void Limiter::restartGain(int before) noexcept
{
    for (Channel& channel : channels_) {
        if (channel.average.empty())
            continue;
        // Go on from the gain in use, so the change does not jump.
        const double from = std::min(channel.released, channel.sum / static_cast<double>(before + 1));
        const double level = std::isfinite(from) ? std::clamp(from, 0.0, 1.0) : 1.0;
        channel.minimum.clear();
        channel.released = level;
        std::fill(channel.average.begin(), channel.average.end(), level);
        channel.sum = level * static_cast<double>(lookahead_ + 1);
        channel.averageAt = 0;
    }
}

void Limiter::reset() noexcept
{
    applyValues(true);
    for (Channel& channel : channels_) {
        channel.audio.clear();
        channel.recent = {};
        channel.recentAt = 0;
        channel.lastBetween = 0.0;
        channel.minimum.clear();
        channel.released = 1.0;
        std::fill(channel.average.begin(), channel.average.end(), 1.0);
        channel.sum = static_cast<double>(lookahead_ + 1);
        channel.averageAt = 0;
    }
    now_ = 0;
    gainReduction_.store(0.0f);
}

double Limiter::truePeak(Channel& channel, float sample) const noexcept
{
    channel.recent[static_cast<std::size_t>(channel.recentAt)] = sample;
    channel.recentAt = (channel.recentAt + 1) % kInterpolationTaps;
    // recent[recentAt] is now the oldest of the eight.
    const auto at = [&](int j) {
        return static_cast<double>(
            channel.recent[static_cast<std::size_t>((channel.recentAt + j) % kInterpolationTaps)]);
    };
    double between = 0.0;
    for (const auto& taps : interpolation_) {
        double sum = 0.0;
        for (int j = 0; j < kInterpolationTaps; ++j)
            sum += taps[static_cast<std::size_t>(j)] * at(j);
        between = std::max(between, std::abs(sum));
    }
    const double peak = std::max({std::abs(at(3)), between, channel.lastBetween});
    channel.lastBetween = between;
    return peak;
}

void Limiter::process(float* const* channels, int numChannels, int numFrames) noexcept
{
    if (!(sampleRate_ > 0.0) || channels == nullptr || numChannels < 1)
        return;
    numChannels = std::min(numChannels, kMaxChannels);
    const double link = value(kStereoLink) / 100.0;
    const int window = lookahead_ + 1;
    const int delay = latencyFrames();
    const auto ceiling = static_cast<float>(ceiling_);
    double lowest = 1.0;

    for (int i = 0; i < numFrames; ++i) {
        const auto gain = inputGain_.next();
        std::array<double, kMaxChannels> peak{};
        double loudest = 0.0;
        for (int ch = 0; ch < numChannels; ++ch) {
            Channel& channel = channels_[static_cast<std::size_t>(ch)];
            const float x = channels[ch][i] * gain;
            channel.audio.push(x);
            const double p = truePeak_ ? truePeak(channel, x) : static_cast<double>(std::abs(x));
            peak[static_cast<std::size_t>(ch)] = std::isfinite(p) ? p : 0.0;
            loudest = std::max(loudest, peak[static_cast<std::size_t>(ch)]);
        }
        for (int ch = 0; ch < numChannels; ++ch) {
            Channel& channel = channels_[static_cast<std::size_t>(ch)];
            const double own = peak[static_cast<std::size_t>(ch)];
            const double level = own + link * (loudest - own);
            const double needed = level > ceiling_ ? ceiling_ / level : 1.0;
            const double held = channel.minimum.push(needed, now_, window);
            channel.released
                = held < channel.released ? held : held + releaseFactor_ * (channel.released - held);

            channel.sum += channel.released - channel.average[channel.averageAt];
            channel.average[channel.averageAt] = channel.released;
            if (++channel.averageAt == static_cast<std::size_t>(window)) {
                channel.averageAt = 0;
                // Add up afresh once per window, so rounding cannot build up.
                double sum = 0.0;
                for (std::size_t k = 0; k < static_cast<std::size_t>(window); ++k)
                    sum += channel.average[k];
                channel.sum = sum;
            }
            const double smoothed = channel.sum / static_cast<double>(window);
            lowest = std::min(lowest, smoothed);
            const float out = channel.audio.tap(delay + 1) * static_cast<float>(smoothed);
            channels[ch][i] = std::clamp(out, -ceiling, ceiling);
        }
        ++now_;
    }
    for (Channel& channel : channels_)
        if (std::abs(1.0 - channel.released) < 1.0e-12)
            channel.released = 1.0;
    gainReduction_.store(static_cast<float>(lowest > 0.0 ? -20.0 * std::log10(lowest) : 120.0));
}

} // namespace vloom::dsp
