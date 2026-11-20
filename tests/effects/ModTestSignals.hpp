// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

// Signals and measurements shared by the modulation, dynamics and
// bitcrusher effect tests. Everything renders offline.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <numbers>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace modtest {

using Signal = std::vector<float>;

/// Checks `Effect`'s parameter list: the names in order, ids equal to the
/// index (setParameter() relies on it), sane ranges and contiguous groups.
template <typename Effect> void checkParameters(std::initializer_list<std::string_view> names)
{
    const auto parameters = Effect::parameters();
    REQUIRE(parameters.size() == names.size());
    std::size_t i = 0;
    std::set<std::string> groupsSeen;
    std::string_view group;
    for (const std::string_view name : names) {
        const auto& parameter = parameters[i];
        CHECK(parameter.id == i);
        CHECK(std::string_view(parameter.name) == name);
        CHECK(parameter.minValue < parameter.maxValue);
        CHECK(parameter.defaultValue >= parameter.minValue);
        CHECK(parameter.defaultValue <= parameter.maxValue);
        if (parameter.stepped)
            CHECK(parameter.defaultValue == std::round(parameter.defaultValue));
        const std::string_view current(parameter.group);
        CHECK(!current.empty());
        if (current != group) {
            CHECK(groupsSeen.insert(std::string(current)).second); // one run per group
            group = current;
        }
        ++i;
    }
}

inline Signal sine(double frequency, int frames, double rate, float level = 0.5f)
{
    Signal out(static_cast<std::size_t>(frames));
    const double step = 2.0 * std::numbers::pi * frequency / rate;
    for (int i = 0; i < frames; ++i)
        out[static_cast<std::size_t>(i)] = level * static_cast<float>(std::sin(step * i));
    return out;
}

/// Repeatable noise in -level .. level.
inline Signal noise(int frames, std::uint32_t seed = 1, float level = 1.0f)
{
    Signal out(static_cast<std::size_t>(frames));
    for (float& sample : out) {
        seed = seed * 1664525u + 1013904223u;
        sample = level * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
    }
    return out;
}

inline Signal constant(int frames, float value) { return Signal(static_cast<std::size_t>(frames), value); }

/// Runs `channels` (all the same length) through `effect` in blocks.
template <typename Effect> void run(Effect& effect, std::vector<Signal>& channels, int block = 512)
{
    const std::size_t length = channels.front().size();
    std::vector<float*> pointers(channels.size());
    for (std::size_t at = 0; at < length; at += static_cast<std::size_t>(block)) {
        for (std::size_t ch = 0; ch < channels.size(); ++ch)
            pointers[ch] = channels[ch].data() + at;
        const int frames
            = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), length - at));
        effect.process(pointers.data(), static_cast<int>(channels.size()), frames);
    }
}

template <typename Effect> void runMono(Effect& effect, Signal& signal, int block = 512)
{
    std::vector<Signal> channels{std::move(signal)};
    run(effect, channels, block);
    signal = std::move(channels.front());
}

inline double meanSquare(const Signal& signal, std::size_t from = 0, std::size_t to = 0)
{
    to = to == 0 ? signal.size() : std::min(to, signal.size());
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i)
        sum += static_cast<double>(signal[i]) * static_cast<double>(signal[i]);
    return sum / static_cast<double>(to - from);
}

inline double rmsDb(const Signal& signal, std::size_t from = 0, std::size_t to = 0)
{
    return 10.0 * std::log10(std::max(meanSquare(signal, from, to), 1.0e-30));
}

inline float peak(const Signal& signal)
{
    float most = 0.0f;
    for (const float sample : signal)
        most = std::max(most, std::abs(sample));
    return most;
}

inline bool allFinite(const Signal& signal)
{
    return std::all_of(signal.begin(), signal.end(), [](float s) { return std::isfinite(s); });
}

inline bool allZero(const Signal& signal)
{
    return std::all_of(signal.begin(), signal.end(), [](float s) { return s == 0.0f; });
}

/// The share of the power of signal[from, from + length) that sits at
/// `frequency` (1 for a pure sine; less when it is modulated). `length`
/// should hold whole cycles of `frequency`.
inline double powerShareAt(
    const Signal& signal, double frequency, double rate, std::size_t from, std::size_t length)
{
    double re = 0.0;
    double im = 0.0;
    const double w = 2.0 * std::numbers::pi * frequency / rate;
    for (std::size_t i = 0; i < length; ++i) {
        const auto x = static_cast<double>(signal[from + i]);
        re += x * std::cos(w * static_cast<double>(i));
        im -= x * std::sin(w * static_cast<double>(i));
    }
    const auto n = static_cast<double>(length);
    const double tone = 2.0 * (re * re + im * im) / (n * n);
    return tone / meanSquare(signal, from, from + length);
}

/// The magnitude in dB of the response `impulse` at `frequency`.
inline double responseDb(const Signal& impulse, double frequency, double rate)
{
    double re = 0.0;
    double im = 0.0;
    const double w = 2.0 * std::numbers::pi * frequency / rate;
    for (std::size_t i = 0; i < impulse.size(); ++i) {
        const auto x = static_cast<double>(impulse[i]);
        re += x * std::cos(w * static_cast<double>(i));
        im -= x * std::sin(w * static_cast<double>(i));
    }
    return 10.0 * std::log10(std::max(re * re + im * im, 1.0e-30));
}

/// The RMS level in dB of each window of `window` frames.
inline std::vector<double> windowLevels(const Signal& signal, std::size_t window, std::size_t from = 0)
{
    std::vector<double> levels;
    for (std::size_t at = from; at + window <= signal.size(); at += window)
        levels.push_back(rmsDb(signal, at, at + window));
    return levels;
}

} // namespace modtest
