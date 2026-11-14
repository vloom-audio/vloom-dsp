# Coding guidelines

vloom-dsp follows the same rules as vloom-engine, with a few of its own.

## Real-time rules

Code that runs per block or per sample must not:

- allocate or free memory (`new`, `push_back`, `std::string`, a copied
  `std::function`);
- lock a mutex, wait, sleep, log or do I/O;
- throw.

Size every buffer in a `prepare` or `reset` step that takes the sample rate
and the largest block, before audio starts.

## DSP

- Ramp every parameter the user can move while audio plays (`SmoothedValue`),
  so a change never clicks.
- Clamp parameter values and reject NaN and infinity at the boundary. Saved
  state is untrusted input.
- Keep feedback paths and filters free of denormals.
- A processor must be stable at every supported sample rate (44.1 kHz to
  192 kHz) and every parameter value.

## Independence

The library depends only on the C++ standard library. It must not include
vloom-engine, a plugin SDK or a UI toolkit (ADR 0001).

## Style

- C++20. Classes `CamelCase`, functions and variables `camelBack`, private
  members end in `_`, constants start with `k`.
- Namespace `vloom::dsp`. Public headers in `include/vloom/dsp/`.
- Every file starts with `// SPDX-License-Identifier: MIT`.
- Format with `scripts/format.sh` (clang-format 23.1.3, checked by CI).
- Code, comments and commits in English.

## Tests

- Catch2, under `tests/`, named `<Name>Tests.cpp`.
- DSP tests render offline and check exact samples or measured responses.
- Every bug fix comes with a test that fails without it.

## Commits

`<gitmoji> <type>(<scope>): <subject>`, for example
`✨ feat(filter): add a shelf mode`. `scripts/check_commit_messages.sh` checks
this in CI.

## Decisions

Copy [`adr/0000-template.md`](adr/0000-template.md) to the next number.
