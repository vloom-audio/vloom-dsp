// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include "vloom/dsp/Envelope.hpp"
#include "vloom/dsp/Processor.hpp"
#include "vloom/dsp/Sample.hpp"
#include "vloom/dsp/SmoothedValue.hpp"
#include "vloom/dsp/StateVariableFilter.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace vloom::dsp {

/// "vloom.sampler": one sample played across the keyboard.
///
/// - The sample (slot 0, see SampleSlots and ADR 0002) plays at its own
///   pitch on the root key, and at its own speed whatever the rates of the
///   sample and the host. Each key away is a semitone; fine tune and
///   transpose move them all.
/// - It starts at Start, then ends, or loops between Loop Start and Loop End
///   forward or back and forth (ping-pong). A forward loop can crossfade its
///   end into the audio before its start, so the seam does not click.
/// - An amp envelope; a filter (low-pass, band-pass or high-pass, 12 dB per
///   octave) with its own envelope and key tracking.
/// - Up to kMaxVoices notes at once (the oldest fades out to make room), or
///   one at a time (mono) with legato and glide.
///
/// Start, Loop Start and Loop End are in percent of the sample's length, so
/// they stay put when a sample is replaced by another rate's version of it;
/// percentForFrame() and loopFrames() convert. Resampling is 4-point Hermite.
/// No latency.
class Sampler {
public:
    // Parameter ids: saved in projects, so they never change.
    enum Id : std::uint32_t {
        kRootKey,
        kTranspose,
        kFineTune,
        kStart,
        kKeyLow,
        kKeyHigh,
        kVelocity,
        kLoopMode,
        kLoopStart,
        kLoopEnd,
        kLoopCrossfade,
        kAttack,
        kDecay,
        kSustain,
        kRelease,
        kFilterType,
        kCutoff,
        kResonance,
        kFilterEnvAmount,
        kKeyTrack,
        kFilterAttack,
        kFilterDecay,
        kFilterSustain,
        kFilterRelease,
        kMono,
        kLegato,
        kGlide,
        kVoices,
        kGain,
        kPan,
        kParameterCount
    };

    enum class LoopMode { Off, Forward, PingPong };
    enum class FilterType { Off, LowPass, BandPass, HighPass };

    static constexpr int kMaxVoices = 32;
    static constexpr int kControlFrames = 16; ///< pitch and filter are worked out once per this many frames
    /// A full filter envelope amount (100 %) moves the cutoff this many octaves.
    static constexpr double kCutoffOctaves = 6.0;
    static constexpr double kFadeSeconds = 0.004; ///< a stolen or replaced note fades out over this
    static constexpr double kRampSeconds = 0.02; ///< gain and pan glide over this
    static constexpr int kMinLoopFrames = 4;

    Sampler() noexcept; ///< every parameter at its default, no sample
    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    [[nodiscard]] static ProcessorSpec spec() noexcept { return {"vloom.sampler", "Sampler", "1.0"}; }
    [[nodiscard]] static std::span<const ParameterSpec> parameters() noexcept;

    void prepare(double sampleRate, int maxBlockFrames);
    void setParameter(std::uint32_t id, double value) noexcept;
    void noteOn(int key, float velocity) noexcept;
    void noteOff(int key) noexcept;
    /// Adds the sound of the notes held and ringing out. Blocks longer than
    /// prepare()'s maxBlockFrames are done in parts.
    void process(float* const* channels, int numChannels, int numFrames) noexcept;
    void reset() noexcept;
    [[nodiscard]] int latencyFrames() const noexcept { return 0; }

    // SampleSlots (host side, not real-time): slot 0 is the sample.
    [[nodiscard]] int sampleSlotCount() const noexcept { return 1; }
    void setSample(int slot, std::shared_ptr<const SampleBuffer> buffer);
    [[nodiscard]] std::shared_ptr<const SampleBuffer> sample(int slot) const;
    [[nodiscard]] SampleInfo sampleInfo(int slot) const noexcept;
    void collectGarbage() noexcept { slot_.collectGarbage(); }

    /// Where the loop is, in frames of a sample, as it is played.
    struct LoopFrames {
        int start = 0;
        int end = 0; ///< one past the last frame of the loop
        int crossfade = 0; ///< forward loops only

        friend bool operator==(const LoopFrames&, const LoopFrames&) = default;
    };
    /// The loop played for Loop Start, Loop End (in %) and Loop Crossfade (in
    /// ms) on a sample `info`: within the sample, at least kMinLoopFrames
    /// long (an end before the start gives the shortest loop from the
    /// start), the crossfade no longer than half the loop or the audio before
    /// it. All 0 without a sample. Real-time safe.
    [[nodiscard]] static LoopFrames loopFrames(
        const SampleInfo& info, double loopStart, double loopEnd, double crossfadeMs) noexcept;
    /// The value for Start, Loop Start or Loop End that puts it on `frame` of
    /// a sample `frames` long (for a UI that snaps to frames).
    [[nodiscard]] static double percentForFrame(int frame, int frames) noexcept;

    /// Voices sounding now (held, ringing out or fading).
    [[nodiscard]] int activeVoices() const noexcept;

private:
    static constexpr int kPoolSize = kMaxVoices + 8; ///< room for voices fading out
    struct Loop {
        LoopMode mode = LoopMode::Off;
        double start = 0.0;
        double end = 0.0;
        double crossfade = 0.0;
    };
    struct Voice {
        bool active = false;
        bool held = false; ///< its key is down
        bool fading = false; ///< on its way out (stolen, cut or its sample replaced)
        int key = 60;
        float velocity = 1.0f;
        float velocityGain = 1.0f;
        float pitch = 60.0f; ///< where the glide is, in keys
        std::uint64_t started = 0; ///< order of notes: the oldest is stolen first
        const SampleBuffer* buffer = nullptr;
        double position = 0.0; ///< in frames of the sample
        int direction = 1; ///< -1: on the way back in a ping-pong loop
        double step = 0.0; ///< frames of the sample per output frame, until the next control step
        float fade = 1.0f;
        Envelope amp;
        Envelope filterEnv;
        std::array<SvfState, 2> filter{};
        SvfCoefficients coefficients;
    };

    [[nodiscard]] double value(std::uint32_t id) const noexcept { return values_[id]; }
    [[nodiscard]] bool on(std::uint32_t id) const noexcept { return values_[id] >= 0.5; }
    void refresh() noexcept;
    [[nodiscard]] Loop loopFor(const SampleBuffer& buffer) const noexcept;
    [[nodiscard]] int allowedVoices() const noexcept;
    [[nodiscard]] Voice* freeVoice() noexcept;
    void fadeOut(Voice& voice) noexcept;
    void start(Voice& voice, int key, float velocity) noexcept;
    [[nodiscard]] Voice* monoVoice() noexcept;
    void control(Voice& voice) noexcept;
    void render(Voice& voice, float* left, float* right, int frames) noexcept;
    void renderBlock(int frames) noexcept;
    void pickUpSample() noexcept;

    std::array<double, kParameterCount> values_{};
    std::array<Voice, kPoolSize> voices_{};
    std::array<int, kMaxVoices> monoKeys_{}; ///< keys held in mono mode, the newest last
    int monoCount_ = 0;
    SampleSlot slot_;
    Loop loop_; ///< for the active sample
    EnvelopeShape ampShape_;
    EnvelopeShape filterShape_;
    std::vector<float> mixLeft_;
    std::vector<float> mixRight_;
    SmoothedValue gainLeft_{1.0f};
    SmoothedValue gainRight_{1.0f};
    double sampleRate_ = 0.0; ///< 0: not prepared, silent
    int maxBlockFrames_ = 0;
    float fadeStep_ = 1.0f;
    std::uint64_t started_ = 0;
    float lastPitch_ = 60.0f; ///< where a new note's glide starts
    bool anyNoteYet_ = false;
    bool dirty_ = true; ///< a value changed since refresh()
};

static_assert(Instrument<Sampler>);
static_assert(SampleSlots<Sampler>);

} // namespace vloom::dsp
