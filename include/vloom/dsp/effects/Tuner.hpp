// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include "vloom/dsp/Biquad.hpp"
#include "vloom/dsp/Fft.hpp"
#include "vloom/dsp/ParameterValues.hpp"
#include "vloom/dsp/Processor.hpp"
#include "vloom/dsp/SharedValue.hpp"
#include "vloom/dsp/SmoothedValue.hpp"

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vloom::dsp {

/// "vloom.tuner": a chromatic tuner for one note at a time.
///
/// - Detection: YIN (de Cheveigne and Kawahara, 2002). The input (both
///   channels averaged, or one) goes through a low-pass filter and, above
///   80 kHz, is decimated to 44.1 .. 64 kHz. Every kHopSeconds the difference
///   function of the last window is worked out with an FFT (fixed size, set
///   in prepare()), so the cost per block is bounded: at most one detection
///   per process() call. The period is refined with a parabola on the
///   difference function, which is exact to well under a cent for a steady
///   tone.
/// - Range kMinHz .. kMaxHz. Below kMinLevelDb, or when the best dip is not
///   clear enough (kThreshold), there is no pitch: has_pitch 0 and frequency,
///   note and cents 0.
/// - Readouts (HasReadouts): frequency, MIDI note (fractional, against the
///   Reference: A4 = Reference Hz), cents off the nearest note, has_pitch,
///   confidence (0 .. 1) and the input level (RMS since the last detection).
///
/// Audio passes through unchanged, or silent with Mute on (the default, as a
/// tuner is used while the band is quiet); the switch fades over
/// kRampSeconds. No latency.
class Tuner {
public:
    // Parameter ids: saved in projects, so they never change.
    static constexpr std::uint32_t kReference = 0;
    static constexpr std::uint32_t kMute = 1;
    static constexpr std::uint32_t kInput = 2;
    static constexpr std::size_t kParameterCount = 3;

    /// The value of the Input parameter.
    enum class Input { Both, Left, Right };

    // Readouts (HasReadouts): indices into readoutSpecs(), only ever appended.
    enum Readout : std::size_t {
        kReadoutFrequency,
        kReadoutNote,
        kReadoutCents,
        kReadoutHasPitch,
        kReadoutConfidence,
        kReadoutLevel,
        kReadoutCount
    };

    static constexpr double kMinHz = 30.0;
    static constexpr double kMaxHz = 2000.0;
    static constexpr double kMinLevelDb = -60.0; ///< quieter: no pitch
    static constexpr double kFloorDb = -120.0; ///< the level readout of silence
    static constexpr double kThreshold = 0.15; ///< YIN: a dip below this is a pitch
    static constexpr double kHopSeconds = 0.02; ///< between detections
    static constexpr double kRampSeconds = 0.01;

    [[nodiscard]] static ProcessorSpec spec() noexcept { return {"vloom.tuner", "Tuner", "1.0"}; }
    [[nodiscard]] static std::span<const ParameterSpec> parameters() noexcept { return kParameters; }
    [[nodiscard]] static std::span<const ReadoutSpec> readoutSpecs() noexcept { return kReadouts; }

    void prepare(double sampleRate, int maxBlockFrames);
    void setParameter(std::uint32_t id, double value) noexcept;
    void process(float* const* channels, int numChannels, int numFrames) noexcept;
    void reset() noexcept;
    [[nodiscard]] int latencyFrames() const noexcept { return 0; }

    /// The readout at `index` (see Readout). Any thread.
    [[nodiscard]] float readout(std::size_t index) const noexcept { return readouts_.load(index); }

    /// The rate detection runs at (the sample rate, or a quarter or half of
    /// it); 0 before prepare().
    [[nodiscard]] double detectionRate() const noexcept { return detectionRate_; }

private:
    static constexpr std::array<ParameterSpec, kParameterCount> kParameters{
        ParameterSpec{kReference, "Reference", "Hz", 415.0, 466.0, 440.0, false, "Tuner"},
        ParameterSpec{kMute, "Mute", "", 0.0, 1.0, 1.0, true, "Output"},
        ParameterSpec{kInput, "Input", "", 0.0, 2.0, 0.0, true, "Input"},
    };
    static constexpr std::array<ReadoutSpec, kReadoutCount> kReadouts{
        ReadoutSpec{"frequency_hz", "Frequency", "Hz", 0.0, kMaxHz, false},
        ReadoutSpec{"note", "Note", "", 0.0, 127.0, false},
        ReadoutSpec{"cents", "Cents", "ct", -50.0, 50.0, false},
        ReadoutSpec{"has_pitch", "Has Pitch", "", 0.0, 1.0, true},
        ReadoutSpec{"confidence", "Confidence", "", 0.0, 1.0, false},
        ReadoutSpec{"level_db", "Input Level", "dB", kFloorDb, 0.0, false},
    };

    [[nodiscard]] double value(std::uint32_t id) const noexcept { return values_[id]; }
    /// Runs YIN on the window that ends at the newest sample.
    void detect() noexcept;
    void publish(double frequency, double confidence) noexcept;

    std::array<double, kParameterCount> values_ = defaultValues(kParameters);
    double sampleRate_ = 0.0; ///< 0: not prepared, passes through
    double detectionRate_ = 0.0;
    int decimation_ = 1;
    int decimationPhase_ = 0;
    std::array<BiquadCoefficients, 2> lowPass_{}; ///< 4th order, before decimation
    std::array<BiquadState, 2> lowPassState_{};
    std::size_t window_ = 0; ///< W: frames summed per lag
    std::size_t minLag_ = 0;
    std::size_t maxLag_ = 0;
    std::vector<float> history_; ///< ring of the last window_ + maxLag_ + 2 detection frames
    std::size_t historyAt_ = 0; ///< where the next frame goes
    std::size_t historyFilled_ = 0;
    std::size_t hop_ = 0; ///< detection frames between detections
    std::size_t sinceDetection_ = 0;
    Fft fft_;
    std::vector<std::complex<double>> spectrum_;
    std::vector<double> frame_; ///< the window, oldest first
    std::vector<double> difference_; ///< d(lag), lag 0 .. maxLag_ + 1
    double levelSum_ = 0.0; ///< sum of squares of the input since the last detection
    std::size_t levelFrames_ = 0;
    double lastFrequency_ = 0.0; ///< 0: no pitch
    double lastConfidence_ = 0.0;
    SmoothedValue gain_{0.0f}; ///< 0 muted, 1 through
    ReadoutValues<kReadoutCount> readouts_;
};

static_assert(Effect<Tuner>);
static_assert(HasReadouts<Tuner>);

} // namespace vloom::dsp
