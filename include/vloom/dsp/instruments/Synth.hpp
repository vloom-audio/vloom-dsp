// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include "vloom/dsp/Envelope.hpp"
#include "vloom/dsp/Processor.hpp"
#include "vloom/dsp/SmoothedValue.hpp"
#include "vloom/dsp/StateVariableFilter.hpp"
#include "vloom/dsp/Wavetable.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vloom::dsp {

/// "vloom.synth": a wavetable synthesizer.
///
/// - Two wavetable oscillators (A, B), each scanning one of the built-in
///   tables (Wavetable), with up to kMaxUnison detuned copies spread across
///   the stereo field; a sine sub oscillator; noise.
/// - A filter (low-pass, band-pass or high-pass, 12 dB per octave) with
///   drive; each source can go through it or around it.
/// - Envelope 1 shapes the level. Envelopes 2 and 3, four LFOs (each can
///   wait, fade in and be smoothed) and four macro knobs each move one
///   thing: the cutoff, a table position, the pitch or the level.
/// - Up to kMaxVoices notes at once (the oldest gives way), or one at a time
///   (mono) with legato and glide.
///
/// The oscillators read band-limited tables, so the tables do not alias;
/// warp and drive add harmonics of their own that can. No latency.
class Synth {
public:
    // Parameter ids: saved in projects, so they never change.
    enum Id : std::uint32_t {
        kAOn,
        kATable,
        kAPosition,
        kAWarp,
        kAOctave,
        kASemi,
        kAFine,
        kAUnison,
        kADetune,
        kAPan,
        kALevel,
        kBOn,
        kBTable,
        kBPosition,
        kBWarp,
        kBOctave,
        kBSemi,
        kBFine,
        kBUnison,
        kBDetune,
        kBPan,
        kBLevel,
        kSubLevel,
        kSubOctave,
        kNoiseLevel,
        kNoiseColor,
        kFilterOn,
        kFilterType,
        kCutoff,
        kResonance,
        kDrive,
        kFilterMix,
        kKeyTrack,
        kRouteA,
        kRouteB,
        kRouteSub,
        kRouteNoise,
        kEnv1Attack,
        kEnv1Hold,
        kEnv1Decay,
        kEnv1Sustain,
        kEnv1Release,
        kEnv2Attack,
        kEnv2Hold,
        kEnv2Decay,
        kEnv2Sustain,
        kEnv2Release,
        kEnv2Target,
        kEnv2Amount,
        kEnv3Attack,
        kEnv3Hold,
        kEnv3Decay,
        kEnv3Sustain,
        kEnv3Release,
        kEnv3Target,
        kEnv3Amount,
        kLfo1Rate,
        kLfo1Shape,
        kLfo1Target,
        kLfo1Amount,
        kLfo1Retrigger,
        kLfo1Rise,
        kLfo1Delay,
        kLfo1Smooth,
        kLfo2Rate,
        kLfo2Shape,
        kLfo2Target,
        kLfo2Amount,
        kLfo2Retrigger,
        kLfo2Rise,
        kLfo2Delay,
        kLfo2Smooth,
        kLfo3Rate,
        kLfo3Shape,
        kLfo3Target,
        kLfo3Amount,
        kLfo3Retrigger,
        kLfo3Rise,
        kLfo3Delay,
        kLfo3Smooth,
        kLfo4Rate,
        kLfo4Shape,
        kLfo4Target,
        kLfo4Amount,
        kLfo4Retrigger,
        kLfo4Rise,
        kLfo4Delay,
        kLfo4Smooth,
        kMacro1,
        kMacro1Target,
        kMacro1Amount,
        kMacro2,
        kMacro2Target,
        kMacro2Amount,
        kMacro3,
        kMacro3Target,
        kMacro3Amount,
        kMacro4,
        kMacro4Target,
        kMacro4Amount,
        kMono,
        kLegato,
        kGlide,
        kVoices,
        kVolume,
        kParameterCount
    };

    /// What an envelope, an LFO or a macro moves (the value of a "Target" parameter).
    enum class Target { Off, Cutoff, APosition, BPosition, Pitch, Level };
    static constexpr int kTargets = 6;
    /// The shape of an LFO (the value of a "Shape" parameter).
    enum class LfoShape { Sine, Triangle, Saw, Square, Random };
    static constexpr int kLfoShapes = 5;
    enum class FilterType { LowPass, BandPass, HighPass };

    static constexpr int kModEnvelopes = 2; ///< envelopes 2 and 3: each moves a target
    static constexpr int kLfos = 4;
    static constexpr int kMacros = 4;
    static constexpr int kMaxVoices = 16;
    static constexpr int kMaxUnison = 7;
    static constexpr int kControlFrames = 16; ///< modulation is worked out once per this many frames
    static constexpr double kVolumeRampSeconds = 0.01;
    /// A full modulation amount (100 %) moves the cutoff this many octaves,
    /// the pitch this many semitones, a position across its whole table.
    static constexpr double kCutoffOctaves = 6.0;
    static constexpr double kPitchSemitones = 12.0;

    Synth() noexcept; ///< every parameter at its default

    [[nodiscard]] static ProcessorSpec spec() noexcept { return {"vloom.synth", "Synth", "1.0"}; }
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

    /// Voices sounding now (held or ringing out).
    [[nodiscard]] int activeVoices() const noexcept;

private:
    struct Lfo {
        float phase = 0.0f; ///< 0 .. 1
        float held = 0.0f; ///< the Random shape's value for this cycle
        float smoothed = 0.0f; ///< what it puts out, after Smooth
    };
    /// One oscillator of one voice, for the frames until the next control step.
    struct OscillatorRun {
        bool on = false;
        const Wavetable* table = nullptr;
        int level = 0; ///< the table's band limit for this pitch
        int copies = 1;
        float position = 0.0f;
        float bend = 0.5f; ///< warp: where the middle of the cycle is read from
        std::array<float, kMaxUnison> step{}; ///< phase per frame
        std::array<float, kMaxUnison> left{};
        std::array<float, kMaxUnison> right{};
    };
    struct Voice {
        bool active = false;
        bool held = false; ///< its key is down
        int key = 60;
        float velocity = 1.0f;
        float pitch = 60.0f; ///< where the glide is, in keys
        std::uint64_t started = 0; ///< order of noteOn calls: the oldest is stolen first
        Envelope level;
        std::array<Envelope, kModEnvelopes> mods; ///< envelopes 2 and 3
        std::array<Lfo, kLfos> lfos;
        float age = 0.0f; ///< seconds since the note started: an LFO's delay and rise count from it
        std::array<std::array<float, kMaxUnison>, 2> phases{};
        float subPhase = 0.0f;
        std::uint32_t noiseState = 1;
        float noiseLow = 0.0f; ///< the noise's low-passed part, for its colour
        std::array<VoiceFilterState, 2> filter{}; ///< left and right
        // Until the next control step:
        std::array<OscillatorRun, 2> runs;
        float subStep = 0.0f;
        float tremolo = 1.0f;
        float filterG = 0.0f;
        float filterK = 2.0f;
    };

    [[nodiscard]] double value(std::uint32_t id) const noexcept { return values_[id]; }
    [[nodiscard]] bool on(std::uint32_t id) const noexcept { return values_[id] >= 0.5; }
    /// Works the envelope shapes and other settings out of the values.
    void refresh() noexcept;
    [[nodiscard]] EnvelopeShape envelopeShape(std::uint32_t attackId) const noexcept;
    void start(Voice& voice, int key, float velocity, bool retrigger) noexcept;
    [[nodiscard]] Voice* freeVoice() noexcept;
    /// One control step for `voice`: its modulation, pitches and filter for the next frames.
    void control(Voice& voice, int frames) noexcept;
    [[nodiscard]] float lfoValue(Lfo& lfo, std::uint32_t rateId, int frames) noexcept;
    void render(Voice& voice, float* left, float* right, int frames) noexcept;
    void renderBlock(int frames) noexcept;

    std::array<double, kParameterCount> values_{};
    std::array<Voice, kMaxVoices> voices_{};
    std::array<int, kMaxVoices> monoKeys_{}; ///< keys held in mono mode, the newest last
    int monoCount_ = 0;
    std::array<const Wavetable*, Wavetable::kShapes> tables_{};
    std::array<EnvelopeShape, 1 + kModEnvelopes> envelopes_{}; ///< envelope 1, 2, 3
    std::array<Lfo, kLfos> freeLfos_{}; ///< where an LFO that does not retrigger is
    std::vector<float> mixLeft_;
    std::vector<float> mixRight_;
    SmoothedValue volume_{0.0f};
    double sampleRate_ = 0.0; ///< 0: not prepared, silent
    int maxBlockFrames_ = 0;
    std::uint64_t started_ = 0;
    float lastPitch_ = 60.0f; ///< where a new note's glide starts
    bool anyNoteYet_ = false;
    bool dirty_ = true; ///< a value changed since refresh()
    std::uint32_t seed_ = 0x9E3779B9u;
};

static_assert(Instrument<Synth>);

} // namespace vloom::dsp
