// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include "vloom/dsp/ParameterValues.hpp"
#include "vloom/dsp/Processor.hpp"
#include "vloom/dsp/SharedValue.hpp"
#include "vloom/dsp/SidechainFilter.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vloom::dsp {

/// "vloom.gate": a noise gate.
///
/// - Opens when the key's peak level rises above the threshold, and closes
///   when it has stayed below threshold minus hysteresis for the hold time.
///   The channels share one detector (the louder channel wins), so a stereo
///   image stays put.
/// - Closed, it turns the signal down by the range (at kMinRangeDb: silent).
///   It opens over the attack time and closes over the release time.
/// - Sidechain (SidechainEffect), sidechain filter and Listen work as in
///   Compressor.
///
/// No latency. Processes up to kMaxChannels channels; others pass through.
class Gate {
public:
    // Parameter ids: saved in projects, so they never change.
    enum Id : std::uint32_t {
        kThreshold,
        kHysteresis,
        kRange,
        kAttack,
        kHold,
        kRelease,
        kSidechainFilter,
        kSidechainFrequency,
        kSidechainListen,
        kParameterCount
    };

    static constexpr int kMaxChannels = 2;
    static constexpr double kMinRangeDb = -80.0;
    /// How fast the detector's peak level falls between peaks.
    static constexpr double kDetectorReleaseSeconds = 0.005;

    [[nodiscard]] static ProcessorSpec spec() noexcept { return {"vloom.gate", "Gate", "1.0"}; }
    [[nodiscard]] static std::span<const ParameterSpec> parameters() noexcept { return kParameters; }

    void prepare(double sampleRate, int maxBlockFrames);
    void setParameter(std::uint32_t id, double value) noexcept;
    void process(float* const* channels, int numChannels, int numFrames) noexcept;
    /// process() with the detector listening to `key` (SidechainEffect).
    void processWithSidechain(float* const* channels, const float* const* key, int numChannels,
        int numKeyChannels, int numFrames) noexcept;
    void reset() noexcept;
    [[nodiscard]] int latencyFrames() const noexcept { return 0; }

    /// The most the gate turned the signal down during the last block, in dB
    /// (0 .. -kMinRangeDb), for a meter. Safe to call from any thread.
    [[nodiscard]] float gainReductionDb() const noexcept { return gainReduction_.load(); }
    /// Whether the gate was open at the end of the last block. Any thread.
    [[nodiscard]] bool isOpen() const noexcept { return open_.load(); }

    // Readouts (HasReadouts): indices into readoutSpecs(), only ever appended.
    enum Readout : std::size_t { kReadoutOpen, kReadoutGainReduction, kReadoutCount };
    [[nodiscard]] static std::span<const ReadoutSpec> readoutSpecs() noexcept { return kReadouts; }
    /// isOpen() (1 or 0) or gainReductionDb(). Any thread.
    [[nodiscard]] float readout(std::size_t index) const noexcept
    {
        switch (index) {
        case kReadoutOpen:
            return isOpen() ? 1.0f : 0.0f;
        case kReadoutGainReduction:
            return gainReductionDb();
        default:
            return 0.0f;
        }
    }

private:
    static constexpr std::array<ReadoutSpec, kReadoutCount> kReadouts{
        ReadoutSpec{"open", "Open", "", 0.0, 1.0, true},
        ReadoutSpec{"gain_reduction_db", "Gain Reduction", "dB", 0.0, -kMinRangeDb, false},
    };

    static constexpr std::array<ParameterSpec, kParameterCount> kParameters{
        ParameterSpec{kThreshold, "Threshold", "dB", -80.0, 0.0, -40.0, false, "Gate"},
        ParameterSpec{kHysteresis, "Hysteresis", "dB", 0.0, 20.0, 4.0, false, "Gate"},
        ParameterSpec{kRange, "Range", "dB", kMinRangeDb, 0.0, kMinRangeDb, false, "Gate"},
        ParameterSpec{kAttack, "Attack", "ms", 0.01, 100.0, 0.5, false, "Envelope"},
        ParameterSpec{kHold, "Hold", "ms", 0.0, 2000.0, 50.0, false, "Envelope"},
        ParameterSpec{kRelease, "Release", "ms", 1.0, 4000.0, 100.0, false, "Envelope"},
        ParameterSpec{kSidechainFilter, "Sidechain Filter", "", 0.0, 1.0, 0.0, true, "Sidechain"},
        ParameterSpec{
            kSidechainFrequency, "Sidechain Frequency", "Hz", 20.0, 2000.0, 100.0, false, "Sidechain"},
        ParameterSpec{kSidechainListen, "Sidechain Listen", "", 0.0, 1.0, 0.0, true, "Sidechain"},
    };

    [[nodiscard]] double value(std::uint32_t id) const noexcept { return values_[id]; }
    void applyValues() noexcept;
    void run(float* const* channels, const float* const* key, int numChannels, int numKeyChannels,
        int numFrames) noexcept;

    std::array<double, kParameterCount> values_ = defaultValues(kParameters);
    double sampleRate_ = 0.0; ///< 0: not prepared, passes through
    double openLevel_ = 0.0; ///< linear
    double closeLevel_ = 0.0;
    double closedGain_ = 0.0;
    double attackFactor_ = 0.0;
    double releaseFactor_ = 0.0;
    double detectorFactor_ = 0.0;
    int holdFrames_ = 0;
    SidechainFilter sidechain_;
    double envelope_ = 0.0; ///< the detector's peak level
    double gain_ = 1.0; ///< what the signal is multiplied by now
    bool gateOpen_ = true;
    int holdLeft_ = 0;
    SharedValue<float> gainReduction_{0.0f};
    SharedValue<bool> open_{false};
};

static_assert(SidechainEffect<Gate>);
static_assert(HasReadouts<Gate>);

} // namespace vloom::dsp
