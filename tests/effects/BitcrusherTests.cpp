// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "ModTestSignals.hpp"
#include "vloom/dsp/effects/Bitcrusher.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <set>
#include <string_view>

using Catch::Approx;
using vloom::dsp::Bitcrusher;
using namespace modtest;

namespace {

constexpr double kRate = 48000.0;

/// True if every sample of `signal` is a whole number of `step`s.
bool onGrid(const Signal& signal, double step)
{
    for (const float sample : signal) {
        const double steps = static_cast<double>(sample) / step;
        if (std::abs(steps - std::round(steps)) > 1.0e-4)
            return false;
    }
    return true;
}

} // namespace

TEST_CASE("Bitcrusher describes itself and its parameters", "[bitcrusher]")
{
    CHECK(std::string_view(Bitcrusher::spec().id) == "vloom.bitcrusher");
    checkParameters<Bitcrusher>(
        {"Drive", "Bit Depth", "Downsample", "Jitter", "Dither", "Anti-Alias", "Output", "Mix"});
    CHECK(Bitcrusher::parameters()[Bitcrusher::kBits].minValue == 1.0);
    CHECK(Bitcrusher::parameters()[Bitcrusher::kBits].maxValue == 24.0);
    CHECK_FALSE(Bitcrusher::parameters()[Bitcrusher::kBits].stepped); // fractional bits are fine
    CHECK(Bitcrusher::parameters()[Bitcrusher::kAntiAlias].stepped);
    CHECK(Bitcrusher().latencyFrames() == 0);
}

TEST_CASE("Bitcrusher at its defaults passes the input through bit for bit", "[bitcrusher]")
{
    Bitcrusher crusher;
    crusher.prepare(kRate, 512);
    const std::vector<Signal> in{noise(10000, 1), noise(10000, 2)};
    std::vector<Signal> out = in;
    run(crusher, out);
    CHECK(out == in);
}

TEST_CASE("Bitcrusher at Mix 0 passes the input through bit for bit", "[bitcrusher]")
{
    Bitcrusher crusher;
    crusher.setParameter(Bitcrusher::kBits, 3.0);
    crusher.setParameter(Bitcrusher::kDownsample, 7.5);
    crusher.setParameter(Bitcrusher::kDither, 100.0);
    crusher.setParameter(Bitcrusher::kAntiAlias, 1.0);
    crusher.setParameter(Bitcrusher::kMix, 0.0);
    crusher.prepare(kRate, 512);
    const Signal in = noise(10000);
    Signal out = in;
    runMono(crusher, out);
    CHECK(out == in);
}

TEST_CASE("Bitcrusher rounds to the step of its bit depth", "[bitcrusher]")
{
    for (const double bits : {1.0, 2.0, 4.0, 3.5, 8.0, 12.0}) {
        INFO("bits: " << bits);
        const double step = Bitcrusher::stepFor(bits);
        CHECK(step == Approx(2.0 / std::exp2(bits)));
        Bitcrusher crusher;
        crusher.setParameter(Bitcrusher::kBits, bits);
        crusher.prepare(kRate, 512);
        Signal signal = sine(997.0, 9600, kRate, 0.9f);
        runMono(crusher, signal);
        CHECK(onGrid(signal, step));
        // Never more than half a step from the input.
        const Signal in = sine(997.0, 9600, kRate, 0.9f);
        for (std::size_t i = 0; i < in.size(); i += 13)
            CHECK(std::abs(signal[i] - in[i]) <= step / 2.0 + 1.0e-6);
    }
}

TEST_CASE("Bitcrusher at 1 bit gives -1, 0 and 1", "[bitcrusher]")
{
    Bitcrusher crusher;
    crusher.setParameter(Bitcrusher::kBits, 1.0);
    crusher.prepare(kRate, 512);
    Signal signal = sine(440.0, 4800, kRate, 1.0f);
    runMono(crusher, signal);
    const std::set<float> values(signal.begin(), signal.end());
    CHECK(values == std::set<float>{-1.0f, 0.0f, 1.0f});
}

TEST_CASE("Bitcrusher dither keeps the output on the grid", "[bitcrusher]")
{
    Bitcrusher crusher;
    crusher.setParameter(Bitcrusher::kBits, 6.0);
    crusher.setParameter(Bitcrusher::kDither, 100.0);
    crusher.prepare(kRate, 512);
    Signal signal = sine(440.0, 9600, kRate, 0.5f);
    runMono(crusher, signal);
    CHECK(onGrid(signal, Bitcrusher::stepFor(6.0)));
}

TEST_CASE("Bitcrusher holds each sample for the downsample factor", "[bitcrusher]")
{
    Bitcrusher crusher;
    crusher.setParameter(Bitcrusher::kDownsample, 4.0);
    crusher.prepare(kRate, 512);
    const Signal in = noise(4096);
    Signal out = in;
    runMono(crusher, out, 100); // blocks that do not line up with the holds
    for (std::size_t i = 0; i < out.size(); ++i)
        CHECK(out[i] == in[i - i % 4]);
}

TEST_CASE("Bitcrusher anti-alias filter removes what would fold down", "[bitcrusher]")
{
    const auto level = [](bool antiAlias) {
        Bitcrusher crusher;
        crusher.setParameter(Bitcrusher::kDownsample, 4.0);
        crusher.setParameter(Bitcrusher::kAntiAlias, antiAlias ? 1.0 : 0.0);
        crusher.prepare(kRate, 512);
        Signal signal = sine(15000.0, 48000, kRate);
        runMono(crusher, signal);
        return rmsDb(signal, 4800);
    };
    CHECK(level(true) < level(false) - 20.0);
}

TEST_CASE("Bitcrusher jitter varies the hold", "[bitcrusher]")
{
    Bitcrusher crusher;
    crusher.setParameter(Bitcrusher::kDownsample, 8.0);
    crusher.setParameter(Bitcrusher::kJitter, 100.0);
    crusher.prepare(kRate, 512);
    Signal signal = noise(48000);
    runMono(crusher, signal);
    std::set<int> holds;
    int length = 1;
    for (std::size_t i = 1; i < signal.size(); ++i) {
        if (signal[i] == signal[i - 1]) {
            ++length;
        } else {
            holds.insert(length);
            length = 1;
        }
    }
    CHECK(holds.size() > 3);
    CHECK(*holds.begin() >= 4);
    CHECK(*holds.rbegin() <= 12);
}

TEST_CASE("Bitcrusher drive and output set the level", "[bitcrusher]")
{
    Bitcrusher crusher;
    crusher.setParameter(Bitcrusher::kOutput, -6.0);
    crusher.setParameter(Bitcrusher::kDrive, 6.0);
    crusher.prepare(kRate, 512);
    const Signal in = sine(1000.0, 4800, kRate, 0.25f);
    Signal out = in;
    runMono(crusher, out);
    CHECK(rmsDb(out) - rmsDb(in) == Approx(0.0).margin(1.0e-3));
}

TEST_CASE("Bitcrusher turns silence into silence", "[bitcrusher]")
{
    Bitcrusher crusher;
    crusher.setParameter(Bitcrusher::kBits, 4.0);
    crusher.setParameter(Bitcrusher::kDownsample, 3.0);
    crusher.setParameter(Bitcrusher::kAntiAlias, 1.0);
    crusher.prepare(kRate, 512);
    std::vector<Signal> io{constant(8000, 0.0f), constant(8000, 0.0f)};
    run(crusher, io);
    CHECK(allZero(io[0]));
    CHECK(allZero(io[1]));
}

TEST_CASE("Bitcrusher reset starts over", "[bitcrusher]")
{
    Bitcrusher crusher;
    crusher.setParameter(Bitcrusher::kBits, 5.0);
    crusher.setParameter(Bitcrusher::kDownsample, 2.5);
    crusher.setParameter(Bitcrusher::kJitter, 50.0);
    crusher.setParameter(Bitcrusher::kDither, 50.0);
    crusher.setParameter(Bitcrusher::kAntiAlias, 1.0);
    crusher.prepare(kRate, 512);
    Signal first = noise(6000);
    runMono(crusher, first);
    crusher.reset();
    Signal second = noise(6000);
    runMono(crusher, second);
    CHECK(first == second);
}

TEST_CASE("Bitcrusher stays bounded at every sample rate and setting", "[bitcrusher]")
{
    for (const double rate : {44100.0, 192000.0}) {
        Bitcrusher crusher;
        crusher.setParameter(Bitcrusher::kDrive, 24.0);
        crusher.setParameter(Bitcrusher::kBits, 1.0);
        crusher.setParameter(Bitcrusher::kDownsample, 64.0);
        crusher.setParameter(Bitcrusher::kJitter, 100.0);
        crusher.setParameter(Bitcrusher::kAntiAlias, 1.0);
        crusher.setParameter(Bitcrusher::kOutput, 12.0);
        crusher.prepare(rate, 512);
        Signal signal = noise(static_cast<int>(rate), 6);
        signal[3] = std::numeric_limits<float>::quiet_NaN();
        runMono(crusher, signal);
        Signal after = noise(8192, 7);
        runMono(crusher, after);
        CHECK(allFinite(after));
        CHECK(peak(after) <= 4.0f); // 1.0 at +12 dB
    }
}
