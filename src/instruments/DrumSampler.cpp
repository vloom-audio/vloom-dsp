// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/instruments/DrumSampler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace vloom::dsp {

namespace {

using D = DrumSampler;

constexpr double kMaxStep = 256.0; ///< frames of the sample per output frame, at most

/// A pad parameter, without the pad.
struct FieldSpec {
    const char* name;
    const char* unit;
    double minValue;
    double maxValue;
    double defaultValue; ///< for kKey: added to the pad's first key
    bool stepped;
};

// In PadField order.
constexpr std::array<FieldSpec, D::kPadFields> kFields{
    FieldSpec{"Key", "", 0, 127, 0, true},
    FieldSpec{"Tune", "st", -48, 48, 0, true},
    FieldSpec{"Fine", "ct", -100, 100, 0, false},
    FieldSpec{"Decay", "ms", 1, D::kNoDecayMs, D::kNoDecayMs, false},
    FieldSpec{"Release", "ms", 0, 5000, 20, false},
    FieldSpec{"Gain", "dB", -60, 12, 0, false},
    FieldSpec{"Pan", "%", -100, 100, 0, false},
    FieldSpec{"Start", "%", 0, 100, 0, false},
    FieldSpec{"Reverse", "", 0, 1, 0, true},
    FieldSpec{"Choke Group", "", 0, D::kChokeGroups, 0, true},
    FieldSpec{"Mode", "", 0, 1, 0, true},
    FieldSpec{"Velocity Sensitivity", "%", 0, 100, 100, false},
};

constexpr ParameterSpec kMaster{D::kMasterGain, "Master Gain", "dB", -60, 12, 0, false, "Master"};

double fieldDefault(int pad, std::uint32_t field) noexcept
{
    const double base = kFields[field].defaultValue;
    return field == D::kKey ? base + D::kFirstKey + pad : base;
}

/// The list with its names ("Pad 3 Tune") and groups ("Pad 3"), made once.
struct Table {
    std::array<std::array<char, 8>, D::kPads> groups{};
    std::array<std::array<char, 40>, D::kParameterCount> names{};
    std::array<ParameterSpec, D::kParameterCount> list{};

    Table() noexcept
    {
        list[0] = kMaster;
        std::size_t at = 1;
        for (int pad = 0; pad < D::kPads; ++pad) {
            auto& group = groups[static_cast<std::size_t>(pad)];
            std::snprintf(group.data(), group.size(), "Pad %d", pad + 1);
            for (std::uint32_t field = 0; field < D::kPadFields; ++field, ++at) {
                const FieldSpec& spec = kFields[field];
                std::snprintf(names[at].data(), names[at].size(), "Pad %d %s", pad + 1, spec.name);
                list[at] = ParameterSpec{D::padParameter(pad, static_cast<D::PadField>(field)),
                    names[at].data(), spec.unit, spec.minValue, spec.maxValue, fieldDefault(pad, field),
                    spec.stepped, group.data()};
            }
        }
    }
};

const Table& table() noexcept
{
    static const Table instance;
    return instance;
}

} // namespace

std::span<const ParameterSpec> DrumSampler::parameters() noexcept { return table().list; }

DrumSampler::DrumSampler() noexcept
{
    for (int pad = 0; pad < kPads; ++pad)
        for (std::uint32_t field = 0; field < kPadFields; ++field)
            pads_[static_cast<std::size_t>(pad)].values[field] = fieldDefault(pad, field);
    masterGainDb_ = kMaster.defaultValue;
}

void DrumSampler::prepare(double sampleRate, int maxBlockFrames)
{
    const bool usable = std::isfinite(sampleRate) && sampleRate > 0.0 && maxBlockFrames > 0;
    sampleRate_ = usable ? sampleRate : 0.0;
    maxBlockFrames_ = usable ? maxBlockFrames : 0;
    mixLeft_.assign(static_cast<std::size_t>(maxBlockFrames_), 0.0f);
    mixRight_.assign(static_cast<std::size_t>(maxBlockFrames_), 0.0f);
    fadeStep_ = usable ? static_cast<float>(1.0 / std::max(1.0, kFadeSeconds * sampleRate)) : 1.0f;
    reset();
    refresh();
    masterGain_.reset(sampleRate_, kRampSeconds, masterGain_.target()); // no ramp from an old value
}

void DrumSampler::setParameter(std::uint32_t id, double value) noexcept
{
    if (!std::isfinite(value))
        return;
    if (id == kMasterGain) {
        masterGainDb_ = std::clamp(value, kMaster.minValue, kMaster.maxValue);
        dirty_ = true;
        return;
    }
    if (id < kPadBase)
        return;
    const std::uint32_t pad = (id - kPadBase) / kPadStride;
    const std::uint32_t field = (id - kPadBase) % kPadStride;
    if (pad >= static_cast<std::uint32_t>(kPads) || field >= kPadFields)
        return;
    const FieldSpec& spec = kFields[field];
    value = std::clamp(value, spec.minValue, spec.maxValue);
    pads_[pad].values[field] = spec.stepped ? std::round(value) : value;
    dirty_ = true;
}

void DrumSampler::setSample(int slot, std::shared_ptr<const SampleBuffer> buffer)
{
    if (slot >= 0 && slot < kPads)
        slots_[static_cast<std::size_t>(slot)].set(std::move(buffer));
}

std::shared_ptr<const SampleBuffer> DrumSampler::sample(int slot) const
{
    return slot >= 0 && slot < kPads ? slots_[static_cast<std::size_t>(slot)].buffer() : nullptr;
}

SampleInfo DrumSampler::sampleInfo(int slot) const noexcept
{
    return slot >= 0 && slot < kPads ? slots_[static_cast<std::size_t>(slot)].info() : SampleInfo{};
}

void DrumSampler::collectGarbage() noexcept
{
    for (SampleSlot& slot : slots_)
        slot.collectGarbage();
}

void DrumSampler::refresh() noexcept
{
    dirty_ = false;
    if (sampleRate_ == 0.0)
        return;
    const auto rate = static_cast<float>(sampleRate_);
    for (Pad& pad : pads_) {
        const double decay = pad.values[kDecay];
        const bool decays = decay < kNoDecayMs;
        pad.shape = EnvelopeShape::make(rate, 0.0f, 0.0f, static_cast<float>(decay / 1000.0),
            decays ? 0.0f : 1.0f, static_cast<float>(pad.values[kRelease] / 1000.0));
        const double db = pad.values[kGain];
        const double gain = db > kFields[kGain].minValue ? std::pow(10.0, db / 20.0) : 0.0;
        const double pan = pad.values[kPan] / 100.0;
        pad.gainLeft = static_cast<float>(gain * std::min(1.0, 1.0 - pan));
        pad.gainRight = static_cast<float>(gain * std::min(1.0, 1.0 + pan));
    }
    masterGain_.setTarget(
        masterGainDb_ > kMaster.minValue ? static_cast<float>(std::pow(10.0, masterGainDb_ / 20.0)) : 0.0f);
}

void DrumSampler::reset() noexcept
{
    for (Voice& voice : voices_) {
        voice.active = false;
        voice.held = false;
        voice.fading = false;
        voice.buffer = nullptr;
    }
    for (SampleSlot& slot : slots_)
        slot.fadeDone(); // no voice reads an old sample now
    masterGain_.setCurrentAndTarget(masterGain_.target());
}

int DrumSampler::activeVoices() const noexcept
{
    return static_cast<int>(
        std::count_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.active; }));
}

void DrumSampler::fadeOut(Voice& voice) noexcept
{
    voice.fading = true;
    voice.held = false;
}

DrumSampler::Voice* DrumSampler::freeVoice() noexcept
{
    int sounding = 0;
    Voice* oldest = nullptr;
    for (Voice& voice : voices_) {
        if (!voice.active || voice.fading)
            continue;
        ++sounding;
        if (oldest == nullptr || voice.started < oldest->started)
            oldest = &voice;
    }
    if (sounding >= kMaxVoices && oldest != nullptr)
        fadeOut(*oldest);
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

void DrumSampler::start(int pad, float velocity) noexcept
{
    const SampleBuffer* buffer = slots_[static_cast<std::size_t>(pad)].active();
    if (buffer == nullptr)
        return;
    Voice* voice = freeVoice();
    if (voice == nullptr)
        return;
    const Pad& settings = pads_[static_cast<std::size_t>(pad)];
    *voice = Voice{};
    voice->active = true;
    voice->held = true;
    voice->pad = pad;
    voice->started = ++started_;
    voice->buffer = buffer;
    const auto sensitivity = static_cast<float>(settings.values[kVelocity] / 100.0);
    voice->velocityGain = 1.0f - sensitivity + sensitivity * velocity;
    const double frames = buffer->frames();
    const double offset = std::clamp(settings.values[kStart] / 100.0 * frames, 0.0, frames - 1.0);
    const bool reversed = settings.values[kReverse] >= 0.5;
    voice->direction = reversed ? -1.0 : 1.0;
    voice->position = reversed ? frames - 1.0 - offset : offset;
    voice->gainLeft.reset(sampleRate_, kRampSeconds, settings.gainLeft);
    voice->gainRight.reset(sampleRate_, kRampSeconds, settings.gainRight);
    voice->amp.start();
}

void DrumSampler::noteOn(int key, float velocity) noexcept
{
    if (sampleRate_ == 0.0)
        return;
    pickUpSamples(); // a sample set just now plays from this note on
    if (dirty_)
        refresh();
    key = std::clamp(key, 0, 127);
    velocity = std::isfinite(velocity) ? std::clamp(velocity, 0.0f, 1.0f) : 1.0f;
    if (velocity <= 0.0f) {
        noteOff(key); // as MIDI has it
        return;
    }
    // First every choke group hit cuts what it has playing, then the pads start.
    for (int pad = 0; pad < kPads; ++pad) {
        if (static_cast<int>(padValue(pad, kKey)) != key)
            continue;
        const auto group = static_cast<int>(padValue(pad, kChoke));
        if (group == 0 || slots_[static_cast<std::size_t>(pad)].active() == nullptr)
            continue;
        for (Voice& voice : voices_)
            if (voice.active && static_cast<int>(padValue(voice.pad, kChoke)) == group)
                fadeOut(voice);
    }
    for (int pad = 0; pad < kPads; ++pad)
        if (static_cast<int>(padValue(pad, kKey)) == key)
            start(pad, velocity);
}

void DrumSampler::noteOff(int key) noexcept
{
    for (Voice& voice : voices_) {
        if (!voice.held || static_cast<int>(padValue(voice.pad, kKey)) != key)
            continue;
        voice.held = false;
        if (static_cast<Mode>(static_cast<int>(padValue(voice.pad, kMode))) == Mode::Gate)
            voice.amp.release();
        // One-shot: it plays on.
    }
}

void DrumSampler::render(Voice& voice, float* left, float* right, int frames) noexcept
{
    const SampleBuffer& buffer = *voice.buffer;
    const bool stereo = buffer.numChannels() > 1;
    const Pad& pad = pads_[static_cast<std::size_t>(voice.pad)];
    const double semitones = pad.values[kTune] + pad.values[kFine] / 100.0;
    const double step = std::min(buffer.sampleRate() / sampleRate_ * std::exp2(semitones / 12.0), kMaxStep);
    const auto end = static_cast<double>(buffer.frames());
    voice.gainLeft.setTarget(pad.gainLeft);
    voice.gainRight.setTarget(pad.gainRight);

    const auto read = [&buffer](int channel, double position) noexcept {
        const auto at = static_cast<std::ptrdiff_t>(std::floor(position));
        const auto t = static_cast<float>(position - static_cast<double>(at));
        return hermite(buffer.at(channel, at - 1), buffer.at(channel, at), buffer.at(channel, at + 1),
            buffer.at(channel, at + 2), t);
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
        const float level = voice.amp.next(pad.shape) * voice.velocityGain * fade;
        const float l = read(0, voice.position);
        const float r = stereo ? read(1, voice.position) : l;
        left[i] += l * level * voice.gainLeft.next();
        right[i] += r * level * voice.gainRight.next();
        voice.position += step * voice.direction;
        if (voice.position >= end || voice.position <= -1.0) {
            voice.active = false;
            break;
        }
    }
    // Done: released to silence, or decayed to it.
    const bool decayed = voice.amp.stage == Envelope::Stage::Sustain && voice.amp.level <= 0.0f;
    if (voice.amp.idle() || decayed)
        voice.active = false;
    if (!voice.active)
        voice.held = false;
}

void DrumSampler::pickUpSamples() noexcept
{
    for (int pad = 0; pad < kPads; ++pad) {
        SampleSlot& slot = slots_[static_cast<std::size_t>(pad)];
        const SampleSlot::Update update = slot.update();
        if (update.changed) {
            for (Voice& voice : voices_) {
                if (!voice.active || voice.pad != pad)
                    continue;
                if (update.dropped != nullptr && voice.buffer == update.dropped)
                    voice.active = false; // it must not read that one again
                else if (update.fading != nullptr && voice.buffer == update.fading)
                    fadeOut(voice);
            }
        }
        if (const SampleBuffer* fading = slot.fading()) {
            const bool stillRead = std::any_of(voices_.begin(), voices_.end(),
                [fading, pad](const Voice& v) { return v.active && v.pad == pad && v.buffer == fading; });
            if (!stillRead)
                slot.fadeDone();
        }
    }
}

void DrumSampler::process(float* const* channels, int numChannels, int numFrames) noexcept
{
    if (sampleRate_ == 0.0 || numChannels <= 0 || numFrames <= 0)
        return;
    if (dirty_)
        refresh();
    pickUpSamples();
    for (int done = 0; done < numFrames; done += maxBlockFrames_) {
        const int frames = std::min(maxBlockFrames_, numFrames - done);
        std::fill_n(mixLeft_.begin(), frames, 0.0f);
        std::fill_n(mixRight_.begin(), frames, 0.0f);
        for (Voice& voice : voices_)
            if (voice.active)
                render(voice, mixLeft_.data(), mixRight_.data(), frames);
        for (int i = 0; i < frames; ++i) {
            const float gain = masterGain_.next();
            const auto at = static_cast<std::size_t>(i);
            if (numChannels == 1) {
                channels[0][done + i] += 0.5f * (mixLeft_[at] + mixRight_[at]) * gain;
            } else {
                channels[0][done + i] += mixLeft_[at] * gain;
                channels[1][done + i] += mixRight_[at] * gain;
            }
        }
    }
    pickUpSamples(); // sounds on an old sample may have finished in this block
}

} // namespace vloom::dsp
