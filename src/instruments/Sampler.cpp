// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/instruments/Sampler.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace vloom::dsp {

namespace {

constexpr double kMaxStep = 256.0; ///< frames of the sample per output frame, at most

constexpr const char* kSample = "Sample";
constexpr const char* kKeys = "Keys";
constexpr const char* kLoop = "Loop";
constexpr const char* kAmp = "Amp Envelope";
constexpr const char* kFilter = "Filter";
constexpr const char* kFilterEnvelope = "Filter Envelope";
constexpr const char* kVoicing = "Voicing";
constexpr const char* kOutput = "Output";

using P = ParameterSpec;
using S = Sampler;

// In id order: kParameters[id] is parameter id (checked below).
constexpr std::array<ParameterSpec, Sampler::kParameterCount> kParameters{
    P{S::kRootKey, "Root Key", "", 0, 127, 60, true, kSample},
    P{S::kTranspose, "Transpose", "st", -48, 48, 0, true, kSample},
    P{S::kFineTune, "Fine Tune", "ct", -100, 100, 0, false, kSample},
    P{S::kStart, "Start", "%", 0, 100, 0, false, kSample},
    P{S::kKeyLow, "Key Low", "", 0, 127, 0, true, kKeys},
    P{S::kKeyHigh, "Key High", "", 0, 127, 127, true, kKeys},
    P{S::kVelocity, "Velocity Sensitivity", "%", 0, 100, 100, false, kKeys},
    P{S::kLoopMode, "Loop Mode", "", 0, 2, 0, true, kLoop},
    P{S::kLoopStart, "Loop Start", "%", 0, 100, 0, false, kLoop},
    P{S::kLoopEnd, "Loop End", "%", 0, 100, 100, false, kLoop},
    P{S::kLoopCrossfade, "Loop Crossfade", "ms", 0, 1000, 0, false, kLoop},
    P{S::kAttack, "Attack", "ms", 0, 10000, 1, false, kAmp},
    P{S::kDecay, "Decay", "ms", 0, 10000, 300, false, kAmp},
    P{S::kSustain, "Sustain", "%", 0, 100, 100, false, kAmp},
    P{S::kRelease, "Release", "ms", 0, 10000, 100, false, kAmp},
    P{S::kFilterType, "Filter Type", "", 0, 3, 0, true, kFilter},
    P{S::kCutoff, "Filter Cutoff", "Hz", 20, 20000, 20000, false, kFilter},
    P{S::kResonance, "Filter Resonance", "%", 0, 100, 0, false, kFilter},
    P{S::kFilterEnvAmount, "Filter Envelope Amount", "%", -100, 100, 0, false, kFilter},
    P{S::kKeyTrack, "Filter Key Track", "%", 0, 100, 0, false, kFilter},
    P{S::kFilterAttack, "Filter Attack", "ms", 0, 10000, 1, false, kFilterEnvelope},
    P{S::kFilterDecay, "Filter Decay", "ms", 0, 10000, 300, false, kFilterEnvelope},
    P{S::kFilterSustain, "Filter Sustain", "%", 0, 100, 0, false, kFilterEnvelope},
    P{S::kFilterRelease, "Filter Release", "ms", 0, 10000, 100, false, kFilterEnvelope},
    P{S::kMono, "Mono", "", 0, 1, 0, true, kVoicing},
    P{S::kLegato, "Legato", "", 0, 1, 0, true, kVoicing},
    P{S::kGlide, "Glide", "ms", 0, 2000, 0, false, kVoicing},
    P{S::kVoices, "Voices", "", 1, S::kMaxVoices, 16, true, kVoicing},
    P{S::kGain, "Gain", "dB", -60, 12, 0, false, kOutput},
    P{S::kPan, "Pan", "%", -100, 100, 0, false, kOutput},
};

constexpr bool inIdOrder()
{
    for (std::uint32_t id = 0; id < kParameters.size(); ++id)
        if (kParameters[id].id != id)
            return false;
    return true;
}
static_assert(inIdOrder());

/// The frame read for interpolation tap `frame`: taps past the end of a loop
/// come from where it goes on (`inLoop`: the note has reached the loop).
std::ptrdiff_t tapFrame(std::ptrdiff_t frame, Sampler::LoopMode mode, std::ptrdiff_t start,
    std::ptrdiff_t end, bool inLoop) noexcept
{
    switch (mode) {
    case Sampler::LoopMode::Off:
        break;
    case Sampler::LoopMode::Forward:
        if (frame >= end)
            return frame - (end - start);
        break;
    case Sampler::LoopMode::PingPong:
        if (frame >= end)
            return 2 * (end - 1) - frame;
        if (frame < start && inLoop)
            return 2 * start - frame;
        break;
    }
    return frame;
}

} // namespace

std::span<const ParameterSpec> Sampler::parameters() noexcept { return kParameters; }

Sampler::Sampler() noexcept
{
    for (const ParameterSpec& parameter : kParameters)
        values_[parameter.id] = parameter.defaultValue;
}

Sampler::LoopFrames Sampler::loopFrames(
    const SampleInfo& info, double loopStart, double loopEnd, double crossfadeMs) noexcept
{
    if (info.frames < 1 || !std::isfinite(loopStart) || !std::isfinite(loopEnd))
        return {};
    const auto frames = static_cast<double>(info.frames);
    const int shortest = std::min(kMinLoopFrames, info.frames);
    LoopFrames loop;
    loop.start = static_cast<int>(
        std::clamp(std::round(loopStart / 100.0 * frames), 0.0, static_cast<double>(info.frames - shortest)));
    loop.end = static_cast<int>(
        std::clamp(std::round(loopEnd / 100.0 * frames), static_cast<double>(loop.start + shortest), frames));
    const double crossfade
        = std::isfinite(crossfadeMs) ? std::round(crossfadeMs / 1000.0 * info.sampleRate) : 0.0;
    loop.crossfade = static_cast<int>(
        std::clamp(crossfade, 0.0, static_cast<double>(std::min(loop.start, (loop.end - loop.start) / 2))));
    return loop;
}

double Sampler::percentForFrame(int frame, int frames) noexcept
{
    return frames > 0 ? 100.0 * std::clamp(static_cast<double>(frame), 0.0, static_cast<double>(frames))
            / static_cast<double>(frames)
                      : 0.0;
}

void Sampler::prepare(double sampleRate, int maxBlockFrames)
{
    const bool usable = std::isfinite(sampleRate) && sampleRate > 0.0 && maxBlockFrames > 0;
    sampleRate_ = usable ? sampleRate : 0.0;
    maxBlockFrames_ = usable ? maxBlockFrames : 0;
    mixLeft_.assign(static_cast<std::size_t>(maxBlockFrames_), 0.0f);
    mixRight_.assign(static_cast<std::size_t>(maxBlockFrames_), 0.0f);
    fadeStep_ = usable ? static_cast<float>(1.0 / std::max(1.0, kFadeSeconds * sampleRate)) : 1.0f;
    reset();
    refresh();
    // No ramp from an old value.
    gainLeft_.reset(sampleRate_, kRampSeconds, gainLeft_.target());
    gainRight_.reset(sampleRate_, kRampSeconds, gainRight_.target());
}

void Sampler::setParameter(std::uint32_t id, double value) noexcept
{
    if (id >= kParameterCount || !std::isfinite(value))
        return;
    const ParameterSpec& spec = kParameters[id];
    value = std::clamp(value, spec.minValue, spec.maxValue);
    values_[id] = spec.stepped ? std::round(value) : value;
    dirty_ = true;
}

void Sampler::setSample(int slot, std::shared_ptr<const SampleBuffer> buffer)
{
    if (slot == 0)
        slot_.set(std::move(buffer));
}

std::shared_ptr<const SampleBuffer> Sampler::sample(int slot) const
{
    return slot == 0 ? slot_.buffer() : nullptr;
}

SampleInfo Sampler::sampleInfo(int slot) const noexcept { return slot == 0 ? slot_.info() : SampleInfo{}; }

Sampler::Loop Sampler::loopFor(const SampleBuffer& buffer) const noexcept
{
    const LoopFrames frames
        = loopFrames(buffer.info(), value(kLoopStart), value(kLoopEnd), value(kLoopCrossfade));
    return {static_cast<LoopMode>(static_cast<int>(value(kLoopMode))), static_cast<double>(frames.start),
        static_cast<double>(frames.end), static_cast<double>(frames.crossfade)};
}

int Sampler::allowedVoices() const noexcept { return on(kMono) ? 1 : static_cast<int>(value(kVoices)); }

void Sampler::refresh() noexcept
{
    dirty_ = false;
    if (sampleRate_ == 0.0)
        return;
    const auto rate = static_cast<float>(sampleRate_);
    const auto seconds = [this](std::uint32_t id) { return static_cast<float>(value(id) / 1000.0); };
    ampShape_ = EnvelopeShape::make(rate, seconds(kAttack), 0.0f, seconds(kDecay),
        static_cast<float>(value(kSustain) / 100.0), seconds(kRelease));
    filterShape_ = EnvelopeShape::make(rate, seconds(kFilterAttack), 0.0f, seconds(kFilterDecay),
        static_cast<float>(value(kFilterSustain) / 100.0), seconds(kFilterRelease));
    if (const SampleBuffer* buffer = slot_.active())
        loop_ = loopFor(*buffer);
    else
        loop_ = {};

    const double db = value(kGain);
    const double gain = db > kParameters[kGain].minValue ? std::pow(10.0, db / 20.0) : 0.0;
    const double pan = value(kPan) / 100.0;
    gainLeft_.setTarget(static_cast<float>(gain * std::min(1.0, 1.0 - pan)));
    gainRight_.setTarget(static_cast<float>(gain * std::min(1.0, 1.0 + pan)));

    // Fewer voices allowed, or mono: the oldest past the limit fade out.
    const int allowed = allowedVoices();
    for (;;) {
        int sounding = 0;
        Voice* oldest = nullptr;
        for (Voice& voice : voices_) {
            if (!voice.active || voice.fading)
                continue;
            ++sounding;
            if (oldest == nullptr || voice.started < oldest->started)
                oldest = &voice;
        }
        if (sounding <= allowed || oldest == nullptr)
            break;
        fadeOut(*oldest);
    }
    if (!on(kMono))
        monoCount_ = 0;
}

void Sampler::reset() noexcept
{
    for (Voice& voice : voices_) {
        voice.active = false;
        voice.held = false;
        voice.fading = false;
        voice.buffer = nullptr;
    }
    slot_.fadeDone(); // no voice reads an old sample now
    monoCount_ = 0;
    anyNoteYet_ = false;
    gainLeft_.setCurrentAndTarget(gainLeft_.target());
    gainRight_.setCurrentAndTarget(gainRight_.target());
}

int Sampler::activeVoices() const noexcept
{
    return static_cast<int>(
        std::count_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.active; }));
}

void Sampler::fadeOut(Voice& voice) noexcept
{
    voice.fading = true;
    voice.held = false;
}

Sampler::Voice* Sampler::freeVoice() noexcept
{
    // Past the limit: the oldest ringing out makes room, or else the oldest.
    int sounding = 0;
    Voice* released = nullptr;
    Voice* oldest = nullptr;
    for (Voice& voice : voices_) {
        if (!voice.active || voice.fading)
            continue;
        ++sounding;
        if (!voice.held && (released == nullptr || voice.started < released->started))
            released = &voice;
        if (oldest == nullptr || voice.started < oldest->started)
            oldest = &voice;
    }
    if (sounding >= allowedVoices() && oldest != nullptr)
        fadeOut(released != nullptr ? *released : *oldest);
    // A free voice; if every one is still fading out, the quietest.
    Voice* quietest = nullptr;
    for (Voice& voice : voices_) {
        if (!voice.active)
            return &voice;
        if (voice.fading && (quietest == nullptr || voice.fade < quietest->fade))
            quietest = &voice;
    }
    return quietest;
}

Sampler::Voice* Sampler::monoVoice() noexcept
{
    for (Voice& voice : voices_)
        if (voice.active && !voice.fading)
            return &voice;
    return nullptr;
}

void Sampler::start(Voice& voice, int key, float velocity) noexcept
{
    const SampleBuffer* buffer = slot_.active();
    const bool gliding = value(kGlide) > 0.0 && anyNoteYet_;
    voice = Voice{};
    voice.active = true;
    voice.held = true;
    voice.key = key;
    voice.velocity = velocity;
    const auto sensitivity = static_cast<float>(value(kVelocity) / 100.0);
    voice.velocityGain = 1.0f - sensitivity + sensitivity * velocity;
    voice.pitch = gliding ? lastPitch_ : static_cast<float>(key);
    voice.started = ++started_;
    voice.buffer = buffer;
    const double frames = buffer->frames();
    voice.position = std::clamp(value(kStart) / 100.0 * frames, 0.0, frames - 1.0);
    if (loop_.mode != LoopMode::Off && voice.position >= loop_.end)
        voice.position = loop_.start; // a start past the loop plays the loop
    voice.amp.start();
    voice.filterEnv.start();
    lastPitch_ = static_cast<float>(key);
    anyNoteYet_ = true;
}

void Sampler::noteOn(int key, float velocity) noexcept
{
    if (sampleRate_ == 0.0)
        return;
    pickUpSample(); // a sample set just now plays from this note on
    if (dirty_)
        refresh();
    key = std::clamp(key, 0, 127);
    velocity = std::isfinite(velocity) ? std::clamp(velocity, 0.0f, 1.0f) : 1.0f;
    if (velocity <= 0.0f) {
        noteOff(key); // as MIDI has it
        return;
    }
    const auto low = static_cast<int>(std::min(value(kKeyLow), value(kKeyHigh)));
    const auto high = static_cast<int>(std::max(value(kKeyLow), value(kKeyHigh)));
    if (key < low || key > high || slot_.active() == nullptr)
        return;

    if (on(kMono)) {
        // The key joins the ones held (once), the newest last.
        const auto end = std::remove(monoKeys_.begin(), monoKeys_.begin() + monoCount_, key);
        monoCount_ = static_cast<int>(end - monoKeys_.begin());
        if (monoCount_ == kMaxVoices) {
            std::copy(monoKeys_.begin() + 1, monoKeys_.end(), monoKeys_.begin());
            --monoCount_;
        }
        monoKeys_[static_cast<std::size_t>(monoCount_++)] = key;
        Voice* current = monoVoice();
        if (current != nullptr && current->held && on(kLegato)) {
            current->key = key; // the note moves on (gliding there, if Glide is set)
            if (value(kGlide) <= 0.0)
                current->pitch = static_cast<float>(key);
            lastPitch_ = static_cast<float>(key);
            return;
        }
        if (current != nullptr) {
            lastPitch_ = current->pitch; // the glide starts where the old note is
            fadeOut(*current);
        }
    } else {
        for (Voice& voice : voices_) {
            if (voice.active && voice.held
                && voice.key == key) { // the same key again: the old note rings out
                voice.held = false;
                voice.amp.release();
                voice.filterEnv.release();
            }
        }
    }
    if (Voice* voice = freeVoice())
        start(*voice, key, velocity);
}

void Sampler::noteOff(int key) noexcept
{
    if (on(kMono)) {
        const auto end = std::remove(monoKeys_.begin(), monoKeys_.begin() + monoCount_, key);
        monoCount_ = static_cast<int>(end - monoKeys_.begin());
        Voice* current = monoVoice();
        if (current == nullptr || !current->held || current->key != key)
            return; // not the key that sounds
        if (monoCount_ > 0) {
            // Back to the key still held under it.
            const int back = monoKeys_[static_cast<std::size_t>(monoCount_ - 1)];
            if (on(kLegato)) {
                current->key = back;
                if (value(kGlide) <= 0.0)
                    current->pitch = static_cast<float>(back);
                lastPitch_ = static_cast<float>(back);
                return;
            }
            const float velocity = current->velocity;
            lastPitch_ = current->pitch;
            fadeOut(*current);
            if (Voice* voice = freeVoice())
                start(*voice, back, velocity);
            return;
        }
    }
    for (Voice& voice : voices_) {
        if (voice.held && voice.key == key) {
            voice.held = false;
            voice.amp.release();
            voice.filterEnv.release();
        }
    }
}

void Sampler::control(Voice& voice) noexcept
{
    const auto rate = static_cast<float>(sampleRate_);
    // Glide: the pitch closes in on the key, most of the way in the time set.
    const auto key = static_cast<float>(voice.key);
    const auto glideSeconds = static_cast<float>(value(kGlide) / 1000.0);
    voice.pitch = glideSeconds > 0.0f ? key
            + (voice.pitch - key)
                * std::exp(-4.0f * static_cast<float>(kControlFrames) / (glideSeconds * rate))
                                      : key;
    const double fromRoot = static_cast<double>(voice.pitch) - value(kRootKey);
    const double semitones = fromRoot + value(kTranspose) + value(kFineTune) / 100.0;
    voice.step = std::min(voice.buffer->sampleRate() / sampleRate_ * std::exp2(semitones / 12.0), kMaxStep);

    if (static_cast<FilterType>(static_cast<int>(value(kFilterType))) != FilterType::Off) {
        const double octaves = value(kFilterEnvAmount) / 100.0 * kCutoffOctaves * voice.filterEnv.level
            + value(kKeyTrack) / 100.0 * fromRoot / 12.0;
        const double cutoff = value(kCutoff) * std::exp2(std::clamp(octaves, -20.0, 20.0));
        voice.coefficients = SvfCoefficients::design(
            static_cast<float>(cutoff), rate, static_cast<float>(value(kResonance) / 100.0));
    }
}

void Sampler::render(Voice& voice, float* left, float* right, int frames) noexcept
{
    const SampleBuffer& buffer = *voice.buffer;
    const bool stereo = buffer.numChannels() > 1;
    const auto type = static_cast<FilterType>(static_cast<int>(value(kFilterType)));
    const Loop loop = loop_;
    const auto loopStart = static_cast<std::ptrdiff_t>(loop.start);
    const auto loopEnd = static_cast<std::ptrdiff_t>(loop.end);
    const double loopLength = loop.end - loop.start;
    const double fadeFrom = loop.end - loop.crossfade; // a forward loop's crossfade starts here
    const auto sampleEnd = static_cast<double>(buffer.frames());

    const auto read = [&](int channel, double position) noexcept {
        const auto at = static_cast<std::ptrdiff_t>(std::floor(position));
        const auto t = static_cast<float>(position - static_cast<double>(at));
        const bool inLoop = position >= loop.start;
        const auto tap = [&](std::ptrdiff_t frame) noexcept {
            return buffer.at(channel, tapFrame(frame, loop.mode, loopStart, loopEnd, inLoop));
        };
        return hermite(tap(at - 1), tap(at), tap(at + 1), tap(at + 2), t);
    };
    const auto frameAt = [&](int channel, double position) noexcept {
        float out = read(channel, position);
        if (loop.mode == LoopMode::Forward && loop.crossfade > 0.0 && position >= fadeFrom) {
            // The end blends into the audio before the start, which it jumps to.
            const auto w = static_cast<float>((position - fadeFrom) / loop.crossfade);
            out += (read(channel, position - loopLength) - out) * std::min(w, 1.0f);
        }
        return out;
    };

    for (int i = 0; i < frames; ++i) {
        float fade = 1.0f;
        if (voice.fading) {
            voice.fade -= fadeStep_;
            if (voice.fade <= 0.0f) {
                voice.active = false;
                break;
            }
            fade = voice.fade;
        }
        const float level = voice.amp.next(ampShape_) * voice.velocityGain * fade;
        (void)voice.filterEnv.next(filterShape_);
        std::array<float, 2> out{frameAt(0, voice.position), 0.0f};
        out[1] = stereo ? frameAt(1, voice.position) : out[0];
        if (type != FilterType::Off) {
            for (std::size_t ch = 0; ch < (stereo ? 2u : 1u); ++ch) {
                const SvfState::Outputs filtered = voice.filter[ch].process(voice.coefficients, out[ch]);
                out[ch] = type == FilterType::LowPass ? filtered.low
                    : type == FilterType::BandPass    ? filtered.band
                                                      : filtered.high;
            }
            if (!stereo)
                out[1] = out[0];
        }
        left[i] += out[0] * level;
        right[i] += out[1] * level;

        // On to the next frame.
        voice.position += voice.step * voice.direction;
        switch (loop.mode) {
        case LoopMode::Off:
            if (voice.position >= sampleEnd)
                voice.active = false;
            break;
        case LoopMode::Forward:
            if (voice.position >= loop.end)
                voice.position = loop.start + std::fmod(voice.position - loop.start, loopLength);
            break;
        case LoopMode::PingPong: {
            const double last = loop.end - 1.0;
            if (voice.direction > 0 && voice.position > last) {
                voice.position = std::max(2.0 * last - voice.position, loop.start);
                voice.direction = -1;
            } else if (voice.direction < 0 && voice.position < loop.start) {
                voice.position = std::min(2.0 * loop.start - voice.position, last);
                voice.direction = 1;
            }
            break;
        }
        }
        if (!voice.active)
            break;
    }
    for (SvfState& state : voice.filter)
        state.flushDenormals();
    if (voice.amp.idle()) {
        voice.active = false;
        voice.held = false;
    }
}

void Sampler::pickUpSample() noexcept
{
    const SampleSlot::Update update = slot_.update();
    if (update.changed) {
        for (Voice& voice : voices_) {
            if (!voice.active)
                continue;
            if (update.dropped != nullptr && voice.buffer == update.dropped)
                voice.active = false; // it must not read that one again
            else if (update.fading != nullptr && voice.buffer == update.fading)
                fadeOut(voice);
        }
        if (const SampleBuffer* buffer = slot_.active())
            loop_ = loopFor(*buffer);
        else
            loop_ = {};
    }
    if (const SampleBuffer* fading = slot_.fading()) {
        const bool stillRead = std::any_of(voices_.begin(), voices_.end(),
            [fading](const Voice& v) { return v.active && v.buffer == fading; });
        if (!stillRead)
            slot_.fadeDone();
    }
}

void Sampler::renderBlock(int frames) noexcept
{
    std::fill_n(mixLeft_.begin(), frames, 0.0f);
    std::fill_n(mixRight_.begin(), frames, 0.0f);
    for (Voice& voice : voices_) {
        for (int offset = 0; offset < frames && voice.active; offset += kControlFrames) {
            const int count = std::min(kControlFrames, frames - offset);
            control(voice);
            render(voice, mixLeft_.data() + offset, mixRight_.data() + offset, count);
        }
    }
}

void Sampler::process(float* const* channels, int numChannels, int numFrames) noexcept
{
    if (sampleRate_ == 0.0 || numChannels <= 0 || numFrames <= 0)
        return;
    if (dirty_)
        refresh();
    pickUpSample();
    for (int done = 0; done < numFrames; done += maxBlockFrames_) {
        const int frames = std::min(maxBlockFrames_, numFrames - done);
        renderBlock(frames);
        for (int i = 0; i < frames; ++i) {
            const float gainLeft = gainLeft_.next();
            const float gainRight = gainRight_.next();
            const auto at = static_cast<std::size_t>(i);
            if (numChannels == 1) {
                channels[0][done + i] += 0.5f * (mixLeft_[at] * gainLeft + mixRight_[at] * gainRight);
            } else {
                channels[0][done + i] += mixLeft_[at] * gainLeft;
                channels[1][done + i] += mixRight_[at] * gainRight;
            }
        }
    }
    // Voices still fading on an old sample may have finished in this block.
    pickUpSample();
}

} // namespace vloom::dsp
