# 0001. A separate DSP library

- Status: accepted
- Date: 2026-11-14

## Context

vloom-engine hosts processors behind one interface, `AudioProcessor`
(vloom-engine ADR 0006). Its milestone M5 adds built-in effects, and more
instruments and effects are planned, some of them to be sold on their own.

vloom-engine is GPL-3.0-or-later. Code written inside it is GPL too, and is
tied to the engine's types, so it could not be built as a stand-alone plugin
or used under another license.

## Decision

1. The effects and instruments live in this repository, vloom-dsp, under the
   MIT license.
2. The library depends only on the C++ standard library: not on
   vloom-engine, a plugin format or a UI toolkit.
3. vloom-engine pulls the library in with CPM, pinned to a commit, and wraps
   each processor in a thin `AudioProcessor` adapter.
4. Plugins sold separately are built in a separate, closed repository that
   may use this library. Nothing here depends on that repository.

## Alternatives considered

- Built-ins inside vloom-engine: the simplest for now, but the code would be
  GPL and tied to the engine, and could not ship as stand-alone plugins.
- One closed repository for all DSP: the open-source engine could then not be
  built without access to closed code.

## Consequences

- Changing an effect and the engine at once takes two pull requests and a
  pin bump in the engine.
- CI is set up a second time (copied from vloom-engine).
- The MIT license lets anyone reuse this code, also in closed products.
  Processors meant only for sale belong in the closed repository.
