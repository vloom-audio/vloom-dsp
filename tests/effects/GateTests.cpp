// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "support/EffectTesting.hpp"
#include "vloom/dsp/effects/Gate.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string_view>

using Catch::Approx;
using vloom::dsp::Gate;
using namespace vloom::test;

namespace {

constexpr double kRate = 48000.0;

Gate prepared(double rate = kRate)
{
    Gate gate;
    gate.prepare(rate, 512);
    gate.setParameter(Gate::kThreshold, -30.0);
    gate.setParameter(Gate::kHysteresis, 6.0);
    gate.setParameter(Gate::kAttack, 0.1);
    gate.setParameter(Gate::kHold, 10.0);
    gate.setParameter(Gate::kRelease, 5.0);
    return gate;
}

/// A burst of a 1 kHz sine at `level` between quiet stretches at `floor`.
Signal burst(float level, float floor, int quietFrames, int loudFrames)
{
    const int frames = 2 * quietFrames + loudFrames;
    Signal out = sine(1000.0, frames, kRate, floor);
    const Signal loud = sine(1000.0, frames, kRate, level);
    for (int i = quietFrames; i < quietFrames + loudFrames; ++i)
        out[static_cast<std::size_t>(i)] = loud[static_cast<std::size_t>(i)];
    return out;
}

} // namespace

TEST_CASE("Gate describes itself and its parameters", "[gate]")
{
    CHECK(std::string_view(Gate::spec().id) == "vloom.gate");
    checkParameterList<Gate>();
    CHECK(Gate().latencyFrames() == 0);
    STATIC_CHECK(vloom::dsp::SidechainEffect<Gate>);
}

TEST_CASE("Gate stays closed below the threshold and opens above it", "[gate]")
{
    for (const double rate : {44100.0, 96000.0, 192000.0}) {
        Gate gate = prepared(rate);
        // -46 dB: below the threshold. Fully closed (range at its minimum): silent.
        const Signal quiet = renderMono(gate, sine(1000.0, static_cast<int>(rate / 4), rate, 0.005f));
        CHECK(peak(quiet) == 0.0f);
        CHECK_FALSE(gate.isOpen());
        CHECK(gate.gainReductionDb() == Approx(80.0));

        // -6 dB: above. Open after the attack, passing the signal unchanged.
        const Signal in = sine(1000.0, static_cast<int>(rate / 4), rate, 0.5f);
        const Signal out = renderMono(gate, in);
        CHECK(gate.isOpen());
        const auto settled = static_cast<std::size_t>(rate / 100);
        for (std::size_t i = settled; i < out.size(); ++i)
            REQUIRE(out[i] == in[i]);
    }
}

TEST_CASE("Gate holds, then releases; hysteresis keeps it open in between", "[gate]")
{
    Gate gate = prepared();
    // Loud for 0.1 s, then -33 dB: under the threshold but over threshold
    // minus hysteresis (-36 dB), so it stays open.
    Signal signal = burst(0.5f, 0.0224f, 4800, 4800);
    Signal out = renderMono(gate, signal);
    CHECK(gate.isOpen());
    CHECK(rmsDb(out, out.size() - 2400) == Approx(rmsDb(signal, signal.size() - 2400)).margin(0.01));

    // Then -46 dB: under both. It holds 10 ms, then closes within a few
    // release times.
    gate.reset();
    signal = burst(0.5f, 0.005f, 4800, 4800);
    out = renderMono(gate, signal);
    CHECK_FALSE(gate.isOpen());
    const std::size_t end = 9600; // the burst ends here
    CHECK(out[end + 240] != 0.0f); // within the hold time: still open (gain 1)
    CHECK(std::abs(out[end + 240]) == Approx(std::abs(signal[end + 240])));
    CHECK(peak(out, end + 4800) < 1.0e-6f); // closed
}

TEST_CASE("Gate's range turns down instead of muting", "[gate]")
{
    Gate gate = prepared();
    gate.setParameter(Gate::kRange, -20.0);
    gate.reset();
    const Signal in = sine(1000.0, 24000, kRate, 0.005f);
    const Signal out = renderMono(gate, in);
    CHECK(rmsDb(out, 12000) - rmsDb(in, 12000) == Approx(-20.0).margin(0.01));
    CHECK(gate.gainReductionDb() == Approx(20.0).margin(0.01));
}

TEST_CASE("Gate opens on its sidechain key", "[gate]")
{
    Gate gate = prepared();
    const Signal key = burst(0.5f, 0.0f, 4800, 4800);
    Signal main = sine(220.0, static_cast<int>(key.size()), kRate, 0.001f); // far below the threshold
    const Signal in = main;
    float* channel = main.data();
    const float* keyChannel = key.data();
    gate.processWithSidechain(&channel, &keyChannel, 1, 1, static_cast<int>(main.size()));
    CHECK(peak(main, 0, 4800) == 0.0f);
    CHECK(peak(main, 4800 + 480, 9600) == Approx(peak(in, 4800 + 480, 9600)));
    CHECK(peak(main, 9600 + 4800) == 0.0f);

    // Listen plays the key.
    gate.setParameter(Gate::kSidechainListen, 1.0);
    Signal listened = in;
    channel = listened.data();
    gate.processWithSidechain(&channel, &keyChannel, 1, 1, 1024);
    CHECK(Signal(listened.begin(), listened.begin() + 1024) == Signal(key.begin(), key.begin() + 1024));
}

TEST_CASE("Gate: silence, reset, no allocation, stable everywhere", "[gate]")
{
    Gate gate = prepared();
    checkSilentWhenSilent(gate);
    checkNoAllocation(gate);
    checkNoAllocation(gate, 1);

    (void)renderMono(gate, sine(1000.0, 4800, kRate, 0.5f));
    CHECK(gate.isOpen());
    gate.reset();
    CHECK_FALSE(gate.isOpen());
    CHECK(renderMono(gate, constant(1, 0.001f)).front() == 0.0f);

    checkStableEverywhere<Gate>();
}
