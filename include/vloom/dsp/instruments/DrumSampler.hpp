// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include "vloom/dsp/Envelope.hpp"
#include "vloom/dsp/Processor.hpp"
#include "vloom/dsp/Sample.hpp"
#include "vloom/dsp/SmoothedValue.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace vloom::dsp {

/// "vloom.drums": 16 pads, each playing its own sample (slots 0 .. 15, see
/// SampleSlots and ADR 0002) when its key comes.
///
/// - Pad n starts on key 36 + n (C1 upward, General MIDI drums from the kick)
///   and can be moved to any key; pads on one key play together.
/// - Each pad: tune (semitones and cents; the sample plays at its own speed
///   whatever the rates), start, reverse, gain, pan, velocity sensitivity, a
///   decay (at its longest, none) and, in Gate mode, a release when the key
///   goes up. In One-shot mode the key going up does nothing: the hit plays
///   until its sample or its decay ends.
/// - Choke groups 1 .. 8: a hit cuts every sound of its group, its own pad's
///   earlier hits too, within kFadeSeconds (an open hi-hat by a closed one).
/// - Up to kMaxVoices sounds at once; the oldest fades out to make room.
///
/// Parameter ids: kMasterGain, then kPadFields per pad from
/// padParameter(pad, field) = kPadBase + pad * kPadStride + field. No latency.
class DrumSampler {
public:
    static constexpr int kPads = 16;
    static constexpr int kChokeGroups = 8;
    static constexpr int kMaxVoices = 32;
    static constexpr int kFirstKey = 36;
    static constexpr double kFadeSeconds = 0.003; ///< a choked, stolen or replaced sound fades out over this
    static constexpr double kRampSeconds = 0.02; ///< gain and pan glide over this
    static constexpr double kNoDecayMs = 10000.0; ///< Decay at this (its maximum): the sound does not decay

    /// What each pad has, in this order from padParameter(pad, kKey).
    enum PadField : std::uint32_t {
        kKey,
        kTune,
        kFine,
        kDecay,
        kRelease,
        kGain,
        kPan,
        kStart,
        kReverse,
        kChoke,
        kMode,
        kVelocity,
        kPadFields
    };
    enum class Mode { OneShot, Gate };

    // Parameter ids: saved in projects, so they never change.
    static constexpr std::uint32_t kMasterGain = 0;
    static constexpr std::uint32_t kPadBase = 16;
    static constexpr std::uint32_t kPadStride = 16; ///< room for more fields later
    static constexpr std::size_t kParameterCount = 1 + kPads * kPadFields;

    [[nodiscard]] static constexpr std::uint32_t padParameter(int pad, PadField field) noexcept
    {
        return kPadBase + static_cast<std::uint32_t>(pad) * kPadStride + field;
    }

    DrumSampler() noexcept; ///< every parameter at its default, no samples
    DrumSampler(const DrumSampler&) = delete;
    DrumSampler& operator=(const DrumSampler&) = delete;

    [[nodiscard]] static ProcessorSpec spec() noexcept { return {"vloom.drums", "Drum Sampler", "1.0"}; }
    [[nodiscard]] static std::span<const ParameterSpec> parameters() noexcept;

    void prepare(double sampleRate, int maxBlockFrames);
    void setParameter(std::uint32_t id, double value) noexcept;
    void noteOn(int key, float velocity) noexcept;
    void noteOff(int key) noexcept;
    /// Adds the sound of the pads playing. Blocks longer than prepare()'s
    /// maxBlockFrames are done in parts.
    void process(float* const* channels, int numChannels, int numFrames) noexcept;
    void reset() noexcept;
    [[nodiscard]] int latencyFrames() const noexcept { return 0; }

    // SampleSlots (host side, not real-time): slot n is pad n's sample.
    [[nodiscard]] int sampleSlotCount() const noexcept { return kPads; }
    void setSample(int slot, std::shared_ptr<const SampleBuffer> buffer);
    [[nodiscard]] std::shared_ptr<const SampleBuffer> sample(int slot) const;
    [[nodiscard]] SampleInfo sampleInfo(int slot) const noexcept;
    void collectGarbage() noexcept;

    /// Sounds playing now (including ones fading out).
    [[nodiscard]] int activeVoices() const noexcept;

private:
    static constexpr int kPoolSize = kMaxVoices + 16; ///< room for sounds fading out

    struct Pad {
        std::array<double, kPadFields> values{};
        EnvelopeShape shape;
        float gainLeft = 1.0f;
        float gainRight = 1.0f;
    };
    struct Voice {
        bool active = false;
        bool held = false; ///< its key is down
        bool fading = false;
        int pad = 0;
        std::uint64_t started = 0;
        const SampleBuffer* buffer = nullptr;
        double position = 0.0;
        double direction = 1.0; ///< -1: reversed
        float velocityGain = 1.0f;
        float fade = 1.0f;
        Envelope amp;
        SmoothedValue gainLeft{1.0f};
        SmoothedValue gainRight{1.0f};
    };

    [[nodiscard]] double padValue(int pad, PadField field) const noexcept
    {
        return pads_[static_cast<std::size_t>(pad)].values[field];
    }
    void refresh() noexcept;
    void pickUpSamples() noexcept;
    [[nodiscard]] Voice* freeVoice() noexcept;
    void fadeOut(Voice& voice) noexcept;
    void start(int pad, float velocity) noexcept;
    void render(Voice& voice, float* left, float* right, int frames) noexcept;

    std::array<Pad, kPads> pads_{};
    std::array<SampleSlot, kPads> slots_{};
    std::array<Voice, kPoolSize> voices_{};
    std::vector<float> mixLeft_;
    std::vector<float> mixRight_;
    double masterGainDb_ = 0.0;
    SmoothedValue masterGain_{1.0f};
    double sampleRate_ = 0.0; ///< 0: not prepared, silent
    int maxBlockFrames_ = 0;
    float fadeStep_ = 1.0f;
    std::uint64_t started_ = 0;
    bool dirty_ = true;
};

static_assert(Instrument<DrumSampler>);
static_assert(SampleSlots<DrumSampler>);

} // namespace vloom::dsp
