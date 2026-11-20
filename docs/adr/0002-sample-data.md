# 0002. Samples handed over by the host

- Status: accepted
- Date: 2026-11-20

## Context

The Sampler ("vloom.sampler") and the Drum Sampler ("vloom.drums") play
recorded audio. A parameter is a single number, so it cannot carry audio,
and the library does no file I/O (ADR 0001): it cannot open or decode a
file itself.

The host (vloom-engine, a plugin wrapper) has the file, a decoder and a
thread that is not the audio thread. A user may load a new sample while
notes play, so the swap has to be safe on the audio thread: no allocation,
no freeing, no locks there, ever (coding guidelines). Freeing the old sample
is the hard part. The audio thread is the last to read it, but it must not be
the one that frees it.

## Decision

1. **A new, optional capability.** `Sample.hpp` adds the `SampleSlots`
   concept, next to `Effect` and `Instrument`. A processor that plays samples
   has `sampleSlotCount()` numbered slots: one for the Sampler, slot n for pad
   n of the Drum Sampler.

   ```cpp
   template <typename T>
   concept SampleSlots = requires(T p, const T cp, int slot, std::shared_ptr<const SampleBuffer> b) {
       { cp.sampleSlotCount() } noexcept -> std::same_as<int>;
       p.setSample(slot, std::move(b));                  // host thread
       { cp.sample(slot) } -> std::same_as<std::shared_ptr<const SampleBuffer>>;
       { cp.sampleInfo(slot) } noexcept -> std::same_as<SampleInfo>;
       p.collectGarbage();                               // host thread
   };
   ```

2. **The host decodes.** It turns a file into floats at the file's own rate,
   mixes anything over two channels down to stereo, and calls
   `SampleBuffer::copy(channels, numChannels, frames, sampleRate)`. That makes
   an immutable `SampleBuffer`, held by `std::shared_ptr<const SampleBuffer>`.
   NaN and infinity become 0. Unusable input (no frames, more than two
   channels, a bad rate) gives nullptr. The processor resamples in real time:
   a sample plays at its own pitch whatever the host's rate.

3. **Threads.**
   - Host side: `setSample()`, `sample()`, `sampleInfo()` and
     `collectGarbage()` are not real-time and may allocate. The host calls
     them from one thread at a time (or serializes them), never concurrently
     with `prepare()` or destruction. They may run while `process()`,
     `noteOn()` and the rest run on the audio thread.
   - Audio side: the processor picks up the newest sample at the start of
     every block, and in `noteOn()`. A note started from then on plays the
     new sample. Notes still playing the old one fade out over a few
     milliseconds (`kFadeSeconds`), so the swap does not click. If a slot
     changes again before that fade ends, the older notes stop at once.

4. **Ownership and release.** `SampleSlot`, one per slot, does this without
   locks:
   - `set()` stores the new buffer's raw pointer in an atomic (`published`).
     The slot keeps the `shared_ptr` it was given, and moves the one it
     replaces to a `retired` list. Both live on the host side.
   - The audio thread only ever holds raw pointers. Before it reads a buffer,
     it marks it, hazard-pointer style: an `active` mark for what new notes
     play and a `fading` mark for what fading notes still read. After setting
     a mark it loads `published` again, and if that changed in between, it
     takes the newer one. `fadeDone()` clears the fading mark once no note
     reads that buffer.
   - `collectGarbage()` (and every `setSample()`) drops each retired buffer
     that neither mark points at. All atomics are sequentially consistent,
     and the collector loads the active mark before the fading one. So a
     buffer passing from one mark to the other is always seen in at least
     one of them.
   - So a buffer is freed on the host thread, by the last `shared_ptr`. That
     may be the slot's, or the host's own, if it keeps one, for example in a
     cache. It is never freed on the audio thread. The audio side does
     nothing but atomic loads and stores on pointers that are always lock
     free (checked by a `static_assert`).
   - A host calls `collectGarbage()` now and then (on a UI timer, say). Then
     the last replaced sample is released without waiting for the next
     `setSample()`. On destruction the slot frees everything. By then the
     audio thread has stopped.

5. **Metadata.** `sampleInfo(slot)` gives the channels, frames and rate of
   the sample set last (zeros when empty), for a UI or for saving.
   `sample(slot)` gives the buffer itself, for drawing a waveform.

6. **Sample positions.** The Sampler's Start, Loop Start and Loop End
   parameters are in percent of the sample's length, as doubles. Points
   given as fractions stay valid when a sample is swapped for another rate's
   version of the same file, and need no check against the sample when they
   are set. They are checked against the current sample when it is played.
   `Sampler::loopFrames(info, loopStart, loopEnd, crossfadeMs)` puts the loop
   within the sample, makes it at least `kMinLoopFrames` long, and keeps the
   crossfade within half the loop and the audio before it. A UI calls the
   same function to show the frames that are actually played.
   `Sampler::percentForFrame()` gives the exact value for a frame (it round
   trips even in a ten-minute sample).

## Alternatives considered

- **A file path as a parameter, the library decoding.** It needs I/O and
  decoders here, against ADR 0001, and a string cannot travel as a
  parameter.
- **A mutex around the sample.** The audio thread could wait on the UI
  thread: priority inversion and dropouts.
- **`std::atomic<std::shared_ptr>`.** It is not lock free on the standard
  libraries we build with. If the audio thread dropped the last reference,
  it would free the sample itself.
- **Swap in place, the host waiting a few blocks before freeing the old
  buffer.** It works only if every host gets the waiting right, and it
  breaks when a block takes longer than expected. The marks make freeing
  safe without any timing.
- **Epoch counter (blocks processed).** Equivalent, but notes that keep
  reading an old buffer across many blocks, such as a held looped note
  fading out, would need extra bookkeeping. The marks say exactly what is
  read.
- **Loop points in frames.** They are exact, but they are wrong as soon as
  the sample is replaced by one of another length or rate. They would also
  need checking on every `setParameter()` against a sample the audio thread
  may not have picked up yet.

## Consequences

- Hosts must implement the capability (`if constexpr (SampleSlots<P>)`):
  decode files, call `setSample()` off the audio thread, call
  `collectGarbage()` now and then, and save each slot's file path in the
  project. A path is not a parameter, so it goes in the processor's own
  state.
- A slot set twice faster than the audio thread runs drops the middle sample
  unheard. That is fine for a user loading files.
- A swap fades notes out instead of moving them to the new sample, so a held
  note goes silent until it is played again.
- Plugin wrappers (CLAP, VST 3) can map this onto their own state and
  threading in the same way: decode on the main thread, publish, collect.
