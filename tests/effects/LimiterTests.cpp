// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "support/EffectTesting.hpp"
#include "vloom/dsp/effects/Limiter.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <string_view>

using Catch::Approx;
using vloom::dsp::Limiter;
using namespace vloom::test;

namespace {

constexpr double kRate = 48000.0;

/// The highest peak between samples, from a long windowed-sinc
/// interpolation at 8x: what a converter would make of the signal.
double truePeak(const Signal& signal, std::size_t from)
{
    constexpr int kHalf = 32;
    constexpr int kFactor = 8;
    double most = 0.0;
    for (std::size_t n = from + kHalf; n + kHalf < signal.size(); ++n) {
        for (int p = 0; p < kFactor; ++p) {
            const double position = static_cast<double>(n) + static_cast<double>(p) / kFactor;
            double sum = 0.0;
            for (int j = -kHalf; j <= kHalf; ++j) {
                const double t = position - static_cast<double>(static_cast<long long>(n) + j);
                const double sinc = t == 0.0 ? 1.0 : std::sin(std::numbers::pi * t) / (std::numbers::pi * t);
                const double window = 0.5 * (1.0 + std::cos(std::numbers::pi * t / (kHalf + 1)));
                sum += sinc * window
                    * static_cast<double>(signal[static_cast<std::size_t>(static_cast<long long>(n) + j)]);
            }
            most = std::max(most, std::abs(sum));
        }
    }
    return most;
}

/// The frame where the largest peak of `signal` is.
std::size_t peakAt(const Signal& signal)
{
    std::size_t at = 0;
    for (std::size_t i = 0; i < signal.size(); ++i)
        if (std::abs(signal[i]) > std::abs(signal[at]))
            at = i;
    return at;
}

} // namespace

TEST_CASE("Limiter describes itself and its parameters", "[limiter]")
{
    CHECK(std::string_view(Limiter::spec().id) == "vloom.limiter");
    checkParameterList<Limiter>();
    CHECK(Limiter().latencyFrames() == 0); // before prepare()
    STATIC_CHECK_FALSE(vloom::dsp::SidechainEffect<Limiter>);
}

TEST_CASE("Limiter's latency is its look-ahead, and matches the measured delay", "[limiter]")
{
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0}) {
        for (const double lookahead : {0.5, 2.0, 10.0}) {
            for (const bool truePeak : {false, true}) {
                Limiter limiter;
                limiter.prepare(rate, 512);
                limiter.setParameter(Limiter::kLookahead, lookahead);
                limiter.setParameter(Limiter::kTruePeak, truePeak ? 1.0 : 0.0);
                const int expected = static_cast<int>(std::lround(lookahead / 1000.0 * rate))
                    + (truePeak ? Limiter::kTruePeakFrames : 0);
                INFO("rate " << rate << ", look-ahead " << lookahead << " ms, true peak " << truePeak);
                CHECK(limiter.latencyFrames() == expected);
                const Signal out = renderMono(limiter, impulse(expected + 1000, 10, 0.25f), 100);
                CHECK(peakAt(out) == static_cast<std::size_t>(expected + 10));
                CHECK(out[static_cast<std::size_t>(expected + 10)] == 0.25f);
            }
        }
    }
}

TEST_CASE("Limiter below its ceiling passes the input through, delayed, bit for bit", "[limiter]")
{
    // Noise has peaks between its samples well above its sample peaks, so
    // true peak mode is given quieter noise.
    for (const bool truePeak : {false, true}) {
        Limiter limiter;
        limiter.prepare(kRate, 512);
        limiter.setParameter(Limiter::kTruePeak, truePeak ? 1.0 : 0.0);
        const Signal in = noise(24000, 3, truePeak ? 0.4f : 0.95f); // under -0.3 dB
        const Signal out = renderMono(limiter, in);
        const auto delay = static_cast<std::size_t>(limiter.latencyFrames());
        for (std::size_t i = 0; i < delay; ++i)
            REQUIRE(out[i] == 0.0f);
        for (std::size_t i = delay; i < out.size(); ++i)
            REQUIRE(out[i] == in[i - delay]);
        CHECK(limiter.gainReductionDb() == 0.0f);
    }
}

TEST_CASE("Limiter never lets a sample over its ceiling", "[limiter]")
{
    const int frames = 48000;
    Signal loudNoise = noise(frames, 11, 1.0f);
    Signal spikes(static_cast<std::size_t>(frames), 0.0f);
    for (std::size_t i = 100; i < spikes.size(); i += 997)
        spikes[i] = (i % 2 == 0) ? 4.0f : -4.0f;
    Signal bass = sine(55.0, frames, kRate, 2.0f);
    Signal steps = constant(frames, 0.1f);
    for (std::size_t i = 10000; i < 10050; ++i)
        steps[i] = 3.0f;

    for (const double rate : {44100.0, 96000.0}) {
        for (const double ceiling : {-0.3, -6.0}) {
            for (const double lookahead : {0.5, 5.0}) {
                for (const double release : {1.0, 200.0}) {
                    for (const Signal* signal : {&loudNoise, &spikes, &bass, &steps}) {
                        Limiter limiter;
                        limiter.prepare(rate, 512);
                        limiter.setParameter(Limiter::kCeiling, ceiling);
                        limiter.setParameter(Limiter::kLookahead, lookahead);
                        limiter.setParameter(Limiter::kRelease, release);
                        limiter.setParameter(Limiter::kInputGain, 12.0);
                        limiter.setParameter(Limiter::kTruePeak, 0.0);
                        limiter.reset();
                        Audio audio{*signal, noise(frames, 5, 0.3f)};
                        render(limiter, audio, 333);
                        const auto limit = static_cast<float>(std::pow(10.0, ceiling / 20.0));
                        INFO("rate " << rate << ", ceiling " << ceiling << ", look-ahead " << lookahead
                                     << ", release " << release);
                        CHECK(peak(audio[0]) <= limit);
                        CHECK(peak(audio[1]) <= limit);
                        CHECK(allFinite(audio));
                    }
                }
            }
        }
    }
}

TEST_CASE("Limiter's gain moves smoothly, without clipping the peaks it saw coming", "[limiter]")
{
    Limiter limiter;
    limiter.prepare(kRate, 512);
    limiter.setParameter(Limiter::kCeiling, -6.0);
    limiter.setParameter(Limiter::kTruePeak, 0.0);
    // A 1 kHz sine at 0 dB: limited by 6 dB, a clean sine again (not
    // clipped) once the gain has settled.
    const Signal in = sine(1000.0, 48000, kRate, 1.0f);
    const Signal out = renderMono(limiter, in);
    const auto delay = static_cast<std::size_t>(limiter.latencyFrames());
    const double gain = std::pow(10.0, -6.0 / 20.0);
    for (std::size_t i = 24000; i < out.size(); ++i)
        REQUIRE(
            static_cast<double>(out[i]) == Approx(gain * static_cast<double>(in[i - delay])).margin(1e-4));
    CHECK(limiter.gainReductionDb() == Approx(6.0).margin(0.01));
}

TEST_CASE("Limiter's true peak mode limits the peaks between samples", "[limiter]")
{
    // A sine at a quarter of the sample rate, 45 degrees off: its samples are
    // 3 dB under its true peak.
    const Signal in = sine(kRate / 4.0, 24000, kRate, 1.4f, std::numbers::pi / 4.0);
    const double ceiling = std::pow(10.0, -1.0 / 20.0);
    for (const bool truePeakMode : {false, true}) {
        Limiter limiter;
        limiter.prepare(kRate, 512);
        limiter.setParameter(Limiter::kCeiling, -1.0);
        limiter.setParameter(Limiter::kTruePeak, truePeakMode ? 1.0 : 0.0);
        limiter.reset();
        const Signal out = renderMono(limiter, in);
        const double measured = 20.0 * std::log10(truePeak(out, 12000) / ceiling);
        INFO("true peak " << truePeakMode << ": " << measured << " dB over the ceiling");
        if (truePeakMode)
            CHECK(measured < 0.2);
        else
            CHECK(measured > 2.0); // sample peak mode does not see them
    }
}

TEST_CASE("Limiter: silence, reset, no allocation, stable everywhere", "[limiter]")
{
    Limiter limiter;
    limiter.prepare(kRate, 512);
    checkSilentWhenSilent(limiter);
    checkNoAllocation(limiter);

    // reset() clears the delayed signal and the gain.
    limiter.setParameter(Limiter::kInputGain, 24.0);
    (void)renderMono(limiter, noise(4800));
    CHECK(limiter.gainReductionDb() > 10.0f);
    limiter.setParameter(Limiter::kInputGain, 0.0);
    limiter.reset();
    CHECK(limiter.gainReductionDb() == 0.0f);
    checkSilentWhenSilent(limiter, 2, 1000);
    const Signal in = noise(4800, 9, 0.5f);
    const Signal out = renderMono(limiter, in);
    CHECK(out[static_cast<std::size_t>(limiter.latencyFrames())] == in[0]);

    // Changing the look-ahead while playing stays under the ceiling.
    Audio audio{noise(48000, 2, 4.0f)};
    for (int block = 0; block < 10; ++block) {
        limiter.setParameter(Limiter::kLookahead, block % 2 == 0 ? 0.5 : 8.0);
        limiter.setParameter(Limiter::kTruePeak, block % 3 == 0 ? 1.0 : 0.0);
        Audio part{Signal(audio[0].begin() + block * 4800, audio[0].begin() + (block + 1) * 4800)};
        render(limiter, part);
        CHECK(peak(part[0]) <= static_cast<float>(std::pow(10.0, -0.3 / 20.0)));
    }

    checkStableEverywhere<Limiter>();
}
