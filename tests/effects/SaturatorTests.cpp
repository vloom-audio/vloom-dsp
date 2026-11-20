// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "support/EffectTesting.hpp"
#include "vloom/dsp/Oversampler.hpp"
#include "vloom/dsp/effects/Saturator.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>
#include <string_view>

using Catch::Approx;
using vloom::dsp::Oversampler;
using vloom::dsp::Saturator;
using namespace vloom::test;

namespace {

constexpr double kRate = 48000.0;

Saturator prepared(double drive, Saturator::Curve curve = Saturator::Curve::Soft)
{
    Saturator saturator;
    saturator.prepare(kRate, 512);
    saturator.setParameter(Saturator::kDrive, drive);
    saturator.setParameter(Saturator::kCurve, static_cast<double>(curve));
    saturator.reset();
    return saturator;
}

struct Spectrum {
    double fundamental = 0.0; ///< power
    double harmonics = 0.0; ///< power of the harmonics 2, 3, ... below Nyquist (and DC)
    double rest = 0.0; ///< everything else: aliasing
};

/// Splits one second of `signal` (from `from`) holding a sine at a whole
/// number of Hz, `frequency`, into its fundamental, its harmonics and the rest.
Spectrum spectrum(const Signal& signal, std::size_t from, double frequency)
{
    const auto count = static_cast<std::size_t>(kRate);
    Spectrum s;
    double total = 0.0;
    for (std::size_t i = from; i < from + count; ++i)
        total += static_cast<double>(signal[i]) * static_cast<double>(signal[i]);
    total /= static_cast<double>(count);
    const double dc = amplitudeAt(signal, 0.0, kRate, from, count);
    s.harmonics = dc * dc;
    for (int k = 1; k * frequency < kRate / 2.0; ++k) {
        const double a = amplitudeAt(signal, k * frequency, kRate, from, count);
        (k == 1 ? s.fundamental : s.harmonics) += a * a / 2.0;
    }
    s.rest = std::max(total - s.fundamental - s.harmonics, 1.0e-30);
    return s;
}

/// The power of the harmonics of `frequency` (whole Hz) that fold back
/// below `limit` (Hz), in one second of `signal` from `from`: aliasing that
/// can be heard. (Folding from just above Nyquist into the top few kHz is
/// left out: a half-band oversampler lets some of it through by design.)
double audibleAliasing(const Signal& signal, std::size_t from, double frequency, double limit)
{
    const auto rate = static_cast<long>(kRate);
    const auto f0 = static_cast<long>(frequency);
    std::set<long> folded;
    for (long n = 2; n < 400; ++n) {
        long f = (n * f0) % rate;
        if (f > rate / 2)
            f = rate - f;
        if (n * f0 > rate / 2 && f % f0 != 0 && static_cast<double>(f) < limit)
            folded.insert(f);
    }
    double power = 0.0;
    for (const long f : folded) {
        const double a
            = amplitudeAt(signal, static_cast<double>(f), kRate, from, static_cast<std::size_t>(rate));
        power += a * a / 2.0;
    }
    return std::max(power, 1.0e-30);
}

double db(double power) { return 10.0 * std::log10(power); }

} // namespace

TEST_CASE("Saturator describes itself and its parameters", "[saturator]")
{
    CHECK(std::string_view(Saturator::spec().id) == "vloom.saturator");
    checkParameterList<Saturator>();
}

TEST_CASE("Saturator's curves: slope 1 at zero, bounded, Tube lopsided", "[saturator]")
{
    using Curve = Saturator::Curve;
    for (const Curve curve : {Curve::Soft, Curve::Tube, Curve::Tape, Curve::Hard}) {
        INFO("curve " << static_cast<int>(curve));
        CHECK(Saturator::shape(curve, 0.0f) == 0.0f);
        CHECK(Saturator::shape(curve, 1.0e-3f) == Approx(1.0e-3f).epsilon(1e-3));
        CHECK(std::abs(Saturator::shape(curve, 100.0f)) <= 1.0f);
        float last = Saturator::shape(curve, -10.0f);
        for (float x = -10.0f; x <= 10.0f; x += 0.01f) {
            const float y = Saturator::shape(curve, x);
            CHECK(y >= last); // never falls
            last = y;
        }
    }
    CHECK(Saturator::shape(Curve::Soft, -0.7f) == -Saturator::shape(Curve::Soft, 0.7f));
    CHECK(Saturator::shape(Curve::Tube, -0.7f) != -Saturator::shape(Curve::Tube, 0.7f));
    CHECK(Saturator::shape(Curve::Hard, 1.5f) == 1.0f);
}

TEST_CASE("Saturator's latency is the oversampler's, and matches the measured delay", "[saturator]")
{
    for (const double oversampling : {0.0, 1.0}) {
        Saturator saturator = prepared(0.0);
        saturator.setParameter(Saturator::kOversampling, oversampling);
        const int expected = oversampling > 0.5 ? Oversampler::kLatency4x : Oversampler::kLatency2x;
        CHECK(saturator.latencyFrames() == expected);
        // A small impulse (the curves are linear there) comes out at the latency.
        Signal out = renderMono(saturator, impulse(512, 20, 0.001f));
        std::size_t at = 0;
        for (std::size_t i = 0; i < out.size(); ++i)
            if (std::abs(out[i]) > std::abs(out[at]))
                at = i;
        CHECK(at == static_cast<std::size_t>(expected + 20));
        // And Mix 0 % is the dry signal delayed by exactly that much.
        saturator.setParameter(Saturator::kMix, 0.0);
        saturator.setParameter(Saturator::kDrive, 30.0);
        saturator.reset();
        const Signal in = noise(4096);
        out = renderMono(saturator, in);
        for (std::size_t i = 0; i + static_cast<std::size_t>(expected) < out.size(); ++i)
            REQUIRE(out[i + static_cast<std::size_t>(expected)] == in[i]);
    }
}

TEST_CASE("Saturator: quiet signals pass nearly unchanged at 0 dB drive", "[saturator]")
{
    Saturator saturator = prepared(0.0);
    const Signal in = sine(1000.0, 48000, kRate, 0.01f);
    const Signal out = renderMono(saturator, in);
    const auto delay = static_cast<std::size_t>(saturator.latencyFrames());
    for (std::size_t i = 24000; i < out.size(); ++i)
        REQUIRE(out[i] == Approx(in[i - delay]).margin(1e-4)); // 1 %: the DC blocker's phase
}

TEST_CASE("Saturator's distortion rises with drive", "[saturator]")
{
    for (const auto curve :
        {Saturator::Curve::Soft, Saturator::Curve::Tube, Saturator::Curve::Tape, Saturator::Curve::Hard}) {
        double last = -1000.0;
        for (const double drive : {0.0, 6.0, 12.0, 24.0}) {
            Saturator saturator = prepared(drive, curve);
            const Signal out = renderMono(saturator, sine(500.0, 96000, kRate, 0.5f));
            const Spectrum s = spectrum(out, 48000, 500.0);
            const double thd = db(s.harmonics / s.fundamental);
            INFO("curve " << static_cast<int>(curve) << ", drive " << drive << ": THD " << thd << " dB");
            CHECK(thd > last);
            last = thd;
        }
        CHECK(last > -20.0); // clearly distorted at 24 dB
    }
}

TEST_CASE("Saturator's oversampling keeps aliasing far below a naive curve's", "[saturator]")
{
    // 2903 Hz: its harmonics above Nyquist fold back between the harmonics.
    constexpr double frequency = 2903.0;
    constexpr double limit = 18000.0;
    const Signal in = sine(frequency, 96000, kRate, 0.5f);
    for (const auto curve : {Saturator::Curve::Soft, Saturator::Curve::Hard}) {
        const auto drive = static_cast<float>(std::pow(10.0, 18.0 / 20.0));
        Signal naive = in;
        for (float& sample : naive)
            sample = Saturator::shape(curve, sample * drive);
        const double naiveAliasing = db(
            audibleAliasing(naive, 48000, frequency, limit) / spectrum(naive, 48000, frequency).fundamental);

        for (const double oversampling : {0.0, 1.0}) {
            Saturator saturator = prepared(18.0, curve);
            saturator.setParameter(Saturator::kOversampling, oversampling);
            const Signal out = renderMono(saturator, in);
            const double aliasing = db(
                audibleAliasing(out, 48000, frequency, limit) / spectrum(out, 48000, frequency).fundamental);
            INFO("curve " << static_cast<int>(curve) << ", " << (oversampling > 0.5 ? 4 : 2) << "x: aliasing "
                          << aliasing << " dB, naive " << naiveAliasing << " dB");
            // Hard clipping has harmonics too strong for any oversampling to
            // remove; a soft curve's die away fast enough.
            CHECK(aliasing < naiveAliasing - (oversampling > 0.5 ? 20.0 : 8.0));
            if (curve == Saturator::Curve::Soft)
                CHECK(aliasing < (oversampling > 0.5 ? -110.0 : -80.0));
        }
    }
}

TEST_CASE("Saturator's tone tilts the spectrum", "[saturator]")
{
    const auto levelAt = [](double tone, double frequency) {
        Saturator saturator = prepared(0.0);
        saturator.setParameter(Saturator::kTone, tone);
        saturator.reset();
        const Signal in = sine(frequency, 48000, kRate, 0.01f);
        const Signal out = renderMono(saturator, in);
        return rmsDb(out, 24000) - rmsDb(in, 24000);
    };
    CHECK(levelAt(100.0, 10000.0) == Approx(6.0).margin(0.5));
    CHECK(levelAt(100.0, 50.0) == Approx(-6.0).margin(0.5));
    CHECK(levelAt(-100.0, 10000.0) == Approx(-6.0).margin(0.5));
    CHECK(levelAt(0.0, 10000.0) == Approx(0.0).margin(0.05));
}

TEST_CASE("Saturator: silence, reset, no allocation, stable everywhere", "[saturator]")
{
    Saturator saturator = prepared(24.0, Saturator::Curve::Tube);
    checkSilentWhenSilent(saturator);
    checkNoAllocation(saturator);
    checkNoAllocation(saturator, 1);
    saturator.reset();
    checkSilentWhenSilent(saturator, 2, 1000);
    checkStableEverywhere<Saturator>();
}
