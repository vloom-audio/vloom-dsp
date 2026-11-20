// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/effects/Tuner.hpp"

#include <algorithm>
#include <cmath>

namespace vloom::dsp {

namespace {

/// The low-pass before detection: well above kMaxHz, well below any
/// detection rate's Nyquist frequency.
constexpr double kLowPassHz = 5000.0;
/// The two sections of a 4th-order Butterworth low-pass.
constexpr std::array<double, 2> kButterworthQs{0.54119610014619701, 1.3065629648763764};
/// Lags searched beyond the range, so a pitch at either end still has a dip
/// with neighbours on both sides.
constexpr double kLagMargin = 1.06;

std::size_t nextPowerOfTwo(std::size_t n) noexcept
{
    std::size_t size = 2;
    while (size < n)
        size *= 2;
    return size;
}

} // namespace

void Tuner::prepare(double sampleRate, int /*maxBlockFrames*/)
{
    const bool usable = std::isfinite(sampleRate) && sampleRate >= 8000.0 && sampleRate <= 1.0e6;
    sampleRate_ = usable ? sampleRate : 0.0;
    if (!usable) {
        detectionRate_ = 0.0;
        history_.clear();
        frame_.clear();
        difference_.clear();
        spectrum_.clear();
        fft_.prepare(0);
        reset();
        return;
    }
    // 88.2 / 96 kHz: every second frame; 176.4 / 192 kHz: every fourth.
    decimation_ = sampleRate > 160000.0 ? 4 : sampleRate > 80000.0 ? 2 : 1;
    detectionRate_ = sampleRate / decimation_;
    for (std::size_t s = 0; s < lowPass_.size(); ++s)
        lowPass_[s]
            = biquad::lowPass(std::min(kLowPassHz, 0.2 * detectionRate_), sampleRate, kButterworthQs[s]);

    minLag_ = std::max<std::size_t>(
        2, static_cast<std::size_t>(std::floor(detectionRate_ / (kMaxHz * kLagMargin))));
    maxLag_ = static_cast<std::size_t>(std::ceil(detectionRate_ * kLagMargin / kMinHz)) + 1;
    window_ = maxLag_; // at least one period of the lowest pitch
    const std::size_t length = window_ + maxLag_ + 2; // d(lag) up to maxLag_ + 1
    history_.assign(length, 0.0f);
    frame_.assign(length, 0.0);
    difference_.assign(maxLag_ + 2, 0.0);
    const std::size_t size = nextPowerOfTwo(length);
    fft_.prepare(size);
    spectrum_.assign(size, {});
    hop_ = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(kHopSeconds * detectionRate_)));

    gain_.reset(sampleRate_, kRampSeconds, value(kMute) >= 0.5 ? 0.0f : 1.0f);
    reset();
}

void Tuner::setParameter(std::uint32_t id, double value) noexcept
{
    if (id >= kParameterCount || !std::isfinite(value))
        return;
    values_[id] = clampToSpec(kParameters[id], value);
    if (id == kMute)
        gain_.setTarget(values_[id] >= 0.5 ? 0.0f : 1.0f);
    else if (id == kReference && sampleRate_ > 0.0)
        publish(lastFrequency_, lastConfidence_); // the note against the new reference, now
}

void Tuner::reset() noexcept
{
    std::fill(history_.begin(), history_.end(), 0.0f);
    historyAt_ = 0;
    historyFilled_ = 0;
    sinceDetection_ = 0;
    decimationPhase_ = 0;
    for (BiquadState& state : lowPassState_)
        state.clear();
    levelSum_ = 0.0;
    levelFrames_ = 0;
    lastFrequency_ = 0.0;
    lastConfidence_ = 0.0;
    gain_.setCurrentAndTarget(gain_.target());
    readouts_.clear();
    if (sampleRate_ > 0.0)
        readouts_.store(kReadoutLevel, static_cast<float>(kFloorDb));
}

void Tuner::process(float* const* channels, int numChannels, int numFrames) noexcept
{
    if (!(sampleRate_ > 0.0) || channels == nullptr || numChannels < 1 || numFrames < 1)
        return;

    // The input the detector listens to.
    const auto input = static_cast<Input>(static_cast<int>(value(kInput)));
    const float* first = channels[0];
    const float* second = numChannels > 1 ? channels[1] : channels[0];
    if (input == Input::Right)
        first = second;
    else if (input == Input::Left)
        second = first;
    const std::size_t length = history_.size();

    for (int i = 0; i < numFrames; ++i) {
        double in = 0.5 * (static_cast<double>(first[i]) + static_cast<double>(second[i]));
        if (!std::isfinite(in))
            in = 0.0;
        levelSum_ += in * in;
        ++levelFrames_;
        for (std::size_t s = 0; s < lowPass_.size(); ++s)
            in = lowPassState_[s].process(lowPass_[s], in);
        if (++decimationPhase_ < decimation_)
            continue;
        decimationPhase_ = 0;
        history_[historyAt_] = static_cast<float>(in);
        historyAt_ = historyAt_ + 1 == length ? 0 : historyAt_ + 1;
        historyFilled_ = std::min(historyFilled_ + 1, length);
        ++sinceDetection_;
    }
    for (BiquadState& state : lowPassState_)
        state.flushDenormals();

    // At most one detection per block, so the cost of a block stays bounded.
    if (sinceDetection_ >= hop_) {
        sinceDetection_ = 0;
        const double meanSquare = levelFrames_ > 0 ? levelSum_ / static_cast<double>(levelFrames_) : 0.0;
        const double levelDb
            = meanSquare > 0.0 ? std::max(kFloorDb, 10.0 * std::log10(meanSquare)) : kFloorDb;
        levelSum_ = 0.0;
        levelFrames_ = 0;
        readouts_.store(kReadoutLevel, static_cast<float>(levelDb));
        if (historyFilled_ == length && levelDb >= kMinLevelDb) {
            detect();
        } else {
            lastFrequency_ = 0.0;
            lastConfidence_ = 0.0;
        }
        publish(lastFrequency_, lastConfidence_);
    }

    // Mute: silent, through untouched, or a fade between the two.
    if (!gain_.isSmoothing() && gain_.current() == 1.0f)
        return;
    if (!gain_.isSmoothing() && gain_.current() == 0.0f) {
        for (int ch = 0; ch < numChannels; ++ch)
            std::fill_n(channels[ch], numFrames, 0.0f);
        return;
    }
    for (int i = 0; i < numFrames; ++i) {
        const float gain = gain_.next();
        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] *= gain;
    }
}

void Tuner::detect() noexcept
{
    const std::size_t length = history_.size();
    const std::size_t size = spectrum_.size();
    // The window, oldest first (the history is full: historyAt_ is the oldest).
    for (std::size_t i = 0; i < length; ++i) {
        const std::size_t at = historyAt_ + i;
        frame_[i] = static_cast<double>(history_[at < length ? at : at - length]);
    }

    // r(lag) = sum over n < W of x[n] x[n + lag], for every lag at once: x in
    // the real part, its first W frames in the imaginary part, one FFT.
    for (std::size_t i = 0; i < size; ++i) {
        const double x = i < length ? frame_[i] : 0.0;
        const double y = i < window_ ? frame_[i] : 0.0;
        spectrum_[i] = {x, y};
    }
    fft_.forward(spectrum_);
    for (std::size_t k = 0; k <= size / 2; ++k) {
        const std::size_t j = (size - k) % size;
        const std::complex<double> zk = spectrum_[k];
        const std::complex<double> zj = spectrum_[j];
        const auto cross = [](std::complex<double> a, std::complex<double> b) {
            const std::complex<double> x = 0.5 * (a + std::conj(b)); // X
            const std::complex<double> y = std::complex<double>(0.0, -0.5) * (a - std::conj(b)); // Y
            return std::conj(y) * x;
        };
        spectrum_[k] = cross(zk, zj);
        spectrum_[j] = cross(zj, zk);
    }
    fft_.inverse(spectrum_);

    // YIN's difference function d(lag) = sum (x[n] - x[n + lag])^2.
    double head = 0.0; // sum over the first W frames of x^2
    for (std::size_t n = 0; n < window_; ++n)
        head += frame_[n] * frame_[n];
    double shifted = head; // the same sum from `lag` on
    for (std::size_t lag = 0; lag < difference_.size(); ++lag) {
        difference_[lag] = std::max(0.0, head + shifted - 2.0 * spectrum_[lag].real());
        shifted += frame_[lag + window_] * frame_[lag + window_] - frame_[lag] * frame_[lag];
    }

    // The cumulative mean normalized difference: the first dip under the
    // threshold, followed down to its bottom; else the lowest one.
    const auto normalized = [this](std::size_t lag, double sum) {
        return sum > 0.0 ? difference_[lag] * static_cast<double>(lag) / sum : 1.0;
    };
    double sum = 0.0;
    for (std::size_t lag = 1; lag < minLag_; ++lag)
        sum += difference_[lag];
    std::size_t best = 0;
    double bestValue = 1.0e300;
    for (std::size_t lag = minLag_; lag <= maxLag_; ++lag) {
        sum += difference_[lag];
        const double dip = normalized(lag, sum);
        if (dip < kThreshold) {
            // Down to the bottom of this dip.
            double here = dip;
            while (lag + 1 <= maxLag_) {
                const double next = normalized(lag + 1, sum + difference_[lag + 1]);
                if (next >= here)
                    break;
                ++lag;
                sum += difference_[lag];
                here = next;
            }
            best = lag;
            bestValue = here;
            break;
        }
        if (dip < bestValue) {
            best = lag;
            bestValue = dip;
        }
    }
    lastConfidence_ = std::clamp(1.0 - bestValue, 0.0, 1.0);
    if (best == 0 || bestValue >= kThreshold) {
        lastFrequency_ = 0.0;
        return;
    }

    // Between lags: a parabola through d around the dip.
    const double before = difference_[best - 1];
    const double at = difference_[best];
    const double after = difference_[best + 1];
    const double curve = before - 2.0 * at + after;
    const double shift = curve > 0.0 ? std::clamp(0.5 * (before - after) / curve, -1.0, 1.0) : 0.0;
    const double frequency = detectionRate_ / (static_cast<double>(best) + shift);
    lastFrequency_ = std::isfinite(frequency) ? frequency : 0.0;
}

void Tuner::publish(double frequency, double confidence) noexcept
{
    readouts_.store(kReadoutConfidence, static_cast<float>(confidence));
    if (!(frequency > 0.0)) {
        readouts_.store(kReadoutFrequency, 0.0f);
        readouts_.store(kReadoutNote, 0.0f);
        readouts_.store(kReadoutCents, 0.0f);
        readouts_.store(kReadoutHasPitch, 0.0f);
        return;
    }
    const double note = 69.0 + 12.0 * std::log2(frequency / value(kReference));
    readouts_.store(kReadoutFrequency, static_cast<float>(frequency));
    readouts_.store(kReadoutNote, static_cast<float>(note));
    readouts_.store(kReadoutCents, static_cast<float>(100.0 * (note - std::round(note))));
    readouts_.store(kReadoutHasPitch, 1.0f);
}

} // namespace vloom::dsp
