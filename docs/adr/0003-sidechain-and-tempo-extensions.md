# 0003. Optional sidechain and tempo capabilities

- Status: accepted
- Date: 2026-11-20

## Context

The `Effect` concept (`Processor.hpp`) gives a host one way to run an
effect: `process(channels, numChannels, numFrames)`, in place. Two kinds of
built-in effects need more from the host:

- Dynamics (compressor, gate) are often keyed by another track: a kick
  ducking a bass, a gate opened by a snare. The detector must read a second
  signal, the key, while the effect processes its own input.
- Tempo-synced effects (delay times in note values, LFO rates in bars) need
  the host's tempo.

Most effects need neither. `Eq`, `Gain` and `Synth` must keep compiling and
working unchanged, and an effect must not have to pretend it supports a key
or a tempo. Several processors are written in parallel (vloom-dsp-fx-core,
vloom-dsp-fx-mod), and vloom-engine's adapter is written later, so the names
and signatures must be settled now.

## Decision

Two optional capabilities, each a concept in `Processor.hpp` that a host
tests at compile time. A processor without them is wrapped exactly as before.

```cpp
/// An Effect that can also take a key signal (sidechain).
template <typename T>
concept SidechainEffect = Effect<T> && requires(T e, float* const* ch, const float* const* key, int n, int k, int f) {
    { e.processWithSidechain(ch, key, n, k, f) } noexcept;
};

/// A processor that follows the host's tempo (beats per minute, > 0).
template <typename T>
concept TempoAware = requires(T p, double bpm) { { p.setTempo(bpm) } noexcept; };
```

### Sidechain

```cpp
void processWithSidechain(float* const* channels, const float* const* key,
                          int numChannels, int numKeyChannels, int numFrames) noexcept;
```

- Order: channels, key channels, numChannels, numKeyChannels, frames.
- `channels` are processed in place, exactly as by `process()`. `key` holds
  `numKeyChannels` read-only channels of the same `numFrames`, time-aligned
  with `channels`. The effect never writes to `key`.
- The host calls it only while a key is connected, and plain `process()`
  otherwise; the effect then keys off its own input. `key == nullptr` or
  `numKeyChannels < 1` also means "no key" (same result as `process()`).
- Channel mapping is the effect's business. The built-ins: a mono key feeds
  every detector channel; a stereo key feeds detector channel `i` from key
  channel `min(i, numKeyChannels - 1)`; with a stereo key and a mono input
  the detector still listens to both key channels.
- Same real-time rules as `process()`: noexcept, no allocation, no locks;
  any `numFrames`, independent of `prepare()`'s maximum.
- No extra latency: the key is used at the sample it arrives. If an effect
  ever delays its detector, it reports that in `latencyFrames()` as usual.

### Tempo

```cpp
void setTempo(double bpm) noexcept;
```

- Beats per minute, a beat being a quarter note. Real-time safe; takes
  effect at the next `process()` (any glide is the effect's business: the
  built-in delay glides to the new time like tape).
- The host calls it once before the first block, and again whenever the
  tempo changes (at block boundaries; a host may split a block at a tempo
  change). A non-finite or non-positive value is ignored; built-ins clamp to
  1 .. 1000 BPM.
- Until it is called, a processor assumes 120 BPM.
- Tempo only. Transport position (song position, playing or not) is left
  out until an effect needs it; it would be a third capability (for example
  `setTransport(double beatPosition, bool playing) noexcept`, concept
  `TransportAware`), so this one does not have to change.
- `TempoAware` does not require `Effect`, so instruments can use it too.

### Meters (convention, not a concept)

Dynamics effects report how much they turn the signal down for a meter as
`[[nodiscard]] float gainReductionDb() const noexcept` (dB, >= 0, the most
during the last block), lock-free and readable from any thread
(`SharedValue`). A host may show it when present.
ADR 0005 generalises this: the same values are also published as readouts
(`HasReadouts`), which a host can show without knowing the effect.

### Latency that changes

`latencyFrames()` may change after `setParameter()` for a parameter that
changes the latency (Limiter: Lookahead and True Peak; Saturator:
Oversampling). Such parameters are documented on the effect.

## What the engine adapter must do

1. At compile time, `if constexpr (SidechainEffect<T>)`: expose a sidechain
   input bus on the wrapped processor (stereo, accepting mono). Each block,
   if a key is routed and has signal buffers, call
   `processWithSidechain(channels, key, numChannels, numKeyChannels, frames)`;
   otherwise call `process()`. The key must be delay-compensated like any
   other input (it is consumed at the sample it arrives).
2. `if constexpr (TempoAware<T>)`: call `setTempo(bpm)` before the first
   `process()` after `prepare()`, and whenever the transport tempo changes,
   before the block it applies to.
3. Read `latencyFrames()` after `prepare()` and again after any
   `setParameter()`, and update plugin delay compensation when it changes.
4. Optionally poll `gainReductionDb()` from the UI thread for meters.

## Alternatives considered

- Adding a key argument to `process()` for every effect: breaks `Eq`,
  `Gain` and every host call, and most effects would ignore it.
- A key passed as extra channels of `process()` (channels 2 and 3): the
  effect cannot tell a key from a surround input, and an effect that does
  not know about keys would process them as audio.
- A virtual interface (`ISidechain`): the library uses concepts and static
  wrappers; virtual calls add nothing here and need a base class.
- A tempo argument to `process()` or a "process context" struct: more
  general, but it changes the shape of every effect for the sake of a few.
  A struct can still come later as its own capability.

## Consequences

- Existing effects and hosts are untouched. New capabilities are added the
  same way, each as its own concept.
- A host must handle the "connected but silent" and "not connected" cases
  differently: silence keys a compressor off silence (no reduction), no key
  keys it off its own input.
- Two processing entry points per sidechain effect: both must stay
  equivalent when no key is given (tested).
