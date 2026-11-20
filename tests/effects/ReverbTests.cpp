// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "support/EffectTesting.hpp"
#include "vloom/dsp/effects/Reverb.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string_view>

using Catch::Approx;
using vloom::dsp::Reverb;
using namespace vloom::test;

namespace {

constexpr double kRate = 48000.0;

/// A fully wet reverb with the given decay; no damping or cuts in the way.
Reverb wet(double decay, double rate = kRate)
{
    Reverb reverb;
    reverb.prepare(rate, 512);
    reverb.setParameter(Reverb::kMix, 100.0);
    reverb.setParameter(Reverb::kDecay, decay);
    reverb.setParameter(Reverb::kDamping, 0.0);
    reverb.setParameter(Reverb::kLowCut, 20.0);
    reverb.setParameter(Reverb::kHighCut, 20000.0);
    reverb.setParameter(Reverb::kPreDelay, 0.0);
    reverb.reset();
    return reverb;
}

/// The level of `audio[0]` between `from` and `to` seconds, in dB.
double levelDb(const Audio& audio, double from, double to, double rate = kRate)
{
    return rmsDb(audio[0], static_cast<std::size_t>(from * rate), static_cast<std::size_t>(to * rate));
}

} // namespace

TEST_CASE("Reverb describes itself and its parameters", "[reverb]")
{
    CHECK(std::string_view(Reverb::spec().id) == "vloom.reverb");
    checkParameterList<Reverb>();
    CHECK(Reverb().latencyFrames() == 0);
}

TEST_CASE("Reverb's tail decays at the set rate and stays finite", "[reverb]")
{
    for (const double rate : {44100.0, 96000.0, 192000.0}) {
        for (const double decay : {0.5, 2.0}) {
            Reverb reverb = wet(decay, rate);
            const auto frames = static_cast<int>(rate * 3.0 * decay);
            Audio audio{impulse(frames), impulse(frames)};
            render(reverb, audio);
            INFO("rate " << rate << ", decay " << decay);
            REQUIRE(allFinite(audio));
            CHECK(peak(audio[0]) > 0.01f);
            CHECK(peak(audio[0]) < 2.0f);
            // Over one decay time the level falls by about 60 dB.
            const double early = levelDb(audio, 0.2 * decay, 0.4 * decay, rate);
            const double late = levelDb(audio, 1.2 * decay, 1.4 * decay, rate);
            CHECK(early - late == Approx(60.0).margin(8.0));
        }
    }
}

TEST_CASE("Reverb: a longer decay rings longer, damping darkens the tail", "[reverb]")
{
    Reverb shortReverb = wet(0.5);
    Reverb longReverb = wet(4.0);
    Audio a{impulse(96000)};
    Audio b{impulse(96000)};
    render(shortReverb, a);
    render(longReverb, b);
    CHECK(levelDb(b, 1.0, 1.5) > levelDb(a, 1.0, 1.5) + 40.0);

    // High frequencies die sooner with damping: compare the top of the tail.
    const auto highLevel = [](double damping) {
        Reverb reverb = wet(2.0);
        reverb.setParameter(Reverb::kDamping, damping);
        reverb.reset();
        Audio audio{impulse(48000)};
        render(reverb, audio);
        // A crude high-pass: the difference between neighbouring samples.
        Signal edges(audio[0].size(), 0.0f);
        for (std::size_t i = 1; i < edges.size(); ++i)
            edges[i] = audio[0][i] - audio[0][i - 1];
        return rmsDb(edges, 24000, 48000) - rmsDb(audio[0], 24000, 48000);
    };
    CHECK(highLevel(90.0) < highLevel(0.0) - 6.0);
}

TEST_CASE("Reverb's freeze holds the tail", "[reverb]")
{
    Reverb reverb = wet(0.5);
    Audio audio{noise(9600, 4, 0.5f), noise(9600, 5, 0.5f)};
    render(reverb, audio);
    reverb.setParameter(Reverb::kFreeze, 1.0);
    // Frozen: new input is not heard, and the tail does not decay.
    Audio held{noise(96000, 6, 0.5f), noise(96000, 7, 0.5f)};
    render(reverb, held);
    const double start = levelDb(held, 0.2, 0.4);
    const double end = levelDb(held, 1.6, 1.8);
    CHECK(start > -40.0);
    CHECK(std::abs(end - start) < 1.5);
    REQUIRE(allFinite(held));

    // Released: it decays again.
    reverb.setParameter(Reverb::kFreeze, 0.0);
    Audio after{Signal(96000, 0.0f), Signal(96000, 0.0f)};
    render(reverb, after);
    CHECK(levelDb(after, 1.5, 2.0) < start - 60.0);
}

TEST_CASE("Reverb's pre-delay holds the reverb back", "[reverb]")
{
    Reverb reverb = wet(1.0);
    reverb.setParameter(Reverb::kPreDelay, 100.0);
    reverb.reset();
    Audio audio{impulse(24000)};
    render(reverb, audio);
    // Nothing before the pre-delay plus the shortest path through the network.
    CHECK(peak(audio[0], 0, 4800) == 0.0f);
    CHECK(peak(audio[0], 4800) > 0.001f);
}

TEST_CASE("Reverb at 0 % mix passes the input through; width 0 is mono", "[reverb]")
{
    Reverb reverb;
    reverb.prepare(kRate, 512);
    reverb.setParameter(Reverb::kMix, 0.0);
    reverb.reset();
    const Signal in = noise(8192);
    Audio audio{in, noise(8192, 2)};
    render(reverb, audio);
    CHECK(audio[0] == in);

    Reverb mono = wet(1.0);
    mono.setParameter(Reverb::kWidth, 0.0);
    mono.reset();
    Audio stereo{impulse(9600), Signal(9600, 0.0f)};
    render(mono, stereo);
    CHECK(peak(stereo[0]) > 0.001f);
    for (std::size_t i = 0; i < stereo[0].size(); ++i)
        REQUIRE(stereo[0][i] == Approx(stereo[1][i]).margin(1e-6));

    // At full width the sides differ (a stereo image).
    Reverb wide = wet(1.0);
    Audio image{impulse(9600), impulse(9600)};
    render(wide, image);
    double difference = 0.0;
    for (std::size_t i = 0; i < image[0].size(); ++i)
        difference += std::abs(static_cast<double>(image[0][i] - image[1][i]));
    CHECK(difference > 0.1);
}

TEST_CASE("Reverb: silence, reset, no allocation, stable everywhere", "[reverb]")
{
    Reverb reverb;
    reverb.prepare(kRate, 512);
    checkSilentWhenSilent(reverb);
    checkNoAllocation(reverb);
    checkNoAllocation(reverb, 1);

    // reset() ends the tail.
    reverb.setParameter(Reverb::kDecay, 20.0);
    (void)renderMono(reverb, noise(4800));
    reverb.reset();
    checkSilentWhenSilent(reverb, 2, 4800);

    // Moving the size while it rings stays finite and bounded.
    Audio audio{noise(48000), noise(48000, 3)};
    for (int block = 0; block < 10; ++block) {
        reverb.setParameter(Reverb::kSize, block % 2 == 0 ? 0.0 : 100.0);
        Audio part{Signal(audio[0].begin() + block * 4800, audio[0].begin() + (block + 1) * 4800),
            Signal(audio[1].begin() + block * 4800, audio[1].begin() + (block + 1) * 4800)};
        render(reverb, part);
        REQUIRE(allFinite(part));
        CHECK(peak(part[0]) < 4.0f);
    }

    checkStableEverywhere<Reverb>();
}
