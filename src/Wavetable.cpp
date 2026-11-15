// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/Wavetable.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>

namespace vloom::dsp {

namespace {

constexpr int kHarmonics = Wavetable::kSize / 2; // of the fullest level
constexpr double kPi = std::numbers::pi;

using Complex = std::complex<double>;

/// One harmonic of a cycle: `cosine` cos(k x) + `sine` sin(k x).
struct Harmonic {
    double cosine = 0.0;
    double sine = 0.0;
};
using Spectrum = std::array<Harmonic, kHarmonics + 1>; // [0] unused: no DC

/// In-place radix-2 FFT (a power of two long).
void transform(std::vector<Complex>& data)
{
    const std::size_t n = data.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) { // bit-reversed order
        std::size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }
    for (std::size_t length = 2; length <= n; length <<= 1) {
        const Complex step = std::polar(1.0, -2.0 * kPi / static_cast<double>(length));
        for (std::size_t start = 0; start < n; start += length) {
            Complex w(1.0, 0.0);
            for (std::size_t k = 0; k < length / 2; ++k) {
                const Complex a = data[start + k];
                const Complex b = data[start + k + length / 2] * w;
                data[start + k] = a + b;
                data[start + k + length / 2] = a - b;
                w *= step;
            }
        }
    }
}

double sawSine(int k) { return 2.0 / kPi * ((k % 2) != 0 ? 1.0 : -1.0) / k; }

/// Sine, triangle, saw, square: `t` moves through them, blending neighbours.
Spectrum basic(double t)
{
    static constexpr int kAnchors = 4;
    const auto anchor = [](int which, int k) {
        switch (which) {
        case 0:
            return k == 1 ? 1.0 : 0.0;
        case 1:
            return (k % 2) != 0
                ? 8.0 / (kPi * kPi) * (((k - 1) / 2) % 2 == 0 ? 1.0 : -1.0) / (static_cast<double>(k) * k)
                : 0.0;
        case 2:
            return sawSine(k);
        default:
            return (k % 2) != 0 ? 4.0 / kPi / k : 0.0;
        }
    };
    const double place = std::clamp(t, 0.0, 1.0) * (kAnchors - 1);
    const int from = std::min(static_cast<int>(place), kAnchors - 2);
    const double mix = place - from;
    Spectrum spectrum{};
    for (int k = 1; k <= kHarmonics; ++k)
        spectrum[static_cast<std::size_t>(k)].sine
            = anchor(from, k) * (1.0 - mix) + anchor(from + 1, k) * mix;
    return spectrum;
}

/// A saw through a resonant low-pass whose cutoff rises with `t`.
Spectrum sweep(double t)
{
    const double cutoff = std::pow(2.0, t * 7.0); // harmonic 1 .. 128
    Spectrum spectrum{};
    for (int k = 1; k <= kHarmonics; ++k) {
        const double ratio = k / cutoff;
        const double width = 0.15 * cutoff + 0.5;
        const double pass = 1.0 / std::sqrt(1.0 + std::pow(ratio, 8.0));
        const double peak = 1.5 * std::exp(-((k - cutoff) / width) * ((k - cutoff) / width));
        spectrum[static_cast<std::size_t>(k)].sine = sawSine(k) * (pass + peak);
    }
    return spectrum;
}

/// A saw through two vowel peaks that move with `t`.
Spectrum formant(double t)
{
    const double first = 2.0 + 6.0 * t;
    const double second = 10.0 + 22.0 * std::sin(kPi * t);
    Spectrum spectrum{};
    for (int k = 1; k <= kHarmonics; ++k) {
        const double a = std::log2(k / first);
        const double b = std::log2(k / second);
        const double gain = 0.08 + std::exp(-a * a / 0.18) + 0.7 * std::exp(-b * b / 0.1);
        spectrum[static_cast<std::size_t>(k)].sine = sawSine(k) * gain;
    }
    return spectrum;
}

/// A pulse: half the cycle high at `t` 0, a twenty-fifth of it at 1.
Spectrum pulse(double t)
{
    const double duty = 0.5 - 0.46 * std::clamp(t, 0.0, 1.0);
    Spectrum spectrum{};
    for (int k = 1; k <= kHarmonics; ++k)
        spectrum[static_cast<std::size_t>(k)].cosine = 2.0 / (k * kPi) * std::sin(k * kPi * duty);
    return spectrum;
}

Spectrum spectrumOf(Wavetable::Shape shape, double t)
{
    switch (shape) {
    case Wavetable::Shape::Basic:
        return basic(t);
    case Wavetable::Shape::Sweep:
        return sweep(t);
    case Wavetable::Shape::Formant:
        return formant(t);
    case Wavetable::Shape::Pulse:
        break;
    }
    return pulse(t);
}

} // namespace

const char* Wavetable::name(Shape shape) noexcept
{
    switch (shape) {
    case Shape::Basic:
        return "Basic Shapes";
    case Shape::Sweep:
        return "Filter Sweep";
    case Shape::Formant:
        return "Formant";
    case Shape::Pulse:
        break;
    }
    return "Pulse Width";
}

const Wavetable& Wavetable::builtin(Shape shape)
{
    // Built once each, by whichever thread asks first (the language makes
    // that safe); never changed afterwards.
    switch (shape) {
    case Shape::Basic: {
        static const Wavetable table(Shape::Basic);
        return table;
    }
    case Shape::Sweep: {
        static const Wavetable table(Shape::Sweep);
        return table;
    }
    case Shape::Formant: {
        static const Wavetable table(Shape::Formant);
        return table;
    }
    case Shape::Pulse:
        break;
    }
    static const Wavetable table(Shape::Pulse);
    return table;
}

int Wavetable::levelFor(double frequency, double sampleRate) noexcept
{
    if (!(frequency > 0.0) || !(sampleRate > 0.0))
        return kLevels - 1;
    // Harmonics that fit below half the rate; the level whose count is no more.
    const double fit = sampleRate / (2.0 * frequency);
    int level = 0;
    while (level < kLevels - 1 && static_cast<double>(kHarmonics >> level) > fit)
        ++level;
    return level;
}

Wavetable::Wavetable(Shape shape)
    : data_(static_cast<std::size_t>(kLevels) * kFrames * kStride)
{
    std::vector<Complex> bins(static_cast<std::size_t>(kSize));
    for (int frame = 0; frame < kFrames; ++frame) {
        const Spectrum spectrum = spectrumOf(shape, static_cast<double>(frame) / (kFrames - 1));
        double fullest = 1.0; // the peak of the level with every harmonic
        for (int level = 0; level < kLevels; ++level) {
            const int kept = kHarmonics >> level;
            std::fill(bins.begin(), bins.end(), Complex(0.0, 0.0));
            // x[n] = sum of cosine cos + sine sin: the conjugated spectrum, so
            // the forward transform gives the inverse one.
            for (int k = 1; k <= kept && k < kSize / 2; ++k) {
                const Harmonic& h = spectrum[static_cast<std::size_t>(k)];
                bins[static_cast<std::size_t>(k)] = Complex(h.cosine / 2.0, h.sine / 2.0);
                bins[static_cast<std::size_t>(kSize - k)] = Complex(h.cosine / 2.0, -h.sine / 2.0);
            }
            transform(bins);
            float* cycle = data_.data() + offset(level, frame);
            if (level == 0) {
                double peak = 0.0;
                for (const Complex& bin : bins)
                    peak = std::max(peak, std::abs(bin.real()));
                fullest = peak > 1.0e-9 ? peak : 1.0;
            }
            // Every level by the same factor: the frame is as loud at any pitch.
            for (int i = 0; i < kSize; ++i)
                cycle[i] = static_cast<float>(bins[static_cast<std::size_t>(i)].real() / fullest);
            cycle[kSize] = cycle[0];
        }
    }
}

} // namespace vloom::dsp
