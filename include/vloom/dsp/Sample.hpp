// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace vloom::dsp {

/// What a sample is, for a UI or a saved project: zeros when there is none.
struct SampleInfo {
    int numChannels = 0;
    int frames = 0;
    double sampleRate = 0.0;

    [[nodiscard]] bool empty() const noexcept { return frames == 0; }
    friend bool operator==(const SampleInfo&, const SampleInfo&) = default;
};

/// Decoded audio a processor plays, handed over by the host (ADR 0002): one
/// or two channels at any sample rate. It never changes once made, so the
/// audio thread can read it while other threads hold it.
class SampleBuffer {
public:
    static constexpr int kMaxChannels = 2;

    /// A copy of `frames` frames of `numChannels` channels recorded at
    /// `sampleRate`. NaN and infinity become 0. Not for the audio thread
    /// (it allocates). Nullptr when there is nothing to play: no channels or
    /// more than kMaxChannels (the host mixes down), no frames, a null
    /// channel, or a rate that is not positive and finite.
    [[nodiscard]] static std::shared_ptr<const SampleBuffer> copy(
        const float* const* channels, int numChannels, int frames, double sampleRate);

    [[nodiscard]] int numChannels() const noexcept { return numChannels_; }
    [[nodiscard]] int frames() const noexcept { return frames_; }
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] SampleInfo info() const noexcept { return {numChannels_, frames_, sampleRate_}; }

    /// Channel `channel` (0 .. numChannels() - 1).
    [[nodiscard]] std::span<const float> channel(int channel) const noexcept
    {
        return {data_.data() + static_cast<std::size_t>(channel) * static_cast<std::size_t>(frames_),
            static_cast<std::size_t>(frames_)};
    }

    /// Frame `frame` of `channel`, 0 outside the sample. Real-time safe.
    [[nodiscard]] float at(int channel, std::ptrdiff_t frame) const noexcept
    {
        return frame >= 0 && frame < frames_
            ? data_[static_cast<std::size_t>(channel) * static_cast<std::size_t>(frames_)
                  + static_cast<std::size_t>(frame)]
            : 0.0f;
    }

private:
    SampleBuffer() = default;

    std::vector<float> data_; ///< the channels one after the other
    int numChannels_ = 0;
    int frames_ = 0;
    double sampleRate_ = 0.0;
};

/// The value between y1 (t = 0) and y2 (t = 1), with y0 and y3 the
/// neighbours: a third-order Hermite (Catmull-Rom) curve. Real-time safe.
[[nodiscard]] inline float hermite(float y0, float y1, float y2, float y3, float t) noexcept
{
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

/// One place for a sample in a processor, shared between a host thread and
/// the audio thread without locks (ADR 0002).
///
/// Host side (not real-time; one thread at a time, never concurrently with
/// prepare() or the processor's destruction): set() publishes a buffer;
/// collectGarbage() releases the buffers replaced earlier once the audio
/// thread has let go of them. Buffers are only ever released here, never on
/// the audio thread.
///
/// Audio side (real-time, the audio thread only): update() once per block
/// picks up the newest buffer. A processor lets the voices still playing the
/// old one fade out, and calls fadeDone() when none is left.
///
/// Safe reclamation works like hazard pointers: the audio thread marks the
/// (at most two) buffers it reads, and a replaced buffer is released only
/// when neither mark points at it.
class SampleSlot {
public:
    SampleSlot() = default;
    SampleSlot(const SampleSlot&) = delete;
    SampleSlot& operator=(const SampleSlot&) = delete;

    // Host side.

    /// Plays `buffer` from the next block on (nullptr: nothing). The one it
    /// replaces is released by this or a later collectGarbage().
    void set(std::shared_ptr<const SampleBuffer> buffer);
    /// Releases the replaced buffers the audio thread no longer reads.
    void collectGarbage() noexcept;
    /// The buffer set last (nullptr: none).
    [[nodiscard]] const std::shared_ptr<const SampleBuffer>& buffer() const noexcept { return owned_; }
    [[nodiscard]] SampleInfo info() const noexcept { return owned_ ? owned_->info() : SampleInfo{}; }
    /// Replaced buffers not released yet.
    [[nodiscard]] std::size_t pendingRelease() const noexcept { return retired_.size(); }

    // Audio side.

    struct Update {
        bool changed = false;
        /// The buffer voices played until now: they should fade out.
        const SampleBuffer* fading = nullptr;
        /// An older buffer still fading: voices on it must stop now, before
        /// they read it again. Only to compare against, never to read.
        const SampleBuffer* dropped = nullptr;
    };
    /// Picks up the buffer set last. Call at the start of every block.
    [[nodiscard]] Update update() noexcept;
    /// No voice reads fading() any more.
    void fadeDone() noexcept;
    /// What new notes play (nullptr: nothing).
    [[nodiscard]] const SampleBuffer* active() const noexcept { return active_; }
    /// What notes still fading out read (nullptr: none).
    [[nodiscard]] const SampleBuffer* fading() const noexcept { return fading_; }

private:
    static_assert(std::atomic<const SampleBuffer*>::is_always_lock_free);

    // Shared between the threads.
    std::atomic<const SampleBuffer*> published_{nullptr};
    std::atomic<const SampleBuffer*> activeMark_{nullptr}; ///< the audio thread reads it
    std::atomic<const SampleBuffer*> fadingMark_{nullptr}; ///< the audio thread still reads it
    // Host side only.
    std::shared_ptr<const SampleBuffer> owned_;
    std::vector<std::shared_ptr<const SampleBuffer>> retired_;
    // Audio side only.
    const SampleBuffer* active_ = nullptr;
    const SampleBuffer* fading_ = nullptr;
};

/// A processor that plays samples the host hands over (ADR 0002), in
/// `sampleSlotCount()` numbered slots. All but sampleInfo() are host side:
/// not real-time, one thread at a time, never concurrently with prepare().
/// process() may run on the audio thread meanwhile.
///
/// - `setSample(slot, buffer)` plays `buffer` in `slot` from the next block
///   (nullptr empties it). Notes still playing the old sample fade out in a
///   few milliseconds. A bad slot is ignored.
/// - `sample(slot)` is the buffer set last, for saving or showing it.
/// - `sampleInfo(slot)`: its channels, frames and rate; zeros when empty.
/// - `collectGarbage()` releases replaced samples the audio thread has let go
///   of. setSample() also does it; a host calls it now and then (on a timer)
///   so the last replaced sample does not wait for the next setSample().
template <typename T>
concept SampleSlots
    = requires(T processor, const T constProcessor, int slot, std::shared_ptr<const SampleBuffer> buffer) {
          { constProcessor.sampleSlotCount() } noexcept -> std::same_as<int>;
          processor.setSample(slot, std::move(buffer));
          { constProcessor.sample(slot) } -> std::same_as<std::shared_ptr<const SampleBuffer>>;
          { constProcessor.sampleInfo(slot) } noexcept -> std::same_as<SampleInfo>;
          processor.collectGarbage();
      };

} // namespace vloom::dsp
