// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "support/EffectTesting.hpp"
#include "vloom/dsp/effects/Filter.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string_view>

using Catch::Approx;
using vloom::dsp::Filter;
using namespace vloom::test;

namespace {

constexpr double kRate = 48000.0;

Filter prepared(
    Filter::Type type, double cutoff, Filter::Slope slope = Filter::Slope::Db12, double rate = kRate)
{
    Filter filter;
    filter.prepare(rate, 512);
    filter.setParameter(Filter::kType, static_cast<double>(type));
    filter.setParameter(Filter::kCutoff, cutoff);
    filter.setParameter(Filter::kSlope, static_cast<double>(slope));
    filter.reset();
    return filter;
}

/// What `filter` does to the level of a sine at `frequency`, in dB.
double measuredDb(Filter& filter, double frequency, double rate = kRate)
{
    filter.reset();
    const auto frames = static_cast<int>(rate);
    const Signal in = sine(frequency, frames, rate, 0.25f);
    const Signal out = renderMono(filter, in);
    return rmsDb(out, static_cast<std::size_t>(frames / 2)) - rmsDb(in, static_cast<std::size_t>(frames / 2));
}

} // namespace

TEST_CASE("Filter describes itself and its parameters", "[filter]")
{
    CHECK(std::string_view(Filter::spec().id) == "vloom.filter");
    checkParameterList<Filter>();
    CHECK(Filter().latencyFrames() == 0);
}

TEST_CASE("Filter's level at the cutoff", "[filter]")
{
    using Type = Filter::Type;
    using Slope = Filter::Slope;
    for (const double rate : {44100.0, 96000.0, 192000.0}) {
        for (const Slope slope : {Slope::Db12, Slope::Db24}) {
            for (const double cutoff : {200.0, 1000.0, 5000.0}) {
                INFO("rate " << rate << ", slope " << static_cast<int>(slope) << ", cutoff " << cutoff);
                Filter low = prepared(Type::LowPass, cutoff, slope, rate);
                Filter high = prepared(Type::HighPass, cutoff, slope, rate);
                Filter band = prepared(Type::BandPass, cutoff, slope, rate);
                Filter notch = prepared(Type::Notch, cutoff, slope, rate);
                CHECK(measuredDb(low, cutoff, rate) == Approx(-3.01).margin(0.05));
                CHECK(measuredDb(high, cutoff, rate) == Approx(-3.01).margin(0.05));
                CHECK(measuredDb(band, cutoff, rate) == Approx(0.0).margin(0.05));
                CHECK(measuredDb(notch, cutoff, rate) < -40.0);
                CHECK(low.magnitudeDb(cutoff) == Approx(-3.01).margin(0.01));
                CHECK(band.magnitudeDb(cutoff) == Approx(0.0).margin(0.01));
            }
        }
    }
}

TEST_CASE("Filter's slopes: 12 and 24 dB per octave", "[filter]")
{
    Filter shallow = prepared(Filter::Type::LowPass, 500.0);
    Filter steep = prepared(Filter::Type::LowPass, 500.0, Filter::Slope::Db24);
    // Two to three octaves up: about 12 or 24 dB more per octave.
    const double shallowSlope = measuredDb(shallow, 2000.0) - measuredDb(shallow, 4000.0);
    const double steepSlope = measuredDb(steep, 2000.0) - measuredDb(steep, 4000.0);
    CHECK(shallowSlope == Approx(12.0).margin(1.0));
    CHECK(steepSlope == Approx(24.0).margin(2.0));
    // magnitudeDb() matches what is measured.
    for (const double frequency : {100.0, 500.0, 3000.0})
        CHECK(steep.magnitudeDb(frequency) == Approx(measuredDb(steep, frequency)).margin(0.05));
}

TEST_CASE("Filter's resonance raises a peak at the cutoff", "[filter]")
{
    Filter filter = prepared(Filter::Type::LowPass, 1000.0);
    filter.setParameter(Filter::kResonance, 100.0);
    filter.reset();
    CHECK(measuredDb(filter, 1000.0) == Approx(20.0 * std::log10(Filter::kMaxQ)).margin(0.2));
    CHECK(filter.magnitudeDb(1000.0) == Approx(20.0 * std::log10(Filter::kMaxQ)).margin(0.01));
}

TEST_CASE("Filter without drive is linear; drive saturates", "[filter]")
{
    Filter filter = prepared(Filter::Type::LowPass, 2000.0);
    const Signal in = noise(4096, 1, 0.5f);
    const Signal once = renderMono(filter, in);
    filter.reset();
    Signal doubled = in;
    for (float& sample : doubled)
        sample *= 2.0f;
    const Signal twice = renderMono(filter, doubled);
    for (std::size_t i = 0; i < once.size(); ++i)
        REQUIRE(twice[i] == Approx(2.0f * once[i]).margin(1e-6));

    filter.setParameter(Filter::kDrive, 24.0);
    filter.reset();
    const Signal loud = renderMono(filter, constant(4800, 1.0f));
    CHECK(loud.back() == Approx(std::tanh(std::pow(10.0, 24.0 / 20.0))).margin(1e-3));
}

TEST_CASE("Filter survives cutoff and resonance jumping every block", "[filter]")
{
    Filter filter = prepared(Filter::Type::BandPass, 1000.0, Filter::Slope::Db24);
    Audio audio{noise(48000, 1, 0.5f), noise(48000, 2, 0.5f)};
    std::vector<float*> pointers(2);
    for (int at = 0, block = 0; at < 48000; at += 64, ++block) {
        filter.setParameter(Filter::kCutoff, block % 2 == 0 ? 20.0 : 20000.0);
        filter.setParameter(Filter::kResonance, block % 3 == 0 ? 100.0 : 0.0);
        filter.setParameter(Filter::kType, static_cast<double>(block % 4));
        filter.setParameter(Filter::kSlope, static_cast<double>(block % 5 == 0));
        pointers[0] = audio[0].data() + at;
        pointers[1] = audio[1].data() + at;
        filter.process(pointers.data(), 2, 64);
    }
    CHECK(allFinite(audio));
    CHECK(peak(audio[0]) < 50.0f);
}

TEST_CASE("Filter glides, without zipper steps, when the cutoff moves", "[filter]")
{
    Filter filter = prepared(Filter::Type::LowPass, 200.0);
    (void)renderMono(filter, constant(4800, 0.5f)); // settled on DC
    filter.setParameter(Filter::kCutoff, 10000.0);
    const Signal out = renderMono(filter, constant(4800, 0.5f));
    for (const float sample : out)
        CHECK(sample == Approx(0.5f).margin(1e-3)); // DC passes a low-pass at any cutoff, also while moving
}

TEST_CASE("Filter: silence, reset, no allocation, stable everywhere", "[filter]")
{
    Filter filter = prepared(Filter::Type::LowPass, 1000.0, Filter::Slope::Db24);
    checkSilentWhenSilent(filter);
    checkNoAllocation(filter);
    checkNoAllocation(filter, 1);
    filter.reset();
    checkSilentWhenSilent(filter, 2, 1000);

    Filter fresh;
    const Signal in = noise(256);
    CHECK(renderMono(fresh, in) == in); // not prepared: passes through

    checkStableEverywhere<Filter>();
}
