// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/Sample.hpp"

#include <cmath>
#include <utility>

namespace vloom::dsp {

std::shared_ptr<const SampleBuffer> SampleBuffer::copy(
    const float* const* channels, int numChannels, int frames, double sampleRate)
{
    if (channels == nullptr || numChannels < 1 || numChannels > kMaxChannels || frames < 1
        || !std::isfinite(sampleRate) || !(sampleRate > 0.0))
        return nullptr;
    for (int c = 0; c < numChannels; ++c)
        if (channels[c] == nullptr)
            return nullptr;
    std::shared_ptr<SampleBuffer> buffer(new SampleBuffer);
    buffer->numChannels_ = numChannels;
    buffer->frames_ = frames;
    buffer->sampleRate_ = sampleRate;
    buffer->data_.resize(static_cast<std::size_t>(numChannels) * static_cast<std::size_t>(frames));
    float* into = buffer->data_.data();
    for (int c = 0; c < numChannels; ++c)
        for (int i = 0; i < frames; ++i)
            *into++ = std::isfinite(channels[c][i]) ? channels[c][i] : 0.0f;
    return buffer;
}

void SampleSlot::set(std::shared_ptr<const SampleBuffer> buffer)
{
    if (buffer == owned_)
        return;
    retired_.reserve(retired_.size() + 1); // the only thing that can throw, before anything changes
    published_.store(buffer.get());
    if (owned_)
        retired_.push_back(std::move(owned_));
    owned_ = std::move(buffer);
    collectGarbage();
}

void SampleSlot::collectGarbage() noexcept
{
    // The active mark first: the audio thread sets the fading mark before it
    // moves the active one, so a buffer passing from one to the other is
    // seen in at least one of them (both are sequentially consistent).
    const SampleBuffer* const active = activeMark_.load();
    const SampleBuffer* const fading = fadingMark_.load();
    std::erase_if(retired_, [active, fading](const std::shared_ptr<const SampleBuffer>& retired) {
        return retired.get() != active && retired.get() != fading;
    });
}

SampleSlot::Update SampleSlot::update() noexcept
{
    const SampleBuffer* next = published_.load();
    if (next == active_)
        return {};
    Update update;
    update.changed = true;
    update.dropped = fading_;
    fading_ = active_;
    fadingMark_.store(fading_); // before the active mark lets go of it
    // Mark the new buffer, then check it is still the one published: if the
    // host replaced it in between, its collectGarbage() may not have seen the
    // mark, so take the newer one instead.
    for (;;) {
        activeMark_.store(next);
        const SampleBuffer* const again = published_.load();
        if (again == next)
            break;
        next = again;
    }
    active_ = next;
    update.fading = fading_;
    return update;
}

void SampleSlot::fadeDone() noexcept
{
    fading_ = nullptr;
    fadingMark_.store(nullptr);
}

} // namespace vloom::dsp
