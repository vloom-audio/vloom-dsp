# 0006. Pitch bend and mod wheel for instruments

- Status: accepted
- Date: 2026-11-21

## Context

The `Instrument` concept takes notes (`noteOn()`, `noteOff()`) and nothing
else. Players expect the pitch bend wheel to bend the synth, and the mod
wheel to move something. Both are channel-wide MIDI controllers, not notes
and not parameters: they move all the time while playing, are not saved in
projects and must not fill the undo history.

## Decision

Two optional capabilities in `Processor.hpp`, like `TempoAware` (ADR 0003):

```cpp
template <typename T>
concept PitchBendable = requires(T processor, float amount) {
    { processor.pitchBend(amount) } noexcept;
};

template <typename T>
concept ModWheelAware = requires(T processor, float amount) {
    { processor.modWheel(amount) } noexcept;
};
```

- `pitchBend(amount)`: -1 .. 1, 0 at rest (MIDI 14-bit value v as
  `(v - 8192) / 8192`, clamped). `modWheel(amount)`: 0 .. 1 (CC 1, with
  CC 33 as its fine part if the host has it, divided by its maximum).
- Real-time, called on the thread that calls `process()`, between blocks;
  takes effect at the next `process()` for every voice, held or ringing. A
  host splits a block at a controller change for sample accuracy, as for
  notes. NaN is ignored; values out of range are clamped.
- How far a bend goes and what the wheel moves are the instrument's
  business. Synth: Bend Range (0 .. 24 semitones, default 2, a parameter, so
  it is saved) and Mod Wheel Target / Mod Wheel Amount, which work like a
  macro's target and amount (default: the cutoff, +50 %; at rest the wheel
  changes nothing, so old projects sound the same).
- They are the host's controller state: `reset()` does not move them. A
  host that wants them centred after a stop calls `pitchBend(0)`.
- Neither requires `Instrument`, so an effect could follow them too.

The Synth also gets tempo sync for its LFOs: each has a Sync switch and a
Note (`NoteValue.hpp`), and with Sync on runs one cycle per note at the
tempo `setTempo()` gives (`TempoAware`). The new parameters are appended
after the existing ids (105 .. 115), so saved projects keep their values;
Sync defaults to off, so old projects run their LFOs in Hz as before.

## Alternatives considered

- Bend and wheel as parameters: the host would save and automate them, and
  a bend is not a setting.
- A generic `midiController(cc, value)`: every instrument would have to
  decode MIDI; two named capabilities say what they mean and stay small.
  Aftertouch or MPE can come later as capabilities of their own.

## Consequences

- vloom-engine's adapter: `if constexpr (PitchBendable<T>)` forward the
  track's pitch bend; `if constexpr (ModWheelAware<T>)` forward CC 1.
- Per-note bend (MPE) is not covered; it would need a key or voice argument
  and is left to a later capability.
