// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/effects/Utility.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vloom::dsp {

namespace {

// What each glide is when it changes nothing.
constexpr std::array<float, 8> kIdentity{0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

} // namespace

std::array<float, Utility::kGlides> Utility::targets() const noexcept
{
    std::array<float, kGlides> t{};
    t[kDcMix] = value(kDcBlock) >= 0.5 ? 1.0f : 0.0f;
    t[kSwapMix] = value(kSwap) >= 0.5 ? 1.0f : 0.0f;
    t[kSignLeft] = value(kInvertLeft) >= 0.5 ? -1.0f : 1.0f;
    t[kSignRight] = value(kInvertRight) >= 0.5 ? -1.0f : 1.0f;
    t[kWidthGlide] = value(kMono) >= 0.5 ? 0.0f : static_cast<float>(value(kWidth) / 100.0);
    t[kGainGlide] = value(kGain) > kMinGainDb ? static_cast<float>(decibelsToGain(value(kGain))) : 0.0f;
    // Balance: the side the pan moves away from is turned down (constant
    // power); the other side stays at full level.
    const double pan = value(kPan) / 100.0;
    const double away = std::cos(std::abs(pan) * std::numbers::pi / 2.0);
    const double far = std::abs(pan) >= 1.0 ? 0.0 : away;
    t[kPanLeft] = static_cast<float>(pan > 0.0 ? far : 1.0);
    t[kPanRight] = static_cast<float>(pan < 0.0 ? far : 1.0);
    return t;
}

void Utility::prepare(double sampleRate, int /*maxBlockFrames*/)
{
    sampleRate_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 0.0;
    dcFactor_ = std::exp(-2.0 * std::numbers::pi * kDcBlockHz / std::max(sampleRate_, 1.0));
    for (std::size_t g = 0; g < kGlides; ++g)
        glides_[g].reset(sampleRate_, kRampSeconds, kIdentity[g]);
    reset();
}

void Utility::setParameter(std::uint32_t id, double value) noexcept
{
    if (id >= kParameterCount || !std::isfinite(value))
        return;
    values_[id] = clampToSpec(kParameters[id], value);
    applyValues(false);
}

void Utility::applyValues(bool jump) noexcept
{
    const std::array<float, kGlides> t = targets();
    for (std::size_t g = 0; g < kGlides; ++g) {
        if (jump)
            glides_[g].setCurrentAndTarget(t[g]);
        else
            glides_[g].setTarget(t[g]);
    }
}

void Utility::reset() noexcept
{
    applyValues(true);
    dcIn_ = {};
    dcOut_ = {};
}

void Utility::process(float* const* channels, int numChannels, int numFrames) noexcept
{
    if (!(sampleRate_ > 0.0) || channels == nullptr || numChannels < 1)
        return;
    // A glide that rests where it changes nothing is skipped, so the
    // defaults pass the input through bit for bit.
    std::array<bool, kGlides> active{};
    bool any = false;
    for (std::size_t g = 0; g < kGlides; ++g) {
        active[g] = glides_[g].isSmoothing() || glides_[g].current() != kIdentity[g];
        any = any || active[g];
    }
    if (!any)
        return;
    const bool stereo = numChannels >= 2;
    float* const left = channels[0];
    float* const right = stereo ? channels[1] : nullptr;

    for (int i = 0; i < numFrames; ++i) {
        std::array<float, kGlides> v{};
        for (std::size_t g = 0; g < kGlides; ++g)
            v[g] = glides_[g].next();
        double l = static_cast<double>(left[i]);
        double r = stereo ? static_cast<double>(right[i]) : 0.0;

        if (active[kDcMix]) {
            const double blockedL = l - dcIn_[0] + dcFactor_ * dcOut_[0];
            dcIn_[0] = l;
            dcOut_[0] = blockedL;
            l += static_cast<double>(v[kDcMix]) * (blockedL - l);
            if (stereo) {
                const double blockedR = r - dcIn_[1] + dcFactor_ * dcOut_[1];
                dcIn_[1] = r;
                dcOut_[1] = blockedR;
                r += static_cast<double>(v[kDcMix]) * (blockedR - r);
            }
        }
        if (stereo && active[kSwapMix]) {
            const double s = static_cast<double>(v[kSwapMix]);
            const double swappedL = l + s * (r - l);
            r += s * (l - r);
            l = swappedL;
        }
        if (active[kSignLeft])
            l *= static_cast<double>(v[kSignLeft]);
        if (stereo && active[kSignRight])
            r *= static_cast<double>(v[kSignRight]);
        if (stereo && active[kWidthGlide]) {
            const double mid = 0.5 * (l + r);
            const double side = 0.5 * (l - r) * static_cast<double>(v[kWidthGlide]);
            l = mid + side;
            r = mid - side;
        }
        if (active[kGainGlide]) {
            l *= static_cast<double>(v[kGainGlide]);
            r *= static_cast<double>(v[kGainGlide]);
        }
        if (stereo && active[kPanLeft])
            l *= static_cast<double>(v[kPanLeft]);
        if (stereo && active[kPanRight])
            r *= static_cast<double>(v[kPanRight]);

        left[i] = static_cast<float>(l);
        if (stereo)
            right[i] = static_cast<float>(r);
    }
    for (double& state : dcOut_)
        if (std::abs(state) < 1.0e-20)
            state = 0.0;
}

} // namespace vloom::dsp
