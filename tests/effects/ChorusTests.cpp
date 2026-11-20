// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "ModTestSignals.hpp"
#include "vloom/dsp/effects/Chorus.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string_view>

using Catch::Approx;
using vloom::dsp::Chorus;
using namespace modtest;

namespace {

constexpr double kRate = 48000.0;

} // namespace

TEST_CASE("Chorus describes itself and its parameters", "[chorus]")
{
    CHECK(std::string_view(Chorus::spec().id) == "vloom.chorus");
    checkParameters<Chorus>(
        {"Rate", "Sync", "Note", "Depth", "Delay", "Voices", "Feedback", "Spread", "Width", "Mix"});
    CHECK(Chorus::parameters()[Chorus::kSync].stepped);
    CHECK(Chorus::parameters()[Chorus::kNote].stepped);
    CHECK(Chorus::parameters()[Chorus::kVoices].stepped);
    CHECK(Chorus::parameters()[Chorus::kVoices].minValue == 1.0);
    CHECK(Chorus::parameters()[Chorus::kVoices].maxValue == 4.0);
    CHECK(Chorus().latencyFrames() == 0);
}

TEST_CASE("Chorus turns silence into silence", "[chorus]")
{
    Chorus chorus;
    chorus.setParameter(Chorus::kFeedback, 90.0);
    chorus.setParameter(Chorus::kVoices, 4.0);
    chorus.prepare(kRate, 512);
    std::vector<Signal> io{constant(48000, 0.0f), constant(48000, 0.0f)};
    run(chorus, io);
    CHECK(allZero(io[0]));
    CHECK(allZero(io[1]));
}

TEST_CASE("Chorus at Mix 0 passes the input through bit for bit", "[chorus]")
{
    Chorus chorus;
    chorus.setParameter(Chorus::kMix, 0.0);
    chorus.setParameter(Chorus::kFeedback, 60.0);
    chorus.prepare(kRate, 512);
    const std::vector<Signal> in{noise(20000, 1), noise(20000, 2)};
    std::vector<Signal> out = in;
    run(chorus, out);
    CHECK(out == in);
}

TEST_CASE("Chorus passes through before prepare", "[chorus]")
{
    Chorus chorus;
    const Signal in = noise(1024);
    Signal out = in;
    runMono(chorus, out);
    CHECK(out == in);
}

TEST_CASE("Chorus modulates the pitch of a tone", "[chorus]")
{
    const auto share = [](double depth) {
        Chorus chorus;
        chorus.setParameter(Chorus::kVoices, 1.0);
        chorus.setParameter(Chorus::kMix, 100.0);
        chorus.setParameter(Chorus::kRate, 2.0);
        chorus.setParameter(Chorus::kDepth, depth);
        chorus.prepare(kRate, 512);
        Signal signal = sine(1000.0, 96000, kRate);
        runMono(chorus, signal);
        return powerShareAt(signal, 1000.0, kRate, 48000, 48000);
    };
    CHECK(share(0.0) > 0.999); // a plain delay: still a pure tone
    CHECK(share(100.0) < 0.9); // side bands around the tone
}

TEST_CASE("Chorus widens a mono source by its spread", "[chorus]")
{
    const auto difference = [](double spread, int voices) {
        Chorus chorus;
        chorus.setParameter(Chorus::kSpread, spread);
        chorus.setParameter(Chorus::kVoices, voices);
        chorus.setParameter(Chorus::kMix, 100.0);
        chorus.setParameter(Chorus::kDepth, 100.0);
        chorus.prepare(kRate, 512);
        const Signal source = sine(440.0, 48000, kRate);
        std::vector<Signal> io{source, source};
        run(chorus, io);
        Signal side(io[0].size());
        for (std::size_t i = 0; i < side.size(); ++i)
            side[i] = io[0][i] - io[1][i];
        return meanSquare(side, 24000);
    };
    for (int voices = 1; voices <= Chorus::kMaxVoices; ++voices) {
        INFO("voices: " << voices);
        CHECK(difference(0.0, voices) == 0.0);
        CHECK(difference(100.0, voices) > 1.0e-3);
    }
}

TEST_CASE("Chorus stays bounded at full feedback at every sample rate", "[chorus]")
{
    for (const double rate : {44100.0, 96000.0, 192000.0}) {
        Chorus chorus;
        chorus.setParameter(Chorus::kFeedback, 90.0);
        chorus.setParameter(Chorus::kDepth, 100.0);
        chorus.setParameter(Chorus::kRate, 10.0);
        chorus.setParameter(Chorus::kVoices, 4.0);
        chorus.setParameter(Chorus::kDelay, 1.0);
        chorus.setParameter(Chorus::kMix, 100.0);
        chorus.prepare(rate, 512);
        std::vector<Signal> io{noise(static_cast<int>(rate) * 2, 3), noise(static_cast<int>(rate) * 2, 4)};
        run(chorus, io, 333);
        CHECK(allFinite(io[0]));
        CHECK(peak(io[0]) < 20.0f);
        CHECK(peak(io[1]) < 20.0f);
    }
}

TEST_CASE("Chorus follows the tempo when synced", "[chorus]")
{
    Chorus chorus;
    CHECK(chorus.tempo() == 120.0);
    chorus.setParameter(Chorus::kRate, 3.0);
    CHECK(chorus.rateHz() == Approx(3.0));
    chorus.setParameter(Chorus::kSync, 1.0);
    chorus.setParameter(Chorus::kNote, vloom::dsp::note::kQuarter);
    CHECK(chorus.rateHz() == Approx(2.0));
    chorus.setTempo(90.0);
    CHECK(chorus.rateHz() == Approx(1.5));
    chorus.setTempo(std::numeric_limits<double>::quiet_NaN());
    chorus.setTempo(-10.0);
    CHECK(chorus.tempo() == 90.0);
}

TEST_CASE("Chorus reset clears its delay lines", "[chorus]")
{
    Chorus chorus;
    chorus.setParameter(Chorus::kFeedback, 80.0);
    chorus.prepare(kRate, 512);
    Signal signal = noise(4096);
    runMono(chorus, signal);
    chorus.reset();
    Signal silence = constant(8192, 0.0f);
    runMono(chorus, silence);
    CHECK(allZero(silence));
}

TEST_CASE("Chorus ignores NaN and clamps parameters", "[chorus]")
{
    Chorus chorus;
    chorus.setParameter(Chorus::kVoices, 9.0);
    chorus.setParameter(Chorus::kDepth, std::numeric_limits<double>::quiet_NaN());
    chorus.setParameter(999, 1.0);
    chorus.prepare(kRate, 512);
    Signal signal = noise(4096);
    signal[100] = std::numeric_limits<float>::quiet_NaN(); // must not stick in the delay line
    runMono(chorus, signal);
    Signal after = noise(9600);
    runMono(chorus, after);
    CHECK(allFinite(after));
}
