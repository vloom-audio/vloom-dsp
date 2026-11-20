# 0005. Readouts: live values a processor publishes

- Status: accepted
- Date: 2026-11-21

## Context

Several processors measure something a UI must show while audio plays:

- a tuner's detected frequency, note and cents;
- a gate's open state;
- a compressor's, limiter's or de-esser's gain reduction;
- an analyzer's peak level.

Until now each did it its own way (ADR 0003 "Meters"): `gainReductionDb()`
on the dynamics effects, `isOpen()` on the gate. A host had to know every
processor's methods to show them, and a generic plugin wrapper (CLAP, VST 3)
could show none. Parameters already have a generic description
(`ParameterSpec`); measured values need one too.

Constraints: the audio thread writes, a UI thread reads, with no lock and no
allocation; the processor stays copyable; existing methods keep working.

## Decision

An optional capability in `Processor.hpp`, tested at compile time like those
of ADR 0003:

```cpp
struct ReadoutSpec {
    const char* id = "";   // stable, e.g. "gain_reduction_db"
    const char* name = "";
    const char* unit = ""; // "dB", "Hz", ...; empty: none
    double minValue = 0.0; // the range a meter shows
    double maxValue = 1.0;
    bool stepped = false;  // a flag or a whole number
};

template <typename T>
concept HasReadouts = requires(const T constProcessor, std::size_t index) {
    { T::readoutSpecs() } -> std::convertible_to<std::span<const ReadoutSpec>>;
    { constProcessor.readout(index) } noexcept -> std::same_as<float>;
};

constexpr int findReadout(std::span<const ReadoutSpec> list, std::string_view id) noexcept; // index or -1
```

- `readoutSpecs()` is static, like `parameters()`. A readout's index is its
  position in the list; each processor names them in an enum
  (`Gate::kReadoutOpen`). Entries are only ever appended, so ids and indices
  stay stable; a host should look a readout up by id once and keep the index.
- `readout(index)` returns the value as of the end of the last `process()`.
  It is lock-free and wait-free and may be called from any thread at any
  time, during `process()` too. An index out of range gives 0. Before
  `prepare()` every readout is 0; `reset()` puts them at their resting
  value (a level readout at its floor, a pitch at 0).
- Values are `float`: what a meter needs, and lock-free everywhere. A flag is
  0 or 1 (`stepped`).
- Behind them: `SharedValue<float>` (relaxed atomics), or `ReadoutValues<N>`
  (`SharedValue.hpp`), N of them with bounds-checked `store()`/`load()`.
  Readouts are independent values: a UI may see a frequency from one
  detection and a level from the next. A processor that needs several values
  to agree publishes them so that each one alone makes sense: the tuner
  writes `has_pitch` after the frequency, note and cents, and sets all three
  to 0 when there is no pitch.
- The existing methods stay: `gainReductionDb()` and `isOpen()` return the
  same values as the readouts.

Readouts now:

| Processor | Readouts |
|---|---|
| Gate | `open` (0/1), `gain_reduction_db` |
| Compressor, Limiter, DeEsser | `gain_reduction_db` |
| Tuner | `frequency_hz`, `note`, `cents`, `has_pitch`, `confidence`, `level_db` |
| Analyzer | `peak_db` |

### Where the analyzer's FFT lives

The analyzer (`vloom.analyzer`) is a pass-through: its audio is untouched,
and it holds the settings of the display (Speed, Tilt, Range, Peak Hold,
Freeze, Resolution, Channel) as ordinary parameters, plus the `peak_db`
readout. The spectrum is computed by the host's UI from the audio it taps
after the processor, with `Fft` (`Fft.hpp`, the same transform the tuner
uses) and the analyzer's helpers (`fftSize()`, `averagingSeconds()`,
`tiltDb()`).

Why not an FFT in the processor publishing a spectrum:

- A spectrum is 512 .. 8192 values, too many for readouts; it would need a
  lock-free buffer with its own snapshot protocol, for one processor.
- The UI draws at its own frame rate (30 .. 60 Hz). An FFT on the audio
  thread would run at the audio's rate whether or not anyone looks, costing
  the audio budget for nothing when the window is closed.
- Freeze, peak hold, averaging and tilt are display state; on the UI side
  they cost nothing and change without touching the audio thread.
- The host already has a tap of every track's audio for its meters.

The cost: the host must tap the audio and run the FFT; a plugin wrapper
must do the same (a ring buffer from the audio thread to the editor, which
CLAP and VST 3 wrappers usually have anyway).

The tuner, by contrast, does its detection in the processor: it is a few
numbers, it must run whether the window is open or not (a tuner in a rack
drives a small display), and its cost is bounded (one fixed-size detection
per block at most, every 20 ms).

## Alternatives considered

- Keep per-processor methods (`gainReductionDb()`, `isOpen()`, ...): a host
  must special-case each processor, a plugin wrapper cannot show them.
- Readouts as read-only parameters (`ParameterSpec` with an output flag):
  parameters are set by the host and saved in projects; readouts are neither,
  and mixing them risks a host saving or automating a meter.
- A callback or message queue from the audio thread: needs a consumer and
  allocation or a fixed queue; a UI only wants the latest value anyway.
- `std::string_view` for the spec fields: the other specs (`ParameterSpec`,
  `ProcessorSpec`) use `const char*`; kept the same so hosts handle them alike.

## Consequences

- A host shows any processor's meters from `readoutSpecs()` alone, and polls
  them on its UI timer.
- Readouts are not saved, not automated and carry no undo.
- New readouts are appended; renaming or removing one breaks hosts that look
  it up, like changing a parameter id.
