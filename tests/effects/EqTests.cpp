// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/effects/Eq.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <string_view>
#include <vector>

using Catch::Approx;
using vloom::dsp::Eq;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

std::vector<float> sine(double frequency, int frames, double rate = kRate, float level = 0.5f)
{
    std::vector<float> out(static_cast<std::size_t>(frames));
    const double step = 2.0 * std::numbers::pi * frequency / rate;
    for (int i = 0; i < frames; ++i)
        out[static_cast<std::size_t>(i)] = level * static_cast<float>(std::sin(step * i));
    return out;
}

/// Repeatable noise in -1 .. 1.
std::vector<float> noise(int frames, std::uint32_t seed = 1)
{
    std::vector<float> out(static_cast<std::size_t>(frames));
    for (float& sample : out) {
        seed = seed * 1664525u + 1013904223u;
        sample = static_cast<float>(seed >> 8) / 8388608.0f - 1.0f;
    }
    return out;
}

/// Runs mono `signal` through `eq` in blocks of `block` frames.
void run(Eq& eq, std::vector<float>& signal, int block = kBlock)
{
    for (std::size_t at = 0; at < signal.size(); at += static_cast<std::size_t>(block)) {
        float* channel = signal.data() + at;
        const int frames
            = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), signal.size() - at));
        eq.process(&channel, 1, frames);
    }
}

double rmsDb(const std::vector<float>& signal, std::size_t from)
{
    double sum = 0.0;
    for (std::size_t i = from; i < signal.size(); ++i)
        sum += static_cast<double>(signal[i]) * static_cast<double>(signal[i]);
    return 10.0 * std::log10(sum / static_cast<double>(signal.size() - from));
}

/// What `eq` does to the level of a sine at `frequency`, in dB.
double measuredDb(Eq& eq, double frequency)
{
    eq.reset();
    std::vector<float> in = sine(frequency, 96000);
    std::vector<float> out = in;
    run(eq, out);
    return rmsDb(out, 48000) - rmsDb(in, 48000);
}

float peak(const std::vector<float>& signal)
{
    float most = 0.0f;
    for (const float sample : signal)
        most = std::max(most, std::abs(sample));
    return most;
}

} // namespace

TEST_CASE("EQ describes itself and its parameters", "[eq]")
{
    CHECK(std::string_view(Eq::spec().id) == "vloom.eq");
    REQUIRE(Eq::parameters().size() == Eq::kParameterCount);
    for (std::size_t i = 0; i < Eq::kParameterCount; ++i) {
        const auto& parameter = Eq::parameters()[i];
        CHECK(parameter.id == i); // the id is the index: setParameter() relies on it
        CHECK(parameter.minValue < parameter.maxValue);
        CHECK(parameter.defaultValue >= parameter.minValue);
        CHECK(parameter.defaultValue <= parameter.maxValue);
        CHECK(std::string_view(parameter.name).size() > 0);
    }
    const auto* on = vloom::dsp::findParameter(Eq::parameters(), Eq::kLowCutOn);
    REQUIRE(on != nullptr);
    CHECK(on->stepped);
    CHECK(Eq().latencyFrames() == 0);
}

TEST_CASE("EQ at its defaults passes the input through bit for bit", "[eq]")
{
    Eq eq;
    eq.prepare(kRate, kBlock);
    const std::vector<float> in = noise(4096);
    std::vector<float> out = in;
    run(eq, out);
    CHECK(out == in);
    CHECK(eq.magnitudeDb(1000.0) == 0.0);

    // Moving a band that is at 0 dB changes nothing either.
    eq.setParameter(Eq::kLowMidFrequency, 3000.0);
    eq.setParameter(Eq::kLowMidQ, 9.0);
    run(eq, out);
    CHECK(out == in);
}

TEST_CASE("EQ passes through before prepare and after a bad sample rate", "[eq]")
{
    const std::vector<float> in = noise(256);
    Eq eq;
    eq.setParameter(Eq::kLowMidGain, 12.0);
    std::vector<float> out = in;
    run(eq, out);
    CHECK(out == in);

    eq.prepare(std::numeric_limits<double>::quiet_NaN(), kBlock);
    run(eq, out);
    CHECK(out == in);
    eq.prepare(-48000.0, kBlock);
    run(eq, out);
    CHECK(out == in);
    CHECK(eq.magnitudeDb(400.0) == 0.0);
}

TEST_CASE("Each EQ section does what its settings say", "[eq]")
{
    Eq eq;
    eq.prepare(kRate, kBlock);

    SECTION("a bell")
    {
        eq.setParameter(Eq::kHighMidFrequency, 1000.0);
        eq.setParameter(Eq::kHighMidGain, 6.0);
        eq.setParameter(Eq::kHighMidQ, 2.0);
        CHECK(measuredDb(eq, 1000.0) == Approx(6.0).margin(0.02));
        CHECK(measuredDb(eq, 60.0) == Approx(0.0).margin(0.05));
        CHECK(measuredDb(eq, 12000.0) == Approx(0.0).margin(0.05));
    }
    SECTION("the low shelf")
    {
        eq.setParameter(Eq::kLowShelfFrequency, 200.0);
        eq.setParameter(Eq::kLowShelfGain, -9.0);
        CHECK(measuredDb(eq, 30.0) == Approx(-9.0).margin(0.2));
        CHECK(measuredDb(eq, 8000.0) == Approx(0.0).margin(0.05));
    }
    SECTION("the high shelf")
    {
        eq.setParameter(Eq::kHighShelfFrequency, 4000.0);
        eq.setParameter(Eq::kHighShelfGain, 9.0);
        CHECK(measuredDb(eq, 18000.0) == Approx(9.0).margin(0.2));
        CHECK(measuredDb(eq, 100.0) == Approx(0.0).margin(0.05));
    }
    SECTION("the low cut, only when on")
    {
        eq.setParameter(Eq::kLowCutFrequency, 200.0);
        CHECK(measuredDb(eq, 50.0) == Approx(0.0).margin(1e-6));
        eq.setParameter(Eq::kLowCutOn, 1.0);
        CHECK(measuredDb(eq, 200.0) == Approx(-3.01).margin(0.02));
        CHECK(measuredDb(eq, 50.0) == Approx(-24.1).margin(0.2));
        CHECK(measuredDb(eq, 5000.0) == Approx(0.0).margin(0.02));
    }
    SECTION("the high cut, only when on")
    {
        eq.setParameter(Eq::kHighCutFrequency, 2000.0);
        CHECK(measuredDb(eq, 8000.0) == Approx(0.0).margin(1e-6));
        eq.setParameter(Eq::kHighCutOn, 1.0);
        CHECK(measuredDb(eq, 2000.0) == Approx(-3.01).margin(0.02));
        CHECK(measuredDb(eq, 8000.0) < -22.0);
        CHECK(measuredDb(eq, 100.0) == Approx(0.0).margin(0.02));
    }
}

TEST_CASE("EQ's response curve matches what it does to audio", "[eq]")
{
    Eq eq;
    eq.prepare(kRate, kBlock);
    eq.setParameter(Eq::kLowCutOn, 1.0);
    eq.setParameter(Eq::kLowCutFrequency, 60.0);
    eq.setParameter(Eq::kLowShelfGain, 4.0);
    eq.setParameter(Eq::kLowMidGain, -7.0);
    eq.setParameter(Eq::kHighMidGain, 5.0);
    eq.setParameter(Eq::kHighMidQ, 4.0);
    eq.setParameter(Eq::kHighShelfGain, -3.0);
    eq.setParameter(Eq::kHighCutOn, 1.0);
    for (const double frequency : {40.0, 150.0, 400.0, 2500.0, 9000.0, 16000.0})
        CHECK(measuredDb(eq, frequency) == Approx(eq.magnitudeDb(frequency)).margin(0.03));
}

TEST_CASE("EQ gives the same output whatever the block size", "[eq]")
{
    const auto render = [](int block) {
        Eq eq;
        eq.prepare(kRate, kBlock);
        eq.setParameter(Eq::kLowMidGain, 9.0);
        std::vector<float> out = noise(3000);
        run(eq, out, block);
        // Changes land between the same frames for every block size here.
        eq.setParameter(Eq::kLowMidFrequency, 2000.0);
        eq.setParameter(Eq::kHighCutOn, 1.0);
        std::vector<float> more = noise(3000, 7);
        run(eq, more, block);
        out.insert(out.end(), more.begin(), more.end());
        return out;
    };
    const std::vector<float> whole = render(3000);
    CHECK(render(1) == whole);
    CHECK(render(7) == whole);
    CHECK(render(64) == whole);
}

TEST_CASE("EQ changes glide instead of jumping", "[eq]")
{
    Eq eq;
    eq.prepare(kRate, kBlock);
    eq.setParameter(Eq::kLowMidFrequency, 1000.0);
    eq.reset();
    std::vector<float> before = sine(1000.0, 4800);
    run(eq, before);

    eq.setParameter(Eq::kLowMidGain, 12.0); // x4 at 1 kHz
    std::vector<float> after = sine(1000.0, 9600);
    run(eq, after);
    // 1 ms in it has hardly moved; after the 20 ms glide and the filter's
    // own settling it is there.
    const std::vector<float> start(after.begin(), after.begin() + 48);
    const std::vector<float> end(after.end() - 480, after.end());
    CHECK(peak(start) < 0.5f * 1.3f);
    CHECK(peak(end) == Approx(0.5f * 3.981f).margin(0.02));
    // No step anywhere: a 1 kHz sine at that level moves at most this much per frame.
    const float limit = 0.5f * 3.981f * static_cast<float>(2.0 * std::numbers::pi * 1000.0 / kRate) * 1.2f;
    for (std::size_t i = 1; i < after.size(); ++i)
        CHECK(std::abs(after[i] - after[i - 1]) < limit);
}

TEST_CASE("Switching an EQ cut fades instead of clicking", "[eq]")
{
    Eq eq;
    eq.prepare(kRate, kBlock);
    eq.setParameter(Eq::kLowCutFrequency, 1000.0);
    std::vector<float> signal(4800, 0.5f); // DC: the low cut removes it all
    run(eq, signal);
    CHECK(signal.back() == 0.5f);

    eq.setParameter(Eq::kLowCutOn, 1.0);
    std::fill(signal.begin(), signal.end(), 0.5f);
    run(eq, signal);
    CHECK(signal.front() > 0.49f); // still nearly all there
    for (std::size_t i = 1; i < signal.size(); ++i)
        CHECK(std::abs(signal[i] - signal[i - 1]) < 0.01f);
    CHECK(std::abs(signal.back()) < 1.0e-3f);

    eq.setParameter(Eq::kLowCutOn, 0.0);
    std::fill(signal.begin(), signal.end(), 0.5f);
    run(eq, signal);
    for (std::size_t i = 1; i < signal.size(); ++i)
        CHECK(std::abs(signal[i] - signal[i - 1]) < 0.01f);
    CHECK(signal.back() == 0.5f); // out of the path again
}

TEST_CASE("EQ set before prepare starts there, and reset ends a glide", "[eq]")
{
    Eq early;
    early.setParameter(Eq::kHighMidGain, -12.0);
    early.prepare(kRate, kBlock);

    Eq late;
    late.prepare(kRate, kBlock);
    late.setParameter(Eq::kHighMidGain, -12.0);
    std::vector<float> scratch = noise(64);
    run(late, scratch); // mid-glide
    late.reset();

    std::vector<float> a = noise(2048);
    std::vector<float> b = a;
    run(early, a);
    run(late, b);
    CHECK(a == b);
    CHECK(a != noise(2048));
}

TEST_CASE("EQ clamps values and ignores NaN and unknown parameters", "[eq]")
{
    Eq eq;
    eq.prepare(kRate, kBlock);
    eq.setParameter(Eq::kLowMidFrequency, 1000.0);
    eq.setParameter(Eq::kLowMidGain, 1000.0);
    CHECK(eq.magnitudeDb(1000.0) == Approx(Eq::kMaxGainDb).margin(1e-6));
    eq.setParameter(Eq::kLowMidGain, -1000.0);
    CHECK(eq.magnitudeDb(1000.0) == Approx(-Eq::kMaxGainDb).margin(1e-6));

    eq.setParameter(Eq::kLowMidGain, std::numeric_limits<double>::quiet_NaN());
    eq.setParameter(Eq::kLowMidGain, std::numeric_limits<double>::infinity());
    eq.setParameter(99, 6.0);
    CHECK(eq.magnitudeDb(1000.0) == Approx(-Eq::kMaxGainDb).margin(1e-6));

    // A switch rounds to on or off.
    eq.setParameter(Eq::kLowMidGain, 0.0);
    eq.setParameter(Eq::kLowCutOn, 0.4);
    CHECK(eq.magnitudeDb(20.0) == 0.0);
    eq.setParameter(Eq::kLowCutOn, 0.6);
    CHECK(eq.magnitudeDb(20.0) < -10.0);
}

TEST_CASE("EQ keeps channels apart and leaves extra channels alone", "[eq]")
{
    Eq eq;
    eq.prepare(kRate, kBlock);
    eq.setParameter(Eq::kLowShelfGain, 12.0);
    eq.setParameter(Eq::kLowCutOn, 1.0);

    std::vector<std::vector<float>> buffers(Eq::kMaxChannels + 1, std::vector<float>(kBlock, 0.0f));
    buffers[0] = noise(kBlock);
    buffers.back() = noise(kBlock, 3);
    const std::vector<float> extra = buffers.back();
    std::vector<float*> channels;
    for (auto& buffer : buffers)
        channels.push_back(buffer.data());
    eq.process(channels.data(), static_cast<int>(channels.size()), kBlock);

    CHECK(buffers[0] != noise(kBlock));
    for (std::size_t ch = 1; ch < static_cast<std::size_t>(Eq::kMaxChannels); ++ch)
        CHECK(peak(buffers[ch]) == 0.0f); // silence in, silence out
    CHECK(buffers.back() == extra);

    eq.process(nullptr, 2, kBlock); // no buffers: nothing to do
    eq.process(channels.data(), 2, 0);
}

TEST_CASE("EQ stays stable at every sample rate while everything moves", "[eq]")
{
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0}) {
        Eq eq;
        eq.prepare(rate, kBlock);
        std::uint32_t seed = 12345;
        const auto random = [&seed] {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<double>(seed >> 8) / 16777216.0;
        };
        float most = 0.0f;
        for (int block = 0; block < 400; ++block) {
            // Three values jump somewhere in their range every block.
            for (int change = 0; change < 3; ++change) {
                const auto& parameter = Eq::parameters()[static_cast<std::size_t>(random() * 13.999)];
                eq.setParameter(
                    parameter.id, parameter.minValue + random() * (parameter.maxValue - parameter.minValue));
            }
            std::vector<float> signal = noise(kBlock, static_cast<std::uint32_t>(block + 1));
            run(eq, signal);
            for (const float sample : signal)
                REQUIRE(std::isfinite(sample));
            most = std::max(most, peak(signal));
        }
        // Four sections at up to +18 dB each, with resonant bells in motion.
        CHECK(most < 20000.0f);
    }
}
