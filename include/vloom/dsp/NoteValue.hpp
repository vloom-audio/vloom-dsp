// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace vloom::dsp {

/// A note length, for a time or a rate that follows the host's tempo.
struct NoteValue {
    const char* name = ""; ///< e.g. "1/8 D" (dotted), "1/8 T" (triplet)
    double beats = 1.0; ///< the length in beats (quarter notes)
};

/// The tempo a TempoAware processor assumes until the host sets one.
inline constexpr double kDefaultTempo = 120.0;
inline constexpr double kMinTempo = 1.0;
inline constexpr double kMaxTempo = 999.0;

/// The note lengths a "Note" choice parameter offers, longest first. The
/// index is the parameter's value and is saved in projects, so entries are
/// only ever appended.
inline constexpr std::array<NoteValue, 20> kNoteValues{{
    {"4/1", 16.0},
    {"2/1", 8.0},
    {"1/1 D", 6.0},
    {"1/1", 4.0},
    {"1/2 D", 3.0},
    {"1/1 T", 8.0 / 3.0},
    {"1/2", 2.0},
    {"1/4 D", 1.5},
    {"1/2 T", 4.0 / 3.0},
    {"1/4", 1.0},
    {"1/8 D", 0.75},
    {"1/4 T", 2.0 / 3.0},
    {"1/8", 0.5},
    {"1/16 D", 0.375},
    {"1/8 T", 1.0 / 3.0},
    {"1/16", 0.25},
    {"1/32 D", 0.1875},
    {"1/16 T", 1.0 / 6.0},
    {"1/32", 0.125},
    {"1/32 T", 1.0 / 12.0},
}};

inline constexpr int kNoteValueCount = static_cast<int>(kNoteValues.size());

/// Indices of a few common entries of kNoteValues.
namespace note {
inline constexpr int kTwoBars = 1;
inline constexpr int kWhole = 3;
inline constexpr int kHalf = 6;
inline constexpr int kQuarter = 9;
inline constexpr int kEighth = 12;
inline constexpr int kSixteenth = 15;
} // namespace note

/// The entry at `index`, clamped into the list.
[[nodiscard]] constexpr const NoteValue& noteValue(int index) noexcept
{
    return kNoteValues[static_cast<std::size_t>(std::clamp(index, 0, kNoteValueCount - 1))];
}

/// `bpm` kept between kMinTempo and kMaxTempo; kDefaultTempo if not finite.
[[nodiscard]] inline double clampTempo(double bpm) noexcept
{
    return std::isfinite(bpm) ? std::clamp(bpm, kMinTempo, kMaxTempo) : kDefaultTempo;
}

/// How long the note at `index` lasts at `bpm`, in seconds.
[[nodiscard]] inline double noteSeconds(int index, double bpm) noexcept
{
    return noteValue(index).beats * 60.0 / clampTempo(bpm);
}

/// The rate (Hz) of a cycle as long as the note at `index` at `bpm`.
[[nodiscard]] inline double noteHz(int index, double bpm) noexcept { return 1.0 / noteSeconds(index, bpm); }

} // namespace vloom::dsp
