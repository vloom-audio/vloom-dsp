// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

// Signals and measurements shared by the instrument tests.

#include "vloom/dsp/Sample.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <numbers>
#include <vector>

namespace vloom::dsp::test {

struct Stereo {
    std::vector<float> left;
    std::vector<float> right;
};

/// Renders `frames` of `processor` in blocks of `block`, into silence.
template <typename P> Stereo render(P& processor, int frames, int block = 256)
{
    Stereo out{std::vector<float>(static_cast<std::size_t>(frames), 0.0f),
        std::vector<float>(static_cast<std::size_t>(frames), 0.0f)};
    for (int done = 0; done < frames; done += block) {
        std::array<float*, 2> channels{out.left.data() + done, out.right.data() + done};
        processor.process(channels.data(), 2, std::min(block, frames - done));
    }
    return out;
}

/// A sample made of `frames` values of `f(frame)` (one channel), or of `f`
/// and `g` (two).
inline std::shared_ptr<const SampleBuffer> makeSample(int frames, double sampleRate,
    const std::function<float(int)>& f, const std::function<float(int)>& g = {})
{
    std::vector<float> left(static_cast<std::size_t>(frames));
    std::vector<float> right(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        left[static_cast<std::size_t>(i)] = f(i);
        right[static_cast<std::size_t>(i)] = g ? g(i) : 0.0f;
    }
    const std::array<const float*, 2> channels{left.data(), right.data()};
    return SampleBuffer::copy(channels.data(), g ? 2 : 1, frames, sampleRate);
}

/// `seconds` of a sine at `hz`, recorded at `sampleRate`.
inline std::shared_ptr<const SampleBuffer> sineSample(
    double hz, double sampleRate, double seconds, float level = 0.5f)
{
    return makeSample(static_cast<int>(seconds * sampleRate), sampleRate, [=](int i) {
        return level * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * i / sampleRate));
    });
}

inline double peak(const std::vector<float>& signal, std::size_t from = 0, std::size_t to = SIZE_MAX)
{
    double most = 0.0;
    for (std::size_t i = from; i < std::min(to, signal.size()); ++i)
        most = std::max(most, static_cast<double>(std::abs(signal[i])));
    return most;
}

inline double rms(const std::vector<float>& signal, std::size_t from = 0, std::size_t to = SIZE_MAX)
{
    to = std::min(to, signal.size());
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i)
        sum += static_cast<double>(signal[i]) * signal[i];
    return to > from ? std::sqrt(sum / static_cast<double>(to - from)) : 0.0;
}

/// The largest step from one sample to the next, in [from, to).
inline double largestStep(const std::vector<float>& signal, std::size_t from = 1, std::size_t to = SIZE_MAX)
{
    double most = 0.0;
    for (std::size_t i = std::max<std::size_t>(from, 1); i < std::min(to, signal.size()); ++i)
        most = std::max(most, static_cast<double>(std::abs(signal[i] - signal[i - 1])));
    return most;
}

/// Cycles per second, counted where the signal crosses zero going up, with
/// the crossing placed between samples.
inline double frequencyOf(
    const std::vector<float>& signal, double sampleRate, std::size_t from = 0, std::size_t to = SIZE_MAX)
{
    to = std::min(to, signal.size());
    int crossings = 0;
    double first = 0.0;
    double last = 0.0;
    for (std::size_t i = from + 1; i < to; ++i) {
        if (signal[i - 1] < 0.0f && signal[i] >= 0.0f) {
            const double at = static_cast<double>(i - 1) + signal[i - 1] / (signal[i - 1] - signal[i]);
            if (crossings == 0)
                first = at;
            last = at;
            ++crossings;
        }
    }
    return crossings > 1 ? (crossings - 1) * sampleRate / (last - first) : 0.0;
}

} // namespace vloom::dsp::test
