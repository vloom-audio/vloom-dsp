// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
//
// Signals and checks shared by the effect tests.
#pragma once

#include "AllocationCounter.hpp"

#include <vloom/dsp/Processor.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <string_view>
#include <vector>

namespace vloom::test {

using vloom::dsp::findParameter;
using vloom::dsp::ParameterSpec;

using Signal = std::vector<float>;
/// One Signal per channel, all the same length.
using Audio = std::vector<Signal>;

inline Signal sine(double frequency, int frames, double rate, float level = 0.5f, double phase = 0.0)
{
    Signal out(static_cast<std::size_t>(frames));
    const double step = 2.0 * std::numbers::pi * frequency / rate;
    for (int i = 0; i < frames; ++i)
        out[static_cast<std::size_t>(i)] = level * static_cast<float>(std::sin(step * i + phase));
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

inline Signal impulse(int frames, int at = 0, float level = 1.0f)
{
    Signal out(static_cast<std::size_t>(frames), 0.0f);
    out[static_cast<std::size_t>(at)] = level;
    return out;
}

inline Signal constant(int frames, float level) { return Signal(static_cast<std::size_t>(frames), level); }

/// Runs `audio` through `effect` in place, in blocks of `block` frames.
template <typename E> void render(E& effect, Audio& audio, int block = 512)
{
    const std::size_t frames = audio.empty() ? 0 : audio[0].size();
    std::vector<float*> pointers(audio.size());
    for (std::size_t at = 0; at < frames; at += static_cast<std::size_t>(block)) {
        for (std::size_t ch = 0; ch < audio.size(); ++ch)
            pointers[ch] = audio[ch].data() + at;
        const auto count
            = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), frames - at));
        effect.process(pointers.data(), static_cast<int>(audio.size()), count);
    }
}

/// Mono convenience: runs one signal through `effect`.
template <typename E> Signal renderMono(E& effect, Signal signal, int block = 512)
{
    Audio audio{std::move(signal)};
    render(effect, audio, block);
    return std::move(audio[0]);
}

inline float peak(
    const Signal& signal, std::size_t from = 0, std::size_t to = std::numeric_limits<std::size_t>::max())
{
    float most = 0.0f;
    for (std::size_t i = from; i < std::min(to, signal.size()); ++i)
        most = std::max(most, std::abs(signal[i]));
    return most;
}

/// The RMS level of `signal[from, to)` in dB (-300 for silence).
inline double rmsDb(
    const Signal& signal, std::size_t from = 0, std::size_t to = std::numeric_limits<std::size_t>::max())
{
    to = std::min(to, signal.size());
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i)
        sum += static_cast<double>(signal[i]) * static_cast<double>(signal[i]);
    if (!(sum > 0.0) || to <= from)
        return -300.0;
    return 10.0 * std::log10(sum / static_cast<double>(to - from));
}

inline bool allFinite(const Audio& audio)
{
    for (const Signal& signal : audio)
        for (const float sample : signal)
            if (!std::isfinite(sample))
                return false;
    return true;
}

/// The amplitude of the component at `frequency` in `signal[from, from +
/// count)` (a single DFT bin; exact when `count` holds whole cycles).
inline double amplitudeAt(
    const Signal& signal, double frequency, double rate, std::size_t from, std::size_t count)
{
    std::complex<double> sum = 0.0;
    const double step = 2.0 * std::numbers::pi * frequency / rate;
    for (std::size_t i = 0; i < count; ++i)
        sum += static_cast<double>(signal[from + i]) * std::polar(1.0, -step * static_cast<double>(i));
    const double scale = frequency == 0.0 ? 1.0 : 2.0;
    return scale * std::abs(sum) / static_cast<double>(count);
}

/// Checks what every effect's parameter list must hold.
template <typename E> void checkParameterList()
{
    const auto parameters = E::parameters();
    REQUIRE(parameters.size() == static_cast<std::size_t>(E::kParameterCount));
    std::vector<std::string_view> groups;
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        const ParameterSpec& parameter = parameters[i];
        INFO("parameter " << i << " " << parameter.name);
        CHECK(parameter.id == i); // the id is the index: setParameter() relies on it
        CHECK(parameter.minValue < parameter.maxValue);
        CHECK(parameter.defaultValue >= parameter.minValue);
        CHECK(parameter.defaultValue <= parameter.maxValue);
        CHECK_FALSE(std::string_view(parameter.name).empty());
        CHECK_FALSE(std::string_view(parameter.group).empty());
        if (parameter.stepped) {
            CHECK(parameter.defaultValue == std::round(parameter.defaultValue));
            CHECK(parameter.minValue == std::round(parameter.minValue));
            CHECK(parameter.maxValue == std::round(parameter.maxValue));
        }
        // The parameters of one group follow each other.
        const std::string_view group = parameter.group;
        if (groups.empty() || groups.back() != group) {
            CHECK(std::find(groups.begin(), groups.end(), group) == groups.end());
            groups.push_back(group);
        }
        for (std::size_t j = 0; j < i; ++j)
            CHECK(std::string_view(parameters[j].name) != std::string_view(parameter.name));
    }
    CHECK(findParameter(parameters, static_cast<std::uint32_t>(parameters.size())) == nullptr);
}

/// Every parameter at its minimum, its maximum, NaN and infinity, at
/// several sample rates, on mono and stereo noise: the output stays finite.
template <typename E> void checkStableEverywhere()
{
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    constexpr double inf = std::numeric_limits<double>::infinity();
    for (const double rate : {44100.0, 96000.0, 192000.0}) {
        for (const int numChannels : {1, 2}) {
            for (const bool high : {false, true}) {
                E effect;
                effect.prepare(rate, 256);
                for (const ParameterSpec& parameter : E::parameters()) {
                    effect.setParameter(parameter.id, high ? parameter.maxValue : parameter.minValue);
                    effect.setParameter(parameter.id, nan);
                    effect.setParameter(parameter.id, inf);
                    effect.setParameter(parameter.id, -inf);
                }
                Audio audio(
                    static_cast<std::size_t>(numChannels), noise(static_cast<int>(rate / 2), 7, 0.9f));
                render(effect, audio, 256);
                INFO(
                    "rate " << rate << ", channels " << numChannels << ", all at " << (high ? "max" : "min"));
                CHECK(allFinite(audio));
                CHECK(peak(audio[0]) < 1000.0f);
            }
        }
    }
}

/// After prepare(), process() allocates nothing.
template <typename E> void checkNoAllocation(E& effect, int numChannels = 2)
{
    Audio audio(static_cast<std::size_t>(numChannels), noise(4096, 3, 0.5f));
    std::vector<float*> pointers(audio.size());
    for (std::size_t ch = 0; ch < audio.size(); ++ch)
        pointers[ch] = audio[ch].data();
    const std::size_t before = allocationCount();
    for (int at = 0; at + 512 <= 4096; at += 512) {
        effect.process(pointers.data(), numChannels, 512);
        for (float*& pointer : pointers)
            pointer += 512;
    }
    const std::size_t after = allocationCount();
    CHECK(after == before);
}

/// Silence in gives silence out (exact zeros).
template <typename E> void checkSilentWhenSilent(E& effect, int numChannels = 2, int frames = 48000)
{
    Audio audio(static_cast<std::size_t>(numChannels), Signal(static_cast<std::size_t>(frames), 0.0f));
    render(effect, audio);
    for (const Signal& signal : audio)
        CHECK(peak(signal) == 0.0f);
}

} // namespace vloom::test
