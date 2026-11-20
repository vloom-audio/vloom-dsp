// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace vloom::dsp {

/// One parameter of a processor. Values are in the parameter's own units,
/// between minValue and maxValue.
struct ParameterSpec {
    std::uint32_t id = 0; ///< stable across versions and saved projects
    const char* name = "";
    const char* unit = ""; ///< e.g. "dB", "Hz", "%"; empty: none
    double minValue = 0.0;
    double maxValue = 1.0;
    double defaultValue = 0.0;
    bool stepped = false; ///< whole numbers only (a switch, a choice)
    /// What it belongs to, for a host that lays out controls, e.g. "Low
    /// Shelf"; parameters of one group follow each other. Empty: none.
    const char* group = "";
};

/// What describes a processor to a host.
struct ProcessorSpec {
    const char* id = ""; ///< unique and stable, e.g. "vloom.gain"
    const char* name = "";
    const char* version = "";
};

/// An effect that a host (vloom-engine, a plugin wrapper) can wrap.
///
/// - Non-real-time: `prepare()` sizes all memory for a sample rate and a
///   largest block; nothing else allocates.
/// - Real-time (noexcept, no allocation, no locks): `setParameter()` takes a
///   value in the parameter's units, clamps it and ignores NaN; `process()`
///   works in place on `numChannels` channels; `reset()` clears tails.
/// - Starts at every parameter's default; a host sets the values it saved.
template <typename T>
concept Effect = std::default_initializable<T>
    && requires(T effect, const T constEffect, double sampleRate, int frames, std::uint32_t id, double value,
        float* const* channels) {
           { T::spec() } -> std::same_as<ProcessorSpec>;
           { T::parameters() } -> std::convertible_to<std::span<const ParameterSpec>>;
           effect.prepare(sampleRate, frames);
           { effect.setParameter(id, value) } noexcept;
           { effect.process(channels, frames, frames) } noexcept;
           { effect.reset() } noexcept;
           { constEffect.latencyFrames() } noexcept -> std::same_as<int>;
       };

/// An instrument that a host can wrap: it is given notes and adds its sound.
///
/// Like an Effect (prepare() sizes all memory; the rest is real-time), with
/// notes in place of an input:
/// - `noteOn(key, velocity)` starts MIDI key `key` (0 .. 127) at `velocity`
///   (0 .. 1); `noteOff(key)` lets it ring out. Both take effect at the next
///   `process()`: a host splits its block at each note for sample accuracy.
/// - `process()` ADDS the instrument's sound to `numChannels` channels.
/// - `reset()` silences every note at once.
template <typename T>
concept Instrument = std::default_initializable<T>
    && requires(T instrument, const T constInstrument, double sampleRate, int frames, std::uint32_t id,
        double value, float* const* channels, int key, float velocity) {
           { T::spec() } -> std::same_as<ProcessorSpec>;
           { T::parameters() } -> std::convertible_to<std::span<const ParameterSpec>>;
           instrument.prepare(sampleRate, frames);
           { instrument.setParameter(id, value) } noexcept;
           { instrument.noteOn(key, velocity) } noexcept;
           { instrument.noteOff(key) } noexcept;
           { instrument.process(channels, frames, frames) } noexcept;
           { instrument.reset() } noexcept;
           { constInstrument.latencyFrames() } noexcept -> std::same_as<int>;
       };

// Optional capabilities (ADR 0003). A host checks for them with the concepts
// below; a processor without them is wrapped as before.

/// An Effect that can also take a key signal (sidechain).
///
/// `processWithSidechain(channels, key, numChannels, numKeyChannels, frames)`
/// is `process()` with a key: the effect works in place on `channels` as
/// usual, but its detector listens to the `numKeyChannels` read-only `key`
/// channels of the same `frames`. A host calls it only while a key is
/// connected, and plain `process()` otherwise; the effect then keys off its
/// own input. `key` null or `numKeyChannels` < 1 also means: no key.
template <typename T>
concept SidechainEffect
    = Effect<T> && requires(T e, float* const* ch, const float* const* key, int n, int k, int f) {
          { e.processWithSidechain(ch, key, n, k, f) } noexcept;
      };

/// A processor that follows the host's tempo (beats per minute, > 0).
///
/// `setTempo(bpm)` is real-time safe and takes effect at the next
/// `process()`; a host calls it before the first block and whenever the tempo
/// changes. A non-finite or non-positive value is ignored. Until it is
/// called, the processor assumes 120 BPM.
template <typename T>
concept TempoAware = requires(T p, double bpm) {
    { p.setTempo(bpm) } noexcept;
};

/// One live value a processor publishes for a UI (ADR 0005): a tuner's
/// frequency, a gate's open state, a compressor's gain reduction.
struct ReadoutSpec {
    const char* id = ""; ///< stable across versions, e.g. "gain_reduction_db"
    const char* name = "";
    const char* unit = ""; ///< e.g. "dB", "Hz"; empty: none
    double minValue = 0.0; ///< the range a meter shows; the value may leave it
    double maxValue = 1.0;
    bool stepped = false; ///< whole numbers only (a flag: 0 or 1)
};

/// A processor that publishes live values (ADR 0005).
///
/// - `readoutSpecs()` lists them. A readout's index is its position in that
///   list; entries are only ever appended, so indices and ids stay stable.
/// - `readout(index)` is the value now (as of the end of the last
///   `process()`): lock-free, wait-free, safe from any thread, while audio
///   runs. 0 for an index out of range; before prepare(), 0 too.
/// - `reset()` puts them back to their resting values (a meter's floor).
template <typename T>
concept HasReadouts = requires(const T constProcessor, std::size_t index) {
    { T::readoutSpecs() } -> std::convertible_to<std::span<const ReadoutSpec>>;
    { constProcessor.readout(index) } noexcept -> std::same_as<float>;
};

/// An instrument that follows the pitch bend wheel (ADR 0006).
///
/// `pitchBend(amount)` takes -1 (down) .. 1 (up), 0 at rest; how far that
/// bends is the instrument's business (Synth: its Bend Range parameter).
/// Real-time safe, on the thread that calls process(), and it takes effect
/// at the next process() for every voice. NaN is ignored; values out of range
/// are clamped. Until it is called, the wheel is at rest.
template <typename T>
concept PitchBendable = requires(T processor, float amount) {
    { processor.pitchBend(amount) } noexcept;
};

/// An instrument that follows the modulation wheel (ADR 0006).
///
/// `modWheel(amount)` takes 0 .. 1; what it moves is the instrument's
/// business. Same rules as `pitchBend()`. Until it is called, it is at 0.
template <typename T>
concept ModWheelAware = requires(T processor, float amount) {
    { processor.modWheel(amount) } noexcept;
};

/// The parameter with `id` in `list`, or nullptr.
[[nodiscard]] constexpr const ParameterSpec* findParameter(
    std::span<const ParameterSpec> list, std::uint32_t id) noexcept
{
    for (const ParameterSpec& parameter : list)
        if (parameter.id == id)
            return &parameter;
    return nullptr;
}

/// The index of the readout with `id` in `list`, or -1.
[[nodiscard]] constexpr int findReadout(std::span<const ReadoutSpec> list, std::string_view id) noexcept
{
    for (std::size_t i = 0; i < list.size(); ++i)
        if (id == list[i].id)
            return static_cast<int>(i);
    return -1;
}

} // namespace vloom::dsp
