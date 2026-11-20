// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "support/EffectTesting.hpp"
#include "vloom/dsp/effects/Tuner.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string_view>

using Catch::Approx;
using vloom::dsp::Tuner;
using namespace vloom::test;

namespace {

Tuner prepared(double rate, bool mute = false)
{
    Tuner tuner;
    tuner.setParameter(Tuner::kMute, mute ? 1.0 : 0.0); // before prepare(): no fade
    tuner.prepare(rate, 512);
    return tuner;
}

/// Cents between `measured` and `expected` Hz.
double centsOff(double measured, double expected) { return 1200.0 * std::log2(measured / expected); }

float readout(const Tuner& tuner, Tuner::Readout index) { return tuner.readout(index); }

} // namespace

TEST_CASE("Tuner describes itself, its parameters and its readouts", "[tuner]")
{
    CHECK(std::string_view(Tuner::spec().id) == "vloom.tuner");
    checkParameterList<Tuner>();
    CHECK(Tuner().latencyFrames() == 0);
    STATIC_CHECK(vloom::dsp::HasReadouts<Tuner>);
    const auto readouts = Tuner::readoutSpecs();
    REQUIRE(readouts.size() == Tuner::kReadoutCount);
    CHECK(vloom::dsp::findReadout(readouts, "frequency_hz") == Tuner::kReadoutFrequency);
    CHECK(vloom::dsp::findReadout(readouts, "note") == Tuner::kReadoutNote);
    CHECK(vloom::dsp::findReadout(readouts, "cents") == Tuner::kReadoutCents);
    CHECK(vloom::dsp::findReadout(readouts, "has_pitch") == Tuner::kReadoutHasPitch);
    CHECK(vloom::dsp::findReadout(readouts, "confidence") == Tuner::kReadoutConfidence);
    CHECK(vloom::dsp::findReadout(readouts, "level_db") == Tuner::kReadoutLevel);
    CHECK(vloom::dsp::findReadout(readouts, "nope") == -1);

    const ParameterSpec* reference = findParameter(Tuner::parameters(), Tuner::kReference);
    REQUIRE(reference != nullptr);
    CHECK(reference->defaultValue == 440.0);
    CHECK(reference->minValue == 415.0);
    CHECK(reference->maxValue == 466.0);
    CHECK(findParameter(Tuner::parameters(), Tuner::kMute)->defaultValue == 1.0);
    CHECK(Tuner().readout(Tuner::kReadoutCount) == 0.0f);
}

TEST_CASE("Tuner finds sines to within a cent at every sample rate", "[tuner]")
{
    for (const double rate : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0}) {
        for (const double hz :
            {30.0, 41.2, 55.0, 82.41, 110.5, 146.83, 261.63, 440.0, 659.26, 1046.5, 1500.0, 2000.0}) {
            Tuner tuner = prepared(rate);
            (void)renderMono(tuner, sine(hz, static_cast<int>(rate * 0.4), rate, 0.5f), 256);
            INFO("rate " << rate << ", " << hz << " Hz");
            REQUIRE(readout(tuner, Tuner::kReadoutHasPitch) == 1.0f);
            const double found = readout(tuner, Tuner::kReadoutFrequency);
            CHECK(std::abs(centsOff(found, hz)) < 1.0);
            const double note = 69.0 + 12.0 * std::log2(hz / 440.0);
            CHECK(readout(tuner, Tuner::kReadoutNote) == Approx(note).margin(0.01));
            CHECK(readout(tuner, Tuner::kReadoutCents)
                == Approx(100.0 * (note - std::round(note))).margin(1.0));
            CHECK(readout(tuner, Tuner::kReadoutConfidence) > 0.9f);
        }
    }
}

TEST_CASE("Tuner follows a harmonic-rich tone and the chosen input", "[tuner]")
{
    constexpr double rate = 48000.0;
    // A sawtooth-like tone at 110 Hz (eight harmonics) on the left, a 330 Hz sine on the right.
    const int frames = 24000;
    Signal left(static_cast<std::size_t>(frames), 0.0f);
    for (int h = 1; h <= 8; ++h) {
        const Signal partial = sine(110.0 * h, frames, rate, 0.3f / static_cast<float>(h));
        for (std::size_t i = 0; i < left.size(); ++i)
            left[i] += partial[i];
    }
    const Signal right = sine(330.0, frames, rate, 0.4f);

    Tuner tuner = prepared(rate);
    tuner.setParameter(Tuner::kInput, static_cast<double>(Tuner::Input::Left));
    Audio audio{left, right};
    render(tuner, audio);
    CHECK(std::abs(centsOff(readout(tuner, Tuner::kReadoutFrequency), 110.0)) < 1.0);

    tuner.setParameter(Tuner::kInput, static_cast<double>(Tuner::Input::Right));
    audio = {left, right};
    render(tuner, audio);
    CHECK(std::abs(centsOff(readout(tuner, Tuner::kReadoutFrequency), 330.0)) < 1.0);
}

TEST_CASE("Tuner: silence and noise have no pitch", "[tuner]")
{
    for (const double rate : {44100.0, 96000.0, 192000.0}) {
        Tuner tuner = prepared(rate);
        (void)renderMono(tuner, Signal(static_cast<std::size_t>(rate / 2), 0.0f));
        CHECK(readout(tuner, Tuner::kReadoutHasPitch) == 0.0f);
        CHECK(readout(tuner, Tuner::kReadoutFrequency) == 0.0f);
        CHECK(readout(tuner, Tuner::kReadoutLevel) == Approx(Tuner::kFloorDb));

        for (const std::uint32_t seed : {1u, 2u, 3u}) {
            (void)renderMono(tuner, noise(static_cast<int>(rate / 2), seed, 0.5f));
            INFO("rate " << rate << ", seed " << seed);
            CHECK(readout(tuner, Tuner::kReadoutHasPitch) == 0.0f);
            CHECK(readout(tuner, Tuner::kReadoutFrequency) == 0.0f);
            CHECK(readout(tuner, Tuner::kReadoutLevel) > -12.0f);
        }

        // A tone too quiet to trust.
        (void)renderMono(tuner, sine(440.0, static_cast<int>(rate / 2), rate, 0.0005f));
        CHECK(readout(tuner, Tuner::kReadoutHasPitch) == 0.0f);
    }
}

TEST_CASE("Tuner's reference moves the note and the cents", "[tuner]")
{
    constexpr double rate = 48000.0;
    Tuner tuner = prepared(rate);
    (void)renderMono(tuner, sine(440.0, 19200, rate, 0.5f));
    CHECK(readout(tuner, Tuner::kReadoutCents) == Approx(0.0).margin(1.0));
    CHECK(readout(tuner, Tuner::kReadoutNote) == Approx(69.0).margin(0.01));

    // A4 = 442 Hz: 440 Hz is 7.85 cents flat, at once.
    tuner.setParameter(Tuner::kReference, 442.0);
    const double flat = 1200.0 * std::log2(440.0 / 442.0);
    CHECK(readout(tuner, Tuner::kReadoutCents) == Approx(flat).margin(1.0));
    (void)renderMono(tuner, sine(440.0, 19200, rate, 0.5f));
    CHECK(readout(tuner, Tuner::kReadoutCents) == Approx(flat).margin(1.0));
    CHECK(readout(tuner, Tuner::kReadoutFrequency) == Approx(440.0).epsilon(0.0006));

    // A4 = 432 Hz: 440 Hz is 31.8 cents sharp.
    tuner.setParameter(Tuner::kReference, 432.0);
    (void)renderMono(tuner, sine(440.0, 19200, rate, 0.5f));
    CHECK(readout(tuner, Tuner::kReadoutCents) == Approx(1200.0 * std::log2(440.0 / 432.0)).margin(1.0));

    // Out of range: clamped to 415.
    tuner.setParameter(Tuner::kReference, 300.0);
    CHECK(readout(tuner, Tuner::kReadoutNote) == Approx(69.0 + 12.0 * std::log2(440.0 / 415.0)).margin(0.01));
}

TEST_CASE("Tuner passes audio through, or mutes it", "[tuner]")
{
    constexpr double rate = 48000.0;
    const Signal in = noise(9600, 5, 0.5f);

    Tuner through = prepared(rate, false);
    CHECK(renderMono(through, in) == in);

    Tuner muted;
    muted.prepare(rate, 512); // muted by default
    CHECK(peak(renderMono(muted, in)) == 0.0f);
    (void)renderMono(muted, sine(220.0, 19200, rate, 0.5f));
    CHECK(std::abs(centsOff(readout(muted, Tuner::kReadoutFrequency), 220.0)) < 1.0); // still listening

    // Unmuting fades in over kRampSeconds, then passes through.
    muted.setParameter(Tuner::kMute, 0.0);
    const Signal faded = renderMono(muted, constant(9600, 0.5f));
    CHECK(faded.front() < 0.01f);
    CHECK(faded.front() > 0.0f);
    CHECK(faded[1000] == 0.5f);
    CHECK(faded.back() == 0.5f);

    Tuner unprepared;
    CHECK(renderMono(unprepared, in) == in);
}

TEST_CASE("Tuner: NaN and infinity in, finite readouts out, and it recovers", "[tuner]")
{
    constexpr double rate = 48000.0;
    Tuner tuner = prepared(rate);
    Signal bad = sine(440.0, 9600, rate, 0.5f);
    for (std::size_t i = 0; i < bad.size(); i += 7)
        bad[i]
            = (i % 2 == 0) ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
    (void)renderMono(tuner, bad);
    for (std::size_t r = 0; r < Tuner::kReadoutCount; ++r)
        CHECK(std::isfinite(tuner.readout(r)));

    (void)renderMono(tuner, sine(440.0, 19200, rate, 0.5f));
    CHECK(readout(tuner, Tuner::kReadoutHasPitch) == 1.0f);
    CHECK(std::abs(centsOff(readout(tuner, Tuner::kReadoutFrequency), 440.0)) < 1.0);

    Tuner muted;
    muted.prepare(rate, 512);
    CHECK(peak(renderMono(muted, bad)) == 0.0f); // muted: zeros, never NaN
}

TEST_CASE("Tuner: reset, no allocation, any block size, stable everywhere", "[tuner]")
{
    constexpr double rate = 48000.0;
    Tuner tuner = prepared(rate);
    checkNoAllocation(tuner);
    checkNoAllocation(tuner, 1);

    (void)renderMono(tuner, sine(440.0, 19200, rate, 0.5f));
    CHECK(readout(tuner, Tuner::kReadoutHasPitch) == 1.0f);
    tuner.reset();
    CHECK(readout(tuner, Tuner::kReadoutHasPitch) == 0.0f);
    CHECK(readout(tuner, Tuner::kReadoutFrequency) == 0.0f);
    CHECK(readout(tuner, Tuner::kReadoutLevel) == Approx(Tuner::kFloorDb));

    // Tiny and huge blocks give the same pitch.
    for (const int block : {1, 17, 4096}) {
        Tuner blocked = prepared(rate);
        (void)renderMono(blocked, sine(196.0, 24000, rate, 0.5f), block);
        INFO("block " << block);
        CHECK(std::abs(centsOff(readout(blocked, Tuner::kReadoutFrequency), 196.0)) < 1.0);
    }

    checkStableEverywhere<Tuner>();
}
