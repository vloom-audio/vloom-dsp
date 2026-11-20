// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "support/EffectTesting.hpp"
#include "vloom/dsp/effects/Compressor.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <string_view>

using Catch::Approx;
using vloom::dsp::Compressor;
using namespace vloom::test;

namespace {

constexpr double kRate = 48000.0;

Compressor prepared(double rate = kRate)
{
    Compressor compressor;
    compressor.prepare(rate, 512);
    return compressor;
}

double db(double gain) { return 20.0 * std::log10(gain); }

} // namespace

TEST_CASE("Compressor describes itself and its parameters", "[compressor]")
{
    CHECK(std::string_view(Compressor::spec().id) == "vloom.compressor");
    checkParameterList<Compressor>();
    CHECK(Compressor().latencyFrames() == 0);
    STATIC_CHECK(vloom::dsp::SidechainEffect<Compressor>);
    STATIC_CHECK_FALSE(vloom::dsp::TempoAware<Compressor>);
}

TEST_CASE("Compressor's static curve: threshold, ratio and knee", "[compressor]")
{
    // Hard knee: nothing below the threshold, 1/ratio of the overshoot above.
    CHECK(Compressor::staticGainDb(-30.0, -20.0, 4.0, 0.0) == 0.0);
    CHECK(Compressor::staticGainDb(-10.0, -20.0, 4.0, 0.0) == Approx(-7.5));
    CHECK(Compressor::staticGainDb(0.0, -20.0, 20.0, 0.0) == Approx(-19.0));
    CHECK(Compressor::staticGainDb(0.0, -20.0, 1.0, 0.0) == 0.0);
    // Soft knee: half the hard reduction's slope at the threshold, and the
    // same curve as the hard knee outside it.
    CHECK(Compressor::staticGainDb(-20.0, -20.0, 4.0, 10.0) == Approx(-0.75 * 25.0 / 20.0));
    CHECK(Compressor::staticGainDb(-25.0, -20.0, 4.0, 10.0) == Approx(0.0).margin(1e-12));
    CHECK(Compressor::staticGainDb(-15.0, -20.0, 4.0, 10.0) == Approx(-3.75));
    CHECK(Compressor::staticGainDb(-10.0, -20.0, 4.0, 10.0) == Approx(-7.5));
    // Smooth: no step anywhere.
    double last = Compressor::staticGainDb(-60.0, -20.0, 8.0, 12.0);
    for (double in = -60.0; in <= 0.0; in += 0.01) {
        const double gain = Compressor::staticGainDb(in, -20.0, 8.0, 12.0);
        CHECK(gain <= last + 1e-12); // never less reduction for a louder input
        CHECK(std::abs(gain - last) < 0.01);
        last = gain;
    }
}

TEST_CASE("Compressor settles on its static curve (peak detector)", "[compressor]")
{
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0}) {
        for (const double threshold : {-30.0, -18.0, -6.0}) {
            for (const double ratio : {2.0, 4.0, 10.0}) {
                Compressor compressor = prepared(rate);
                compressor.setParameter(Compressor::kThreshold, threshold);
                compressor.setParameter(Compressor::kRatio, ratio);
                compressor.setParameter(Compressor::kKnee, 0.0);
                compressor.setParameter(Compressor::kAttack, 1.0);
                compressor.setParameter(Compressor::kRelease, 50.0);
                const double inDb = -3.0;
                const auto in = static_cast<float>(std::pow(10.0, inDb / 20.0));
                const Signal out = renderMono(compressor, constant(static_cast<int>(rate / 2), in));
                const double expectedGr = Compressor::staticGainDb(inDb, threshold, ratio, 0.0);
                INFO("rate " << rate << ", threshold " << threshold << ", ratio " << ratio);
                CHECK(db(static_cast<double>(out.back() / in)) == Approx(expectedGr).margin(0.01));
                CHECK(compressor.gainReductionDb() == Approx(-expectedGr).margin(0.01));
            }
        }
    }
}

TEST_CASE("Compressor's RMS detector compresses a sine by its RMS level", "[compressor]")
{
    Compressor compressor = prepared();
    compressor.setParameter(Compressor::kDetector, 1.0);
    compressor.setParameter(Compressor::kThreshold, -24.0);
    compressor.setParameter(Compressor::kRatio, 3.0);
    compressor.setParameter(Compressor::kKnee, 0.0);
    compressor.setParameter(Compressor::kAttack, 5.0);
    compressor.setParameter(Compressor::kRelease, 200.0);
    const Signal in = sine(1000.0, 48000, kRate, 0.5f);
    const Signal out = renderMono(compressor, in);
    const double inRms = rmsDb(in, 24000);
    const double expected = Compressor::staticGainDb(inRms, -24.0, 3.0, 0.0);
    CHECK(rmsDb(out, 24000) - inRms == Approx(expected).margin(0.3));
}

TEST_CASE("Compressor attacks and releases at its set times", "[compressor]")
{
    Compressor compressor = prepared();
    compressor.setParameter(Compressor::kThreshold, -40.0);
    compressor.setParameter(Compressor::kRatio, 20.0);
    compressor.setParameter(Compressor::kKnee, 0.0);
    compressor.setParameter(Compressor::kAttack, 10.0);
    compressor.setParameter(Compressor::kRelease, 100.0);
    const Signal out = renderMono(compressor, constant(48000, 1.0f));
    // One attack time in, 63 % of the full reduction (38 dB) is reached.
    const double atAttack = -db(static_cast<double>(out[480]));
    CHECK(atAttack == Approx(38.0 * (1.0 - std::exp(-1.0))).margin(0.3));

    Signal quiet = constant(48000, 0.001f); // -60 dB: below the threshold
    quiet = renderMono(compressor, quiet);
    const double atRelease = -db(static_cast<double>(quiet[4800] / 0.001f));
    CHECK(atRelease == Approx(38.0 * std::exp(-1.0)).margin(0.3));
}

TEST_CASE("Compressor at 1:1 or 0 % mix passes the input through bit for bit", "[compressor]")
{
    const Signal in = noise(8192, 5);
    {
        Compressor compressor = prepared();
        compressor.setParameter(Compressor::kRatio, 1.0);
        CHECK(renderMono(compressor, in) == in);
        CHECK(compressor.gainReductionDb() == 0.0f);
    }
    {
        Compressor compressor = prepared();
        compressor.setParameter(Compressor::kThreshold, -60.0);
        compressor.setParameter(Compressor::kMix, 0.0);
        (void)renderMono(compressor, in); // through the mix glide
        CHECK(renderMono(compressor, in) == in);
        CHECK(compressor.gainReductionDb() > 10.0f); // it does compress, unheard
    }
}

TEST_CASE("Compressor's makeup and auto makeup", "[compressor]")
{
    Compressor compressor = prepared();
    compressor.setParameter(Compressor::kThreshold, -20.0);
    compressor.setParameter(Compressor::kRatio, 5.0);
    compressor.setParameter(Compressor::kKnee, 0.0);
    CHECK(compressor.transferDb(-30.0) == Approx(-30.0));
    CHECK(compressor.transferDb(0.0) == Approx(-16.0));
    compressor.setParameter(Compressor::kAutoMakeup, 1.0);
    CHECK(compressor.transferDb(0.0) == Approx(-8.0)); // half the reduction at 0 dBFS
    compressor.setParameter(Compressor::kMakeup, 3.0);
    CHECK(compressor.transferDb(-30.0) == Approx(-30.0 + 8.0 + 3.0));

    // And the audio follows.
    compressor.reset();
    const Signal out = renderMono(compressor, constant(48000, 0.01f)); // -40 dB
    CHECK(db(static_cast<double>(out.back()) / 0.01) == Approx(11.0).margin(0.01));
}

TEST_CASE("Compressor's stereo link", "[compressor]")
{
    for (const double link : {0.0, 100.0}) {
        Compressor compressor = prepared();
        compressor.setParameter(Compressor::kThreshold, -20.0);
        compressor.setParameter(Compressor::kRatio, 4.0);
        compressor.setParameter(Compressor::kKnee, 0.0);
        compressor.setParameter(Compressor::kStereoLink, link);
        Audio audio{constant(24000, 1.0f), constant(24000, 0.01f)}; // 0 dB left, -40 dB right
        render(compressor, audio);
        const double right = db(static_cast<double>(audio[1].back()) / 0.01);
        INFO("link " << link);
        CHECK(db(static_cast<double>(audio[0].back())) == Approx(-15.0).margin(0.01));
        CHECK(right == Approx(link > 0.0 ? -15.0 : 0.0).margin(0.01));
    }
}

TEST_CASE("Compressor keys off a sidechain", "[compressor]")
{
    Compressor compressor = prepared();
    compressor.setParameter(Compressor::kThreshold, -30.0);
    compressor.setParameter(Compressor::kRatio, 10.0);
    compressor.setParameter(Compressor::kKnee, 0.0);
    const Signal key = constant(24000, 1.0f);
    Signal main = constant(24000, 0.01f); // -40 dB: below the threshold on its own

    // Without a key it leaves the quiet input alone.
    CHECK(renderMono(compressor, main).back() == 0.01f);

    // A loud mono key turns both channels down by the key's reduction.
    compressor.reset();
    Signal left = main;
    Signal right = main;
    std::array<float*, 2> channels{left.data(), right.data()};
    std::array<const float*, 1> keys{key.data()};
    compressor.processWithSidechain(channels.data(), keys.data(), 2, 1, 24000);
    CHECK(db(static_cast<double>(left.back()) / 0.01) == Approx(-27.0).margin(0.01));
    CHECK(right.back() == left.back());

    // No key pointer or no key channels: keys off the input, like process().
    compressor.reset();
    Signal plain = main;
    float* one = plain.data();
    compressor.processWithSidechain(&one, nullptr, 1, 0, 24000);
    CHECK(plain.back() == 0.01f);
}

TEST_CASE("Compressor's sidechain filter ignores low notes; Listen plays the key", "[compressor]")
{
    const Signal key = sine(40.0, 48000, kRate, 1.0f);
    const auto reductionFor = [&](bool filter) {
        Compressor compressor = prepared();
        compressor.setParameter(Compressor::kThreshold, -30.0);
        compressor.setParameter(Compressor::kRatio, 10.0);
        compressor.setParameter(Compressor::kSidechainFilter, filter ? 1.0 : 0.0);
        compressor.setParameter(Compressor::kSidechainFrequency, 500.0);
        Signal main = constant(48000, 0.01f);
        float* channel = main.data();
        const float* keyChannel = key.data();
        compressor.processWithSidechain(&channel, &keyChannel, 1, 1, 48000);
        return compressor.gainReductionDb();
    };
    const float unfiltered = reductionFor(false);
    const float filtered = reductionFor(true);
    CHECK(unfiltered > 20.0f);
    CHECK(filtered < unfiltered - 15.0f);

    Compressor compressor = prepared();
    compressor.setParameter(Compressor::kSidechainListen, 1.0);
    Signal main = constant(512, 0.01f);
    float* channel = main.data();
    const float* keyChannel = key.data();
    compressor.processWithSidechain(&channel, &keyChannel, 1, 1, 512);
    CHECK(main == Signal(key.begin(), key.begin() + 512));
}

TEST_CASE("Compressor: silence, reset, no allocation, stable everywhere", "[compressor]")
{
    Compressor compressor = prepared();
    checkSilentWhenSilent(compressor);
    checkNoAllocation(compressor);
    checkNoAllocation(compressor, 1);

    // reset() ends a reduction at once.
    compressor.setParameter(Compressor::kThreshold, -40.0);
    (void)renderMono(compressor, constant(4800, 1.0f));
    CHECK(compressor.gainReductionDb() > 10.0f);
    compressor.reset();
    CHECK(compressor.gainReductionDb() == 0.0f);
    CHECK(renderMono(compressor, constant(1, 0.001f)).front() == 0.001f);

    // Not prepared: passes through.
    Compressor fresh;
    const Signal in = noise(256);
    CHECK(renderMono(fresh, in) == in);

    checkStableEverywhere<Compressor>();
}
