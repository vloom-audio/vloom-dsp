// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/instruments/Synth.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace vloom::dsp {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kVoiceGain = 0.5f; ///< room for several voices before the volume
constexpr float kDetuneSemitones = 0.5f; ///< unison at 100 %: the outer copies this far out
constexpr float kUnisonSpread = 0.8f; ///< how far the outer copies sit to the sides
constexpr int kOscillatorParameters = Synth::kBOn - Synth::kAOn;

constexpr const char* kA = "Oscillator A";
constexpr const char* kB = "Oscillator B";
constexpr const char* kSub = "Sub";
constexpr const char* kNoise = "Noise";
constexpr const char* kFilter = "Filter";
constexpr const char* kEnv1 = "Envelope 1";
constexpr const char* kMacroGroup = "Macros";
constexpr int kEnvelopeParameters = Synth::kEnv3Attack - Synth::kEnv2Attack;
constexpr int kLfoParameters = Synth::kLfo2Rate - Synth::kLfo1Rate;
constexpr int kMacroParameters = Synth::kMacro2 - Synth::kMacro1;
constexpr const char* kVoicing = "Voicing";

using P = ParameterSpec;
using S = Synth;

// In id order: kParameters[id] is parameter id (checked below).
constexpr std::array<ParameterSpec, Synth::kParameterCount> kParameters{
    P{S::kAOn, "Oscillator A", "", 0, 1, 1, true, kA},
    P{S::kATable, "Oscillator A Table", "", 0, Wavetable::kShapes - 1, 0, true, kA},
    P{S::kAPosition, "Oscillator A Position", "%", 0, 100, 67, false, kA},
    P{S::kAWarp, "Oscillator A Warp", "%", 0, 100, 0, false, kA},
    P{S::kAOctave, "Oscillator A Octave", "", -3, 3, 0, true, kA},
    P{S::kASemi, "Oscillator A Semi", "st", -12, 12, 0, true, kA},
    P{S::kAFine, "Oscillator A Fine", "ct", -100, 100, 0, false, kA},
    P{S::kAUnison, "Oscillator A Unison", "", 1, S::kMaxUnison, 1, true, kA},
    P{S::kADetune, "Oscillator A Detune", "%", 0, 100, 20, false, kA},
    P{S::kAPan, "Oscillator A Pan", "%", -100, 100, 0, false, kA},
    P{S::kALevel, "Oscillator A Level", "%", 0, 100, 80, false, kA},
    P{S::kBOn, "Oscillator B", "", 0, 1, 0, true, kB},
    P{S::kBTable, "Oscillator B Table", "", 0, Wavetable::kShapes - 1, 0, true, kB},
    P{S::kBPosition, "Oscillator B Position", "%", 0, 100, 100, false, kB},
    P{S::kBWarp, "Oscillator B Warp", "%", 0, 100, 0, false, kB},
    P{S::kBOctave, "Oscillator B Octave", "", -3, 3, 0, true, kB},
    P{S::kBSemi, "Oscillator B Semi", "st", -12, 12, 0, true, kB},
    P{S::kBFine, "Oscillator B Fine", "ct", -100, 100, 0, false, kB},
    P{S::kBUnison, "Oscillator B Unison", "", 1, S::kMaxUnison, 1, true, kB},
    P{S::kBDetune, "Oscillator B Detune", "%", 0, 100, 20, false, kB},
    P{S::kBPan, "Oscillator B Pan", "%", -100, 100, 0, false, kB},
    P{S::kBLevel, "Oscillator B Level", "%", 0, 100, 80, false, kB},
    P{S::kSubLevel, "Sub Level", "%", 0, 100, 0, false, kSub},
    P{S::kSubOctave, "Sub Octave", "", -2, -1, -1, true, kSub},
    P{S::kNoiseLevel, "Noise Level", "%", 0, 100, 0, false, kNoise},
    P{S::kNoiseColor, "Noise Color", "%", -100, 100, 0, false, kNoise},
    P{S::kFilterOn, "Filter", "", 0, 1, 1, true, kFilter},
    P{S::kFilterType, "Filter Type", "", 0, 2, 0, true, kFilter},
    P{S::kCutoff, "Filter Cutoff", "Hz", 20, 20000, 20000, false, kFilter},
    P{S::kResonance, "Filter Resonance", "%", 0, 100, 10, false, kFilter},
    P{S::kDrive, "Filter Drive", "%", 0, 100, 0, false, kFilter},
    P{S::kFilterMix, "Filter Mix", "%", 0, 100, 100, false, kFilter},
    P{S::kKeyTrack, "Filter Key Track", "%", 0, 100, 0, false, kFilter},
    P{S::kRouteA, "Filter Oscillator A", "", 0, 1, 1, true, kFilter},
    P{S::kRouteB, "Filter Oscillator B", "", 0, 1, 1, true, kFilter},
    P{S::kRouteSub, "Filter Sub", "", 0, 1, 1, true, kFilter},
    P{S::kRouteNoise, "Filter Noise", "", 0, 1, 1, true, kFilter},
    P{S::kEnv1Attack, "Envelope 1 Attack", "ms", 0, 10000, 2, false, kEnv1},
    P{S::kEnv1Hold, "Envelope 1 Hold", "ms", 0, 10000, 0, false, kEnv1},
    P{S::kEnv1Decay, "Envelope 1 Decay", "ms", 0, 10000, 300, false, kEnv1},
    P{S::kEnv1Sustain, "Envelope 1 Sustain", "%", 0, 100, 80, false, kEnv1},
    P{S::kEnv1Release, "Envelope 1 Release", "ms", 0, 10000, 200, false, kEnv1},
    P{S::kEnv2Attack, "Envelope 2 Attack", "ms", 0, 10000, 2, false, "Envelope 2"},
    P{S::kEnv2Hold, "Envelope 2 Hold", "ms", 0, 10000, 0, false, "Envelope 2"},
    P{S::kEnv2Decay, "Envelope 2 Decay", "ms", 0, 10000, 400, false, "Envelope 2"},
    P{S::kEnv2Sustain, "Envelope 2 Sustain", "%", 0, 100, 0, false, "Envelope 2"},
    P{S::kEnv2Release, "Envelope 2 Release", "ms", 0, 10000, 200, false, "Envelope 2"},
    P{S::kEnv2Target, "Envelope 2 Target", "", 0, S::kTargets - 1, 1, true, "Envelope 2"},
    P{S::kEnv2Amount, "Envelope 2 Amount", "%", -100, 100, 0, false, "Envelope 2"},
    P{S::kEnv3Attack, "Envelope 3 Attack", "ms", 0, 10000, 2, false, "Envelope 3"},
    P{S::kEnv3Hold, "Envelope 3 Hold", "ms", 0, 10000, 0, false, "Envelope 3"},
    P{S::kEnv3Decay, "Envelope 3 Decay", "ms", 0, 10000, 400, false, "Envelope 3"},
    P{S::kEnv3Sustain, "Envelope 3 Sustain", "%", 0, 100, 0, false, "Envelope 3"},
    P{S::kEnv3Release, "Envelope 3 Release", "ms", 0, 10000, 200, false, "Envelope 3"},
    P{S::kEnv3Target, "Envelope 3 Target", "", 0, S::kTargets - 1, 0, true, "Envelope 3"},
    P{S::kEnv3Amount, "Envelope 3 Amount", "%", -100, 100, 0, false, "Envelope 3"},
    P{S::kLfo1Rate, "LFO 1 Rate", "Hz", 0.01, 30, 2, false, "LFO 1"},
    P{S::kLfo1Shape, "LFO 1 Shape", "", 0, S::kLfoShapes - 1, 0, true, "LFO 1"},
    P{S::kLfo1Target, "LFO 1 Target", "", 0, S::kTargets - 1, 0, true, "LFO 1"},
    P{S::kLfo1Amount, "LFO 1 Amount", "%", -100, 100, 0, false, "LFO 1"},
    P{S::kLfo1Retrigger, "LFO 1 Retrigger", "", 0, 1, 1, true, "LFO 1"},
    P{S::kLfo1Rise, "LFO 1 Rise", "ms", 0, 5000, 0, false, "LFO 1"},
    P{S::kLfo1Delay, "LFO 1 Delay", "ms", 0, 5000, 0, false, "LFO 1"},
    P{S::kLfo1Smooth, "LFO 1 Smooth", "%", 0, 100, 0, false, "LFO 1"},
    P{S::kLfo2Rate, "LFO 2 Rate", "Hz", 0.01, 30, 5, false, "LFO 2"},
    P{S::kLfo2Shape, "LFO 2 Shape", "", 0, S::kLfoShapes - 1, 0, true, "LFO 2"},
    P{S::kLfo2Target, "LFO 2 Target", "", 0, S::kTargets - 1, 0, true, "LFO 2"},
    P{S::kLfo2Amount, "LFO 2 Amount", "%", -100, 100, 0, false, "LFO 2"},
    P{S::kLfo2Retrigger, "LFO 2 Retrigger", "", 0, 1, 1, true, "LFO 2"},
    P{S::kLfo2Rise, "LFO 2 Rise", "ms", 0, 5000, 0, false, "LFO 2"},
    P{S::kLfo2Delay, "LFO 2 Delay", "ms", 0, 5000, 0, false, "LFO 2"},
    P{S::kLfo2Smooth, "LFO 2 Smooth", "%", 0, 100, 0, false, "LFO 2"},
    P{S::kLfo3Rate, "LFO 3 Rate", "Hz", 0.01, 30, 0.5, false, "LFO 3"},
    P{S::kLfo3Shape, "LFO 3 Shape", "", 0, S::kLfoShapes - 1, 0, true, "LFO 3"},
    P{S::kLfo3Target, "LFO 3 Target", "", 0, S::kTargets - 1, 0, true, "LFO 3"},
    P{S::kLfo3Amount, "LFO 3 Amount", "%", -100, 100, 0, false, "LFO 3"},
    P{S::kLfo3Retrigger, "LFO 3 Retrigger", "", 0, 1, 1, true, "LFO 3"},
    P{S::kLfo3Rise, "LFO 3 Rise", "ms", 0, 5000, 0, false, "LFO 3"},
    P{S::kLfo3Delay, "LFO 3 Delay", "ms", 0, 5000, 0, false, "LFO 3"},
    P{S::kLfo3Smooth, "LFO 3 Smooth", "%", 0, 100, 0, false, "LFO 3"},
    P{S::kLfo4Rate, "LFO 4 Rate", "Hz", 0.01, 30, 8, false, "LFO 4"},
    P{S::kLfo4Shape, "LFO 4 Shape", "", 0, S::kLfoShapes - 1, 0, true, "LFO 4"},
    P{S::kLfo4Target, "LFO 4 Target", "", 0, S::kTargets - 1, 0, true, "LFO 4"},
    P{S::kLfo4Amount, "LFO 4 Amount", "%", -100, 100, 0, false, "LFO 4"},
    P{S::kLfo4Retrigger, "LFO 4 Retrigger", "", 0, 1, 1, true, "LFO 4"},
    P{S::kLfo4Rise, "LFO 4 Rise", "ms", 0, 5000, 0, false, "LFO 4"},
    P{S::kLfo4Delay, "LFO 4 Delay", "ms", 0, 5000, 0, false, "LFO 4"},
    P{S::kLfo4Smooth, "LFO 4 Smooth", "%", 0, 100, 0, false, "LFO 4"},
    P{S::kMacro1, "Macro 1", "%", 0, 100, 0, false, kMacroGroup},
    P{S::kMacro1Target, "Macro 1 Target", "", 0, S::kTargets - 1, 0, true, kMacroGroup},
    P{S::kMacro1Amount, "Macro 1 Amount", "%", -100, 100, 0, false, kMacroGroup},
    P{S::kMacro2, "Macro 2", "%", 0, 100, 0, false, kMacroGroup},
    P{S::kMacro2Target, "Macro 2 Target", "", 0, S::kTargets - 1, 0, true, kMacroGroup},
    P{S::kMacro2Amount, "Macro 2 Amount", "%", -100, 100, 0, false, kMacroGroup},
    P{S::kMacro3, "Macro 3", "%", 0, 100, 0, false, kMacroGroup},
    P{S::kMacro3Target, "Macro 3 Target", "", 0, S::kTargets - 1, 0, true, kMacroGroup},
    P{S::kMacro3Amount, "Macro 3 Amount", "%", -100, 100, 0, false, kMacroGroup},
    P{S::kMacro4, "Macro 4", "%", 0, 100, 0, false, kMacroGroup},
    P{S::kMacro4Target, "Macro 4 Target", "", 0, S::kTargets - 1, 0, true, kMacroGroup},
    P{S::kMacro4Amount, "Macro 4 Amount", "%", -100, 100, 0, false, kMacroGroup},
    P{S::kMono, "Mono", "", 0, 1, 0, true, kVoicing},
    P{S::kLegato, "Legato", "", 0, 1, 0, true, kVoicing},
    P{S::kGlide, "Glide", "ms", 0, 2000, 0, false, kVoicing},
    P{S::kVoices, "Voices", "", 1, S::kMaxVoices, 8, true, kVoicing},
    P{S::kVolume, "Volume", "dB", -60, 6, -6, false, ""},
};

constexpr bool inIdOrder()
{
    for (std::uint32_t id = 0; id < kParameters.size(); ++id)
        if (kParameters[id].id != id)
            return false;
    return true;
}
static_assert(inIdOrder());

std::uint32_t nextRandom(std::uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

/// -1 .. 1.
float randomSigned(std::uint32_t& state) noexcept
{
    return static_cast<float>(nextRandom(state) >> 8) * (2.0f / 16777216.0f) - 1.0f;
}

float keyToHz(float key) noexcept { return 440.0f * std::pow(2.0f, (key - 69.0f) / 12.0f); }

/// tanh, near enough for a drive stage, without the call.
float softClip(float x) noexcept
{
    x = std::clamp(x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

/// Warp: the cycle read so its middle comes at `bend` (0.5: unchanged).
float bent(float phase, float bend) noexcept
{
    return phase < bend ? 0.5f * phase / bend : 0.5f + 0.5f * (phase - bend) / (1.0f - bend);
}

float lfoShape(Synth::LfoShape shape, float phase, float held) noexcept
{
    switch (shape) {
    case Synth::LfoShape::Sine:
        return std::sin(2.0f * kPi * phase);
    case Synth::LfoShape::Triangle:
        return phase < 0.25f ? 4.0f * phase : phase < 0.75f ? 2.0f - 4.0f * phase : 4.0f * phase - 4.0f;
    case Synth::LfoShape::Saw:
        return 1.0f - 2.0f * phase;
    case Synth::LfoShape::Square:
        return phase < 0.5f ? 1.0f : -1.0f;
    case Synth::LfoShape::Random:
        break;
    }
    return held;
}

} // namespace

std::span<const ParameterSpec> Synth::parameters() noexcept { return kParameters; }

Synth::Synth() noexcept
{
    for (const ParameterSpec& parameter : kParameters)
        values_[parameter.id] = parameter.defaultValue;
}

void Synth::prepare(double sampleRate, int maxBlockFrames)
{
    const bool usable = std::isfinite(sampleRate) && sampleRate > 0.0 && maxBlockFrames > 0;
    sampleRate_ = usable ? sampleRate : 0.0;
    maxBlockFrames_ = usable ? maxBlockFrames : 0;
    mixLeft_.assign(static_cast<std::size_t>(maxBlockFrames_), 0.0f);
    mixRight_.assign(static_cast<std::size_t>(maxBlockFrames_), 0.0f);
    // Built here, not on the audio thread, whichever table is chosen later.
    for (int shape = 0; shape < Wavetable::kShapes; ++shape)
        tables_[static_cast<std::size_t>(shape)] = &Wavetable::builtin(static_cast<Wavetable::Shape>(shape));
    reset();
    refresh();
    volume_.reset(sampleRate_, kVolumeRampSeconds, volume_.target()); // no ramp from an old value
}

void Synth::setParameter(std::uint32_t id, double value) noexcept
{
    if (id >= kParameterCount || !std::isfinite(value))
        return;
    const ParameterSpec& spec = kParameters[id];
    value = std::clamp(value, spec.minValue, spec.maxValue);
    values_[id] = spec.stepped ? std::round(value) : value;
    dirty_ = true;
}

EnvelopeShape Synth::envelopeShape(std::uint32_t attackId) const noexcept
{
    const auto seconds = [this](std::uint32_t id) { return static_cast<float>(value(id) / 1000.0); };
    return EnvelopeShape::make(static_cast<float>(sampleRate_), seconds(attackId), seconds(attackId + 1),
        seconds(attackId + 2), static_cast<float>(value(attackId + 3) / 100.0), seconds(attackId + 4));
}

void Synth::refresh() noexcept
{
    dirty_ = false;
    if (sampleRate_ == 0.0)
        return;
    envelopes_[0] = envelopeShape(kEnv1Attack);
    for (std::uint32_t e = 0; e < kModEnvelopes; ++e)
        envelopes_[1 + e] = envelopeShape(kEnv2Attack + e * kEnvelopeParameters);
    const double db = value(kVolume);
    volume_.setTarget(
        db > kParameters[kVolume].minValue ? static_cast<float>(std::pow(10.0, db / 20.0)) : 0.0f);
    // Fewer voices allowed, or mono: the ones past the limit ring out.
    const int allowed = on(kMono) ? 1 : static_cast<int>(value(kVoices));
    for (int i = allowed; i < kMaxVoices; ++i) {
        Voice& voice = voices_[static_cast<std::size_t>(i)];
        voice.held = false;
        voice.level.release();
        for (Envelope& mod : voice.mods)
            mod.release();
    }
    if (!on(kMono))
        monoCount_ = 0;
}

void Synth::reset() noexcept
{
    for (Voice& voice : voices_) {
        voice.active = false;
        voice.held = false;
        voice.level = Envelope{};
        voice.mods = {};
        voice.filter = {};
    }
    monoCount_ = 0;
    anyNoteYet_ = false;
    freeLfos_ = {};
    volume_.setCurrentAndTarget(volume_.target());
}

int Synth::activeVoices() const noexcept
{
    return static_cast<int>(
        std::count_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.active; }));
}

Synth::Voice* Synth::freeVoice() noexcept
{
    const auto allowed = static_cast<std::size_t>(value(kVoices));
    Voice* released = nullptr; // the oldest ringing out
    Voice* oldest = nullptr;
    for (std::size_t i = 0; i < allowed; ++i) {
        Voice& voice = voices_[i];
        if (!voice.active)
            return &voice;
        if (!voice.held && (released == nullptr || voice.started < released->started))
            released = &voice;
        if (oldest == nullptr || voice.started < oldest->started)
            oldest = &voice;
    }
    return released != nullptr ? released : oldest;
}

void Synth::start(Voice& voice, int key, float velocity, bool retrigger) noexcept
{
    const bool gliding = value(kGlide) > 0.0 && anyNoteYet_;
    const bool continues = voice.active && on(kMono); // the one voice moves on: it glides from where it is
    if (!voice.active) {
        // A fresh voice: clean filter, and unison copies that start apart.
        voice.filter = {};
        voice.noiseLow = 0.0f;
        voice.noiseState = nextRandom(seed_) | 1u;
        voice.subPhase = 0.0f;
        for (auto& phases : voice.phases)
            for (std::size_t u = 0; u < phases.size(); ++u)
                phases[u] = u == 0 ? 0.0f : 0.5f * (randomSigned(seed_) + 1.0f);
        voice.level = Envelope{};
        voice.mods = {};
    }
    if (!continues || !gliding)
        voice.pitch = gliding ? lastPitch_ : static_cast<float>(key);
    if (retrigger || !voice.active) {
        // From wherever the envelopes are: a stolen voice does not click.
        voice.level.start();
        for (Envelope& mod : voice.mods)
            mod.start();
        voice.age = 0.0f;
        for (std::size_t i = 0; i < voice.lfos.size(); ++i) {
            const std::uint32_t retriggerId = kLfo1Retrigger + static_cast<std::uint32_t>(i) * kLfoParameters;
            voice.lfos[i].phase = on(retriggerId) ? 0.0f : freeLfos_[i].phase;
            voice.lfos[i].held = randomSigned(seed_);
            voice.lfos[i].smoothed = 0.0f;
        }
    }
    voice.key = key;
    voice.velocity = velocity;
    voice.active = true;
    voice.held = true;
    voice.started = ++started_;
    lastPitch_ = static_cast<float>(key);
    anyNoteYet_ = true;
}

void Synth::noteOn(int key, float velocity) noexcept
{
    if (sampleRate_ == 0.0)
        return;
    if (dirty_)
        refresh();
    key = std::clamp(key, 0, 127);
    velocity = std::isfinite(velocity) ? std::clamp(velocity, 0.0f, 1.0f) : 1.0f;
    if (velocity <= 0.0f) {
        noteOff(key); // as MIDI has it
        return;
    }
    if (on(kMono)) {
        // The key joins the ones held (once), the newest last.
        const auto end = std::remove(monoKeys_.begin(), monoKeys_.begin() + monoCount_, key);
        monoCount_ = static_cast<int>(end - monoKeys_.begin());
        if (monoCount_ == kMaxVoices) {
            std::copy(monoKeys_.begin() + 1, monoKeys_.end(), monoKeys_.begin());
            --monoCount_;
        }
        monoKeys_[static_cast<std::size_t>(monoCount_++)] = key;
        Voice& voice = voices_[0];
        start(voice, key, velocity, !(on(kLegato) && voice.active && voice.held));
        return;
    }
    const auto allowed = static_cast<std::size_t>(value(kVoices));
    for (std::size_t i = 0; i < allowed; ++i) {
        if (voices_[i].active && voices_[i].held && voices_[i].key == key) {
            start(voices_[i], key, velocity, true); // the same key again
            return;
        }
    }
    if (Voice* voice = freeVoice())
        start(*voice, key, velocity, true);
}

void Synth::noteOff(int key) noexcept
{
    if (on(kMono)) {
        const auto end = std::remove(monoKeys_.begin(), monoKeys_.begin() + monoCount_, key);
        monoCount_ = static_cast<int>(end - monoKeys_.begin());
        Voice& voice = voices_[0];
        if (!voice.held || voice.key != key)
            return; // not the key that sounds
        if (monoCount_ > 0) {
            // Back to the key still held under it.
            start(voice, monoKeys_[static_cast<std::size_t>(monoCount_ - 1)], voice.velocity, !on(kLegato));
            return;
        }
    }
    for (Voice& voice : voices_) {
        if (voice.held && voice.key == key) {
            voice.held = false;
            voice.level.release();
            for (Envelope& mod : voice.mods)
                mod.release();
        }
    }
}

float Synth::lfoValue(Lfo& lfo, std::uint32_t rateId, int frames) noexcept
{
    const auto shape = static_cast<LfoShape>(static_cast<int>(value(rateId + 1)));
    const float now = lfoShape(shape, lfo.phase, lfo.held);
    lfo.phase += static_cast<float>(value(rateId) * frames / sampleRate_);
    if (lfo.phase >= 1.0f) {
        lfo.phase -= std::floor(lfo.phase);
        lfo.held = randomSigned(seed_); // a new value each cycle
    }
    return now;
}

void Synth::control(Voice& voice, int frames) noexcept
{
    const auto rate = static_cast<float>(sampleRate_);

    // What envelope 2 and the LFOs move.
    float cutoffOctaves = 0.0f;
    std::array<float, 2> positionShift{};
    float pitchShift = 0.0f;
    float tremolo = 1.0f;
    const auto move = [&](std::uint32_t targetId, float source, bool bipolar) {
        const auto amount = static_cast<float>(value(targetId + 1) / 100.0);
        switch (static_cast<Target>(static_cast<int>(value(targetId)))) {
        case Target::Off:
            break;
        case Target::Cutoff:
            cutoffOctaves += static_cast<float>(kCutoffOctaves) * amount * source;
            break;
        case Target::APosition:
            positionShift[0] += amount * source;
            break;
        case Target::BPosition:
            positionShift[1] += amount * source;
            break;
        case Target::Pitch:
            pitchShift += static_cast<float>(kPitchSemitones) * amount * source;
            break;
        case Target::Level: {
            // The source as 0 .. 1 (turned over by a negative amount): at 0
            // the level is down by the amount, at 1 it is whole.
            float part = bipolar ? 0.5f * (source + 1.0f) : source;
            if (amount < 0.0f)
                part = 1.0f - part;
            tremolo *= 1.0f - std::abs(amount) * (1.0f - part);
            break;
        }
        }
    };
    for (std::uint32_t e = 0; e < kModEnvelopes; ++e)
        move(kEnv2Target + e * kEnvelopeParameters, voice.mods[e].level, false);
    for (std::uint32_t l = 0; l < kLfos; ++l) {
        const std::uint32_t rateId = kLfo1Rate + l * kLfoParameters;
        Lfo& lfo = voice.lfos[l];
        float out = lfoValue(lfo, rateId, frames);
        // It waits for Delay, then comes in over Rise.
        const auto delay = static_cast<float>(value(rateId + (kLfo1Delay - kLfo1Rate)) / 1000.0);
        const auto rise = static_cast<float>(value(rateId + (kLfo1Rise - kLfo1Rate)) / 1000.0);
        if (voice.age < delay)
            out = 0.0f;
        else if (rise > 0.0f)
            out *= std::min(1.0f, (voice.age - delay) / rise);
        // Smooth: its corners rounded, by up to a fifth of a second.
        const auto smooth = static_cast<float>(value(rateId + (kLfo1Smooth - kLfo1Rate)) / 100.0);
        lfo.smoothed = smooth > 0.0f
            ? out + (lfo.smoothed - out) * std::exp(-static_cast<float>(frames) / (0.2f * smooth * rate))
            : out;
        move(rateId + (kLfo1Target - kLfo1Rate), lfo.smoothed, true);
    }
    for (std::uint32_t m = 0; m < kMacros; ++m) {
        const std::uint32_t macroId = kMacro1 + m * kMacroParameters;
        move(macroId + 1, static_cast<float>(value(macroId) / 100.0), false);
    }
    voice.tremolo = tremolo;
    voice.age += static_cast<float>(frames) / rate;

    // Glide: the pitch closes in on the key, most of the way in the time set.
    const auto key = static_cast<float>(voice.key);
    const auto glideSeconds = static_cast<float>(value(kGlide) / 1000.0);
    voice.pitch = glideSeconds > 0.0f
        ? key + (voice.pitch - key) * std::exp(-4.0f * static_cast<float>(frames) / (glideSeconds * rate))
        : key;
    const float note = voice.pitch + pitchShift;

    for (std::size_t o = 0; o < 2; ++o) {
        const std::uint32_t base = kAOn + static_cast<std::uint32_t>(o) * kOscillatorParameters;
        OscillatorRun& run = voice.runs[o];
        run.on = on(base);
        if (!run.on)
            continue;
        run.table = tables_[static_cast<std::size_t>(value(base + (kATable - kAOn)))];
        run.position = std::clamp(
            static_cast<float>(value(base + (kAPosition - kAOn)) / 100.0) + positionShift[o], 0.0f, 1.0f);
        run.bend = 0.5f - 0.45f * static_cast<float>(value(base + (kAWarp - kAOn)) / 100.0);
        run.copies = static_cast<int>(value(base + (kAUnison - kAOn)));
        const auto detune = static_cast<float>(value(base + (kADetune - kAOn)) / 100.0) * kDetuneSemitones;
        const auto pan = static_cast<float>(value(base + (kAPan - kAOn)) / 100.0);
        const float level = static_cast<float>(value(base + (kALevel - kAOn)) / 100.0)
            / std::sqrt(static_cast<float>(run.copies));
        const float centre = note + 12.0f * static_cast<float>(value(base + (kAOctave - kAOn)))
            + static_cast<float>(value(base + (kASemi - kAOn)))
            + static_cast<float>(value(base + (kAFine - kAOn)) / 100.0);
        for (int u = 0; u < run.copies; ++u) {
            const auto i = static_cast<std::size_t>(u);
            // Copies from -1 to 1: flat to sharp, left to right.
            const float place = run.copies > 1
                ? 2.0f * static_cast<float>(u) / static_cast<float>(run.copies - 1) - 1.0f
                : 0.0f;
            run.step[i] = std::min(keyToHz(centre + place * detune) / rate, 0.49f);
            const float angle = (std::clamp(pan + place * kUnisonSpread, -1.0f, 1.0f) + 1.0f) * kPi / 4.0f;
            run.left[i] = std::cos(angle) * level;
            run.right[i] = std::sin(angle) * level;
        }
        run.level = Wavetable::levelFor(keyToHz(centre + detune), sampleRate_);
    }
    voice.subStep = std::min(keyToHz(note + 12.0f * static_cast<float>(value(kSubOctave))) / rate, 0.49f);

    const float tracked
        = cutoffOctaves + static_cast<float>(value(kKeyTrack) / 100.0) * (note - 60.0f) / 12.0f;
    const float cutoff
        = std::clamp(static_cast<float>(value(kCutoff)) * std::pow(2.0f, tracked), 20.0f, 0.45f * rate);
    voice.filterG = std::tan(kPi * cutoff / rate);
    voice.filterK = 2.0f * std::pow(0.03f, static_cast<float>(value(kResonance) / 100.0)); // Q 0.5 .. 17
}

void Synth::render(Voice& voice, float* left, float* right, int frames) noexcept
{
    const std::array<bool, 2> routed{on(kRouteA), on(kRouteB)};
    const auto subLevel = static_cast<float>(value(kSubLevel) / 100.0) * 0.7f;
    const auto noiseLevel = static_cast<float>(value(kNoiseLevel) / 100.0) * 0.5f;
    const auto noiseColor = static_cast<float>(value(kNoiseColor) / 100.0);
    const bool subRouted = on(kRouteSub);
    const bool noiseRouted = on(kRouteNoise);
    const bool filtering = on(kFilterOn);
    const auto type = static_cast<FilterType>(static_cast<int>(value(kFilterType)));
    const auto drive = static_cast<float>(value(kDrive) / 100.0);
    const float push = 1.0f + 7.0f * drive;
    const float pushBack = 1.0f / softClip(push);
    const auto filterMix = static_cast<float>(value(kFilterMix) / 100.0);
    const SvfCoefficients coefficients = SvfCoefficients::make(voice.filterG, voice.filterK);
    const Wavetable& sine = *tables_[0];
    const float gain = voice.velocity * voice.tremolo * kVoiceGain;

    for (int i = 0; i < frames; ++i) {
        std::array<float, 2> wet{}; // through the filter
        std::array<float, 2> dry{}; // around it
        for (std::size_t o = 0; o < 2; ++o) {
            const OscillatorRun& run = voice.runs[o];
            if (!run.on)
                continue;
            float l = 0.0f;
            float r = 0.0f;
            for (std::size_t u = 0; u < static_cast<std::size_t>(run.copies); ++u) {
                float& phase = voice.phases[o][u];
                const float sample = run.table->sample(run.position, bent(phase, run.bend), run.level);
                l += sample * run.left[u];
                r += sample * run.right[u];
                phase += run.step[u];
                if (phase >= 1.0f)
                    phase -= 1.0f;
            }
            std::array<float, 2>& into = routed[o] ? wet : dry;
            into[0] += l;
            into[1] += r;
        }
        if (subLevel > 0.0f) {
            const float sample = sine.sample(0.0f, voice.subPhase, 0) * subLevel;
            voice.subPhase += voice.subStep;
            if (voice.subPhase >= 1.0f)
                voice.subPhase -= 1.0f;
            std::array<float, 2>& into = subRouted ? wet : dry;
            into[0] += sample;
            into[1] += sample;
        }
        if (noiseLevel > 0.0f) {
            const float white = randomSigned(voice.noiseState);
            voice.noiseLow += 0.1f * (white - voice.noiseLow);
            // Dark: the low part, made up for its level. Bright: without it.
            const float coloured = noiseColor < 0.0f ? white + (3.0f * voice.noiseLow - white) * -noiseColor
                                                     : white - voice.noiseLow * noiseColor;
            std::array<float, 2>& into = noiseRouted ? wet : dry;
            into[0] += coloured * noiseLevel;
            into[1] += coloured * noiseLevel;
        }
        if (filtering) {
            for (std::size_t ch = 0; ch < 2; ++ch) {
                const float in = drive > 0.0f ? softClip(wet[ch] * push) * pushBack : wet[ch];
                // A state-variable filter that stays stable while its cutoff moves.
                const SvfState::Outputs filtered = voice.filter[ch].process(coefficients, in);
                const float out = type == FilterType::LowPass ? filtered.low
                    : type == FilterType::BandPass            ? filtered.band
                                                              : filtered.high;
                wet[ch] = in + (out - in) * filterMix;
            }
        }
        const float level = voice.level.next(envelopes_[0]) * gain;
        for (std::size_t e = 0; e < voice.mods.size(); ++e)
            (void)voice.mods[e].next(envelopes_[1 + e]);
        left[i] += (wet[0] + dry[0]) * level;
        right[i] += (wet[1] + dry[1]) * level;
    }
    for (SvfState& state : voice.filter) // no denormals left ringing in the filter
        state.flushDenormals();
    if (voice.level.stage == Envelope::Stage::Idle) {
        voice.active = false;
        voice.held = false;
    }
}

void Synth::renderBlock(int frames) noexcept
{
    std::fill_n(mixLeft_.begin(), frames, 0.0f);
    std::fill_n(mixRight_.begin(), frames, 0.0f);
    for (Voice& voice : voices_) {
        for (int offset = 0; offset < frames && voice.active; offset += kControlFrames) {
            const int count = std::min(kControlFrames, frames - offset);
            control(voice, count);
            render(voice, mixLeft_.data() + offset, mixRight_.data() + offset, count);
        }
    }
    // LFOs that do not retrigger run on whether or not a note sounds.
    for (std::size_t i = 0; i < freeLfos_.size(); ++i) {
        const std::uint32_t rateId = kLfo1Rate + static_cast<std::uint32_t>(i) * kLfoParameters;
        freeLfos_[i].phase += static_cast<float>(value(rateId) * frames / sampleRate_);
        freeLfos_[i].phase -= std::floor(freeLfos_[i].phase);
    }
}

void Synth::process(float* const* channels, int numChannels, int numFrames) noexcept
{
    if (sampleRate_ == 0.0 || numChannels <= 0 || numFrames <= 0)
        return;
    if (dirty_)
        refresh();
    for (int done = 0; done < numFrames; done += maxBlockFrames_) {
        const int frames = std::min(maxBlockFrames_, numFrames - done);
        renderBlock(frames);
        for (int i = 0; i < frames; ++i) {
            const float volume = volume_.next();
            const auto at = static_cast<std::size_t>(i);
            if (numChannels == 1) {
                channels[0][done + i] += 0.5f * (mixLeft_[at] + mixRight_[at]) * volume;
            } else {
                channels[0][done + i] += mixLeft_[at] * volume;
                channels[1][done + i] += mixRight_[at] * volume;
            }
        }
    }
}

} // namespace vloom::dsp
