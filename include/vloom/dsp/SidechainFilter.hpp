// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include "vloom/dsp/Biquad.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace vloom::dsp {

/// What the detector of a dynamics effect listens to: the key signal (an
/// external sidechain, or the effect's own input when no key is connected),
/// optionally through a high-pass filter (12 dB per octave) so low notes do
/// not drive it. Two channels. Real-time safe.
class SidechainFilter {
public:
    static constexpr int kMaxChannels = 2;

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        design();
        clear();
    }

    /// Switches the high-pass on or off and sets its frequency (Hz).
    void set(bool on, double frequency) noexcept
    {
        if (on && !on_)
            clear(); // no stale memory from before it was switched off
        on_ = on;
        if (frequency != frequency_) {
            frequency_ = frequency;
            design();
        }
    }

    [[nodiscard]] float process(int channel, float sample) noexcept
    {
        if (!on_)
            return sample;
        BiquadState& state = states_[static_cast<std::size_t>(std::clamp(channel, 0, kMaxChannels - 1))];
        return static_cast<float>(state.process(coefficients_, static_cast<double>(sample)));
    }

    void flushDenormals() noexcept
    {
        for (BiquadState& state : states_)
            state.flushDenormals();
    }

    void clear() noexcept
    {
        for (BiquadState& state : states_)
            state.clear();
    }

    /// The key channel that feeds detector channel `channel`: a mono key
    /// feeds both; null when there is no key.
    [[nodiscard]] static const float* keyChannel(
        const float* const* key, int numKeyChannels, int channel) noexcept
    {
        if (key == nullptr || numKeyChannels < 1)
            return nullptr;
        return key[std::min(channel, numKeyChannels - 1)];
    }

private:
    void design() noexcept { coefficients_ = biquad::highPass(frequency_, sampleRate_); }

    BiquadCoefficients coefficients_{};
    std::array<BiquadState, kMaxChannels> states_{};
    double sampleRate_ = 0.0;
    double frequency_ = 100.0;
    bool on_ = false;
};

} // namespace vloom::dsp
