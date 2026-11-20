// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "support/EffectTesting.hpp"
#include "vloom/dsp/effects/Delay.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string_view>

using Catch::Approx;
using vloom::dsp::Delay;
using namespace vloom::test;

namespace {

constexpr double kRate = 48000.0;

/// A delay that only echoes: no dry signal, no filters, no feedback.
Delay echoOnly(double rate = kRate)
{
    Delay delay;
    delay.prepare(rate, 512);
    delay.setParameter(Delay::kSync, 0.0);
    delay.setParameter(Delay::kMix, 100.0);
    delay.setParameter(Delay::kFeedback, 0.0);
    delay.setParameter(Delay::kLowCut, 20.0);
    delay.setParameter(Delay::kHighCut, 20000.0);
    delay.reset(); // no glides
    return delay;
}

/// The frames where `signal` is not (nearly) zero.
std::vector<std::size_t> echoes(const Signal& signal, float above = 1.0e-4f)
{
    std::vector<std::size_t> at;
    for (std::size_t i = 0; i < signal.size(); ++i)
        if (std::abs(signal[i]) > above)
            at.push_back(i);
    return at;
}

} // namespace

TEST_CASE("Delay describes itself and its parameters", "[delay]")
{
    CHECK(std::string_view(Delay::spec().id) == "vloom.delay");
    checkParameterList<Delay>();
    CHECK(Delay().latencyFrames() == 0);
    STATIC_CHECK(vloom::dsp::TempoAware<Delay>);
    STATIC_CHECK_FALSE(vloom::dsp::SidechainEffect<Delay>);

    CHECK(std::string_view(Delay::noteName(0)) == "1/32");
    CHECK(std::string_view(Delay::noteName(5)) == "1/8");
    CHECK(std::string_view(Delay::noteName(Delay::kNoteCount - 1)) == "1/1");
    CHECK(std::string_view(Delay::noteName(Delay::kNoteCount)).empty());
    for (int i = 1; i < Delay::kNoteCount; ++i)
        CHECK(Delay::noteBeats(i) > Delay::noteBeats(i - 1));
}

TEST_CASE("Delay's echo lands on the right sample", "[delay]")
{
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0}) {
        Delay delay = echoOnly(rate);
        delay.setParameter(Delay::kTime, 10.0);
        delay.reset();
        const auto frames = static_cast<std::size_t>(std::lround(0.01 * rate));
        const Signal out = renderMono(delay, impulse(static_cast<int>(frames * 3), 7), 100);
        INFO("rate " << rate);
        REQUIRE(echoes(out) == std::vector<std::size_t>{frames + 7});
        CHECK(out[frames + 7] == 1.0f);
    }
}

TEST_CASE("Delay follows the host tempo when synced", "[delay]")
{
    Delay delay = echoOnly();
    delay.setParameter(Delay::kSync, 1.0);
    delay.setParameter(Delay::kNote, 5.0); // 1/8
    delay.reset();
    CHECK(delay.delaySeconds(false) == Approx(0.25)); // 120 BPM until told
    Signal out = renderMono(delay, impulse(30000));
    CHECK(echoes(out) == std::vector<std::size_t>{12000});

    delay.setTempo(100.0); // 1/8 at 100 BPM: 0.3 s
    delay.setTempo(std::nan("")); // ignored
    delay.setTempo(-5.0); // ignored
    delay.reset();
    out = renderMono(delay, impulse(30000));
    CHECK(echoes(out) == std::vector<std::size_t>{14400});

    delay.setParameter(Delay::kNote, 7.0); // 1/8 dotted: 0.45 s
    delay.reset();
    out = renderMono(delay, impulse(30000));
    CHECK(echoes(out) == std::vector<std::size_t>{21600});

    // Too long for the memory: kept at kMaxSeconds.
    delay.setTempo(20.0);
    delay.setParameter(Delay::kNote, Delay::kNoteCount - 1.0); // 12 s at 20 BPM
    delay.reset();
    out = renderMono(delay, impulse(static_cast<int>(Delay::kMaxSeconds * kRate) + 100));
    REQUIRE(echoes(out).size() == 1);
    CHECK(echoes(out)[0] >= static_cast<std::size_t>(Delay::kMaxSeconds * kRate) - 2);
}

TEST_CASE("Delay feeds back, and its filters thin out the repeats", "[delay]")
{
    Delay delay = echoOnly();
    delay.setParameter(Delay::kTime, 10.0);
    delay.setParameter(Delay::kFeedback, 50.0);
    delay.reset();
    const Signal out = renderMono(delay, impulse(2000));
    CHECK(echoes(out) == std::vector<std::size_t>{480, 960, 1440, 1920});
    CHECK(out[480] == Approx(1.0f));
    CHECK(out[960] == Approx(0.5f));
    CHECK(out[1440] == Approx(0.25f));

    // With a high cut, each repeat of noise is darker than the last.
    delay.setParameter(Delay::kHighCut, 2000.0);
    delay.setParameter(Delay::kFeedback, 90.0);
    delay.reset();
    Signal burst = noise(480);
    burst.resize(480 * 6, 0.0f);
    const Signal noisy = renderMono(delay, burst);
    // Echo k occupies frames [480 k, 480 (k + 1)); it loses level each time.
    double previous = 1.0e9;
    for (int k = 1; k < 5; ++k) {
        const double level
            = rmsDb(noisy, 480u * static_cast<std::size_t>(k), 480u * static_cast<std::size_t>(k + 1));
        CHECK(level < previous);
        previous = level;
    }
}

TEST_CASE("Delay ping-pong bounces between the sides", "[delay]")
{
    Delay delay = echoOnly();
    delay.setParameter(Delay::kMode, 1.0);
    delay.setParameter(Delay::kTime, 10.0);
    delay.setParameter(Delay::kFeedback, 50.0);
    delay.reset();
    Audio audio{impulse(2000), impulse(2000)};
    render(delay, audio);
    CHECK(echoes(audio[0]) == std::vector<std::size_t>{480, 1440});
    CHECK(echoes(audio[1]) == std::vector<std::size_t>{960, 1920});
    CHECK(audio[0][480] == Approx(1.0f)); // the input sum, (1 + 1) / 2
    CHECK(audio[1][960] == Approx(0.5f));

    // Width 0: both sides hear the same echoes.
    delay.setParameter(Delay::kWidth, 0.0);
    delay.reset();
    audio = {impulse(2000), impulse(2000)};
    render(delay, audio);
    CHECK(audio[0] == audio[1]);
}

TEST_CASE("Delay's stereo offset makes the right side longer", "[delay]")
{
    Delay delay = echoOnly();
    delay.setParameter(Delay::kTime, 10.0);
    delay.setParameter(Delay::kStereoOffset, 50.0);
    delay.reset();
    Audio audio{impulse(2000), impulse(2000)};
    render(delay, audio);
    CHECK(echoes(audio[0]) == std::vector<std::size_t>{480});
    CHECK(echoes(audio[1]) == std::vector<std::size_t>{720});
}

TEST_CASE("Delay at 0 % mix passes the input through bit for bit", "[delay]")
{
    Delay delay;
    delay.prepare(kRate, 512);
    delay.setParameter(Delay::kMix, 0.0);
    delay.reset();
    const Signal in = noise(8192);
    Audio audio{in, in};
    render(delay, audio);
    CHECK(audio[0] == in);
    CHECK(audio[1] == in);
}

TEST_CASE("Delay glides to a new time without a jump", "[delay]")
{
    Delay delay = echoOnly();
    delay.setParameter(Delay::kTime, 100.0);
    delay.reset();
    const Signal tone = sine(500.0, 48000, kRate, 0.5f);
    Signal out = renderMono(delay, tone);
    delay.setParameter(Delay::kTime, 300.0);
    Signal moved = renderMono(delay, sine(500.0, 9600, kRate, 0.5f));
    float largestStep = std::abs(moved[0] - out.back());
    for (std::size_t i = 1; i < moved.size(); ++i)
        largestStep = std::max(largestStep, std::abs(moved[i] - moved[i - 1]));
    CHECK(largestStep < 0.2f); // a 500 Hz sine of 0.5 moves at most 0.033 per frame; a click is much more
}

TEST_CASE("Delay: silence, reset, no allocation, stable everywhere", "[delay]")
{
    Delay delay;
    delay.prepare(kRate, 512);
    checkSilentWhenSilent(delay);
    checkNoAllocation(delay);
    checkNoAllocation(delay, 1);

    delay.setParameter(Delay::kFeedback, 80.0);
    (void)renderMono(delay, noise(4800));
    delay.reset();
    checkSilentWhenSilent(delay, 2, 96000);

    // NaN in the input does not get stuck in the loop.
    Signal poisoned = noise(1000);
    poisoned[10] = std::nanf("");
    (void)renderMono(delay, poisoned);
    Audio after{noise(96000)};
    render(delay, after);
    CHECK(allFinite(after));

    checkStableEverywhere<Delay>();
}
