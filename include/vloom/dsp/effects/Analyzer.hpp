// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include "vloom/dsp/ParameterValues.hpp"
#include "vloom/dsp/Processor.hpp"
#include "vloom/dsp/SharedValue.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vloom::dsp {

/// "vloom.analyzer": a spectrum analyzer's settings and its peak meter.
///
/// The audio passes through bit for bit; the spectrum itself is drawn by
/// the host's UI from the audio it taps after this processor (ADR 0005):
/// an FFT of fftSize() frames (Fft.hpp does it), averaged over
/// averagingSeconds(), tilted by tiltDb(), Range dB tall, with a peak-hold
/// line when Peak Hold is on, and held still while Freeze is on. Keeping the
/// FFT out of the audio thread costs the audio nothing and lets the UI draw
/// at its own frame rate.
///
/// What runs here is the peak meter of the footer (readout "peak_db"): the
/// highest sample level of the chosen channel(s), held for kPeakHoldSeconds,
/// then falling at kPeakFallDbPerSecond. Freeze holds it too. No latency.
class Analyzer {
public:
    // Parameter ids: saved in projects, so they never change.
    static constexpr std::uint32_t kSpeed = 0;
    static constexpr std::uint32_t kTilt = 1;
    static constexpr std::uint32_t kRange = 2;
    static constexpr std::uint32_t kPeakHold = 3;
    static constexpr std::uint32_t kFreeze = 4;
    static constexpr std::uint32_t kResolution = 5;
    static constexpr std::uint32_t kChannel = 6;
    static constexpr std::size_t kParameterCount = 7;

    /// The value of the Speed parameter.
    enum class Speed { Slow, Medium, Fast };
    /// The value of the Channel parameter. Both: the spectrum of (L + R) / 2,
    /// the peak of the louder side.
    enum class Channel { Both, Left, Right };

    // Readouts (HasReadouts): indices into readoutSpecs(), only ever appended.
    enum Readout : std::size_t { kReadoutPeak, kReadoutCount };

    static constexpr int kResolutions = 5; ///< 1024 .. 16384
    static constexpr double kFloorDb = -120.0; ///< the peak readout of silence
    static constexpr double kPeakHoldSeconds = 1.0;
    static constexpr double kPeakFallDbPerSecond = 20.0;
    /// The frequency the tilt turns around (0 dB there).
    static constexpr double kTiltPivotHz = 1000.0;

    [[nodiscard]] static ProcessorSpec spec() noexcept
    {
        return {"vloom.analyzer", "Spectrum Analyzer", "1.0"};
    }
    [[nodiscard]] static std::span<const ParameterSpec> parameters() noexcept { return kParameters; }
    [[nodiscard]] static std::span<const ReadoutSpec> readoutSpecs() noexcept { return kReadouts; }

    void prepare(double sampleRate, int maxBlockFrames);
    void setParameter(std::uint32_t id, double value) noexcept;
    /// Leaves the audio as it is; measures the peak.
    void process(float* const* channels, int numChannels, int numFrames) noexcept;
    void reset() noexcept;
    [[nodiscard]] int latencyFrames() const noexcept { return 0; }

    /// The readout at `index` (see Readout). Any thread.
    [[nodiscard]] float readout(std::size_t index) const noexcept { return readouts_.load(index); }

    /// A parameter's value now (for the host's UI). 0 for a bad id.
    [[nodiscard]] double parameter(std::uint32_t id) const noexcept
    {
        return id < kParameterCount ? values_[id] : 0.0;
    }

    // For the host's UI, which draws the spectrum.

    /// The FFT size for a Resolution value: 1024, 2048, 4096, 8192, 16384.
    [[nodiscard]] static constexpr int fftSize(int resolution) noexcept
    {
        return 1024 << (resolution < 0 ? 0 : resolution >= kResolutions ? kResolutions - 1 : resolution);
    }
    /// How long the spectrum is averaged over for a Speed value (the time a
    /// one-pole average per bin takes to cover 63 % of a change).
    [[nodiscard]] static constexpr double averagingSeconds(int speed) noexcept
    {
        return speed <= 0 ? 0.8 : speed == 1 ? 0.25 : 0.06;
    }
    /// The gain the tilt adds at `hz`: `dbPerOctave` per octave above
    /// kTiltPivotHz (less below it). 0 for a bad frequency.
    [[nodiscard]] static double tiltDb(double hz, double dbPerOctave) noexcept;

private:
    static constexpr std::array<ParameterSpec, kParameterCount> kParameters{
        ParameterSpec{kSpeed, "Speed", "", 0.0, 2.0, 1.0, true, "Display"},
        ParameterSpec{kTilt, "Tilt", "dB/oct", 0.0, 6.0, 4.5, false, "Display"},
        ParameterSpec{kRange, "Range", "dB", 24.0, 120.0, 72.0, false, "Display"},
        ParameterSpec{kPeakHold, "Peak Hold", "", 0.0, 1.0, 1.0, true, "Display"},
        ParameterSpec{kFreeze, "Freeze", "", 0.0, 1.0, 0.0, true, "Display"},
        ParameterSpec{kResolution, "Resolution", "", 0.0, kResolutions - 1.0, 2.0, true, "Analysis"},
        ParameterSpec{kChannel, "Channel", "", 0.0, 2.0, 0.0, true, "Analysis"},
    };
    static constexpr std::array<ReadoutSpec, kReadoutCount> kReadouts{
        ReadoutSpec{"peak_db", "Peak", "dB", kFloorDb, 0.0, false},
    };

    [[nodiscard]] double value(std::uint32_t id) const noexcept { return values_[id]; }

    std::array<double, kParameterCount> values_ = defaultValues(kParameters);
    double sampleRate_ = 0.0; ///< 0: not prepared
    double peakDb_ = kFloorDb; ///< the meter now
    double holdLeft_ = 0.0; ///< seconds before it starts to fall
    ReadoutValues<kReadoutCount> readouts_;
};

static_assert(Effect<Analyzer>);
static_assert(HasReadouts<Analyzer>);

} // namespace vloom::dsp
