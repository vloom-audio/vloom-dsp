# Review guide for Claude (vloom-dsp)

vloom-dsp is a C++20 library of audio effects and instruments. vloom-engine
wraps its processors as built-in plugins, and they may later ship as CLAP or
VST 3 plugins too. It depends on nothing but the standard library. Read
`docs/coding-guidelines.md` and the ADRs in `docs/adr/` when a change touches
their area.

## What matters here

1. **Real-time safety.** Anything called per block or per sample (`process`,
   `next`, parameter setters used while playing) must not allocate or free,
   lock, wait, log, do I/O or throw. Memory is sized in a prepare/reset step.
2. **DSP correctness.** Clicks (missing ramps on gain, cutoff and mix
   changes), NaN, infinity and denormals (in feedback paths and filters at
   low levels), filter stability at every sample rate and parameter value,
   out-of-bounds reads in delay lines and interpolation, aliasing in
   oscillators and non-linear stages, tails that never decay.
3. **Untrusted input.** Parameter values and saved state come from the host
   or a file: clamp ranges, reject NaN, guard divisions and huge sizes.
   Undefined behaviour counts as a bug (CI runs UBSan with
   `-fno-sanitize-recover`).
4. **Independence.** The library must not depend on vloom-engine, a plugin
   format or a UI toolkit (ADR 0001).
5. **Tests.** DSP tests render offline and check exact samples or measured
   responses. Every bug fix needs a test that fails without it.

## Conventions (only mention real violations)

- Public API changes need `CHANGELOG.md`; design decisions need an ADR.
- Commits: `<gitmoji> <type>(<scope>): <subject>`. clang-format is enforced by
  CI: do not comment on formatting. English everywhere.

## How to report

- Only verified problems, most severe first. For each: the file and line, a
  concrete scenario that goes wrong and a short suggested fix.
- Say "Must fix" or "Should fix". Skip style nits, speculation and praise.
- If the PR is fine, say so in one line.
