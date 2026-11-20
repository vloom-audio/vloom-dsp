// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "ModTestSignals.hpp"
#include "vloom/dsp/effects/DeEsser.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string_view>

using Catch::Approx;
using vloom::dsp::DeEsser;
using namespace modtest;

namespace {

constexpr double kRate = 48000.0;

/// How much `deEsser` turns down a sine at `frequency` (-10 dBFS), in dB.
double reductionDb(DeEsser& deEsser, double frequency, double rate = kRate)
{
    deEsser.prepare(rate, 512);
    const Signal in = sine(frequency, static_cast<int>(rate), rate, 0.316f);
    Signal out = in;
    runMono(deEsser, out);
    const auto from = static_cast<std::size_t>(rate / 2);
    return rmsDb(in, from) - rmsDb(out, from);
}

} // namespace

TEST_CASE("De-esser describes itself and its parameters", "[deesser]")
{
    CHECK(std::string_view(DeEsser::spec().id) == "vloom.deesser");
    checkParameters<DeEsser>({"Frequency", "Threshold", "Listen", "Range", "Mode"});
    CHECK(DeEsser::parameters()[DeEsser::kListen].stepped);
    CHECK(DeEsser::parameters()[DeEsser::kMode].stepped);
    CHECK(DeEsser().latencyFrames() == 0);
    CHECK(DeEsser().gainReductionDb() == 0.0f);
}

TEST_CASE("De-esser turns a sibilant band down more than a low tone", "[deesser]")
{
    for (const auto mode : {DeEsser::Mode::Wideband, DeEsser::Mode::SplitBand}) {
        INFO("mode: " << static_cast<int>(mode));
        DeEsser deEsser;
        deEsser.setParameter(DeEsser::kMode, static_cast<double>(mode));
        deEsser.setParameter(DeEsser::kRange, 12.0);

        const double sibilant = reductionDb(deEsser, 12000.0);
        CHECK(sibilant > 6.0);
        CHECK(deEsser.gainReductionDb() == Approx(12.0).margin(0.01)); // well over: the whole range
        const double low = reductionDb(deEsser, 300.0);
        CHECK(low == Approx(0.0).margin(0.01));
        CHECK(deEsser.gainReductionDb() == 0.0f);
    }
}

TEST_CASE("De-esser in wideband mode takes off the overshoot, up to the range", "[deesser]")
{
    DeEsser deEsser;
    deEsser.setParameter(DeEsser::kMode, static_cast<double>(DeEsser::Mode::Wideband));
    deEsser.setParameter(DeEsser::kRange, 24.0);
    deEsser.setParameter(DeEsser::kFrequency, 2000.0);
    deEsser.setParameter(DeEsser::kThreshold, -20.0);
    // A 10 kHz tone at -10 dBFS passes the high-pass whole: 10 dB over.
    const double reduction = reductionDb(deEsser, 10000.0);
    CHECK(reduction == Approx(10.0).margin(0.5));
}

TEST_CASE("De-esser below its threshold passes the input through bit for bit", "[deesser]")
{
    for (const double mode : {0.0, 1.0}) {
        DeEsser deEsser;
        deEsser.setParameter(DeEsser::kMode, mode);
        deEsser.prepare(kRate, 512);
        const std::vector<Signal> in{noise(20000, 1, 0.003f), noise(20000, 2, 0.003f)};
        std::vector<Signal> out = in;
        run(deEsser, out);
        CHECK(out == in);
    }
}

TEST_CASE("De-esser listen plays the detected band alone", "[deesser]")
{
    DeEsser deEsser;
    deEsser.setParameter(DeEsser::kListen, 1.0);
    deEsser.setParameter(DeEsser::kThreshold, 0.0);
    CHECK(reductionDb(deEsser, 300.0) > 40.0); // far below the band: gone
    CHECK(reductionDb(deEsser, 12000.0) == Approx(0.0).margin(0.5)); // in the band: kept
}

TEST_CASE("De-esser turns silence into silence", "[deesser]")
{
    DeEsser deEsser;
    deEsser.setParameter(DeEsser::kThreshold, -60.0);
    deEsser.prepare(kRate, 512);
    std::vector<Signal> io{constant(8000, 0.0f), constant(8000, 0.0f)};
    run(deEsser, io);
    CHECK(allZero(io[0]));
    CHECK(allZero(io[1]));
}

TEST_CASE("De-esser reset clears its detector and meter", "[deesser]")
{
    DeEsser deEsser;
    deEsser.prepare(kRate, 512);
    Signal signal = sine(8000.0, 4800, kRate, 0.5f);
    runMono(deEsser, signal);
    CHECK(deEsser.gainReductionDb() > 0.0f);
    deEsser.reset();
    CHECK(deEsser.gainReductionDb() == 0.0f);
    Signal quiet = noise(2000, 1, 0.003f);
    const Signal in = quiet;
    runMono(deEsser, quiet);
    CHECK(quiet == in); // no reduction left over from before
}

TEST_CASE("De-esser stays stable at every sample rate and setting", "[deesser]")
{
    for (const double rate : {44100.0, 192000.0}) {
        for (const double frequency : {2000.0, 16000.0}) {
            DeEsser deEsser;
            deEsser.setParameter(DeEsser::kFrequency, frequency);
            deEsser.setParameter(DeEsser::kThreshold, -60.0);
            deEsser.setParameter(DeEsser::kRange, 24.0);
            deEsser.prepare(rate, 512);
            Signal signal = noise(static_cast<int>(rate), 7);
            signal[50] = std::numeric_limits<float>::quiet_NaN();
            runMono(deEsser, signal);
            Signal after = noise(4096, 8);
            runMono(deEsser, after);
            CHECK(allFinite(after));
            CHECK(peak(after) <= 1.5f);
        }
    }
}
