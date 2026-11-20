// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "support/EffectTesting.hpp"
#include "vloom/dsp/SharedValue.hpp"
#include "vloom/dsp/effects/Analyzer.hpp"
#include "vloom/dsp/effects/Compressor.hpp"
#include "vloom/dsp/effects/DeEsser.hpp"
#include "vloom/dsp/effects/Eq.hpp"
#include "vloom/dsp/effects/Gate.hpp"
#include "vloom/dsp/effects/Limiter.hpp"
#include "vloom/dsp/effects/Tuner.hpp"
#include "vloom/dsp/instruments/Synth.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <string_view>

using namespace vloom::dsp;
using namespace vloom::test;

namespace {

/// What every readout list must hold.
template <typename P> void checkReadoutList()
{
    const auto readouts = P::readoutSpecs();
    REQUIRE_FALSE(readouts.empty());
    std::set<std::string> ids;
    for (std::size_t i = 0; i < readouts.size(); ++i) {
        const ReadoutSpec& readout = readouts[i];
        INFO("readout " << i << " " << readout.id);
        CHECK_FALSE(std::string_view(readout.id).empty());
        CHECK_FALSE(std::string_view(readout.name).empty());
        CHECK(readout.minValue < readout.maxValue);
        CHECK(ids.insert(readout.id).second);
        CHECK(findReadout(readouts, readout.id) == static_cast<int>(i));
    }
    const P processor{};
    CHECK(processor.readout(readouts.size()) == 0.0f); // out of range
    CHECK(processor.readout(static_cast<std::size_t>(-1)) == 0.0f);
}

} // namespace

TEST_CASE("Which processors have readouts", "[readouts]")
{
    STATIC_CHECK(HasReadouts<Gate>);
    STATIC_CHECK(HasReadouts<Compressor>);
    STATIC_CHECK(HasReadouts<Limiter>);
    STATIC_CHECK(HasReadouts<DeEsser>);
    STATIC_CHECK(HasReadouts<Tuner>);
    STATIC_CHECK(HasReadouts<Analyzer>);
    STATIC_CHECK_FALSE(HasReadouts<Eq>);
    STATIC_CHECK_FALSE(HasReadouts<Synth>);

    checkReadoutList<Gate>();
    checkReadoutList<Compressor>();
    checkReadoutList<Limiter>();
    checkReadoutList<DeEsser>();
    checkReadoutList<Tuner>();
    checkReadoutList<Analyzer>();
}

TEST_CASE("Dynamics readouts are their meters", "[readouts]")
{
    constexpr double rate = 48000.0;
    const Signal loud = sine(1000.0, 9600, rate, 0.9f);

    Gate gate;
    gate.prepare(rate, 512);
    CHECK(gate.readout(Gate::kReadoutOpen) == 0.0f);
    (void)renderMono(gate, loud);
    CHECK(gate.readout(Gate::kReadoutOpen) == 1.0f);
    CHECK(gate.readout(Gate::kReadoutGainReduction) == gate.gainReductionDb());
    CHECK(findReadout(Gate::readoutSpecs(), "open") == Gate::kReadoutOpen);
    CHECK(findReadout(Gate::readoutSpecs(), "gain_reduction_db") == Gate::kReadoutGainReduction);

    Compressor compressor;
    compressor.prepare(rate, 512);
    compressor.setParameter(Compressor::kThreshold, -30.0);
    (void)renderMono(compressor, loud);
    CHECK(compressor.gainReductionDb() > 1.0f);
    CHECK(compressor.readout(Compressor::kReadoutGainReduction) == compressor.gainReductionDb());

    Limiter limiter;
    limiter.prepare(rate, 512);
    limiter.setParameter(Limiter::kInputGain, 12.0);
    (void)renderMono(limiter, loud);
    CHECK(limiter.gainReductionDb() > 1.0f);
    CHECK(limiter.readout(Limiter::kReadoutGainReduction) == limiter.gainReductionDb());

    DeEsser deEsser;
    deEsser.prepare(rate, 512);
    deEsser.setParameter(DeEsser::kThreshold, -50.0);
    (void)renderMono(deEsser, sine(8000.0, 9600, rate, 0.9f));
    CHECK(deEsser.gainReductionDb() > 1.0f);
    CHECK(deEsser.readout(DeEsser::kReadoutGainReduction) == deEsser.gainReductionDb());

    for (const char* id : {"gain_reduction_db"}) {
        CHECK(findReadout(Compressor::readoutSpecs(), id) == 0);
        CHECK(findReadout(Limiter::readoutSpecs(), id) == 0);
        CHECK(findReadout(DeEsser::readoutSpecs(), id) == 0);
    }
}

TEST_CASE("ReadoutValues store, load and copy", "[readouts]")
{
    ReadoutValues<3> values;
    CHECK(values.load(0) == 0.0f);
    values.store(1, 2.5f);
    values.store(7, 9.0f); // out of range: ignored
    CHECK(values.load(1) == 2.5f);
    CHECK(values.load(7) == 0.0f);
    const ReadoutValues<3> copy = values;
    CHECK(copy.load(1) == 2.5f);
    values.clear();
    CHECK(values.load(1) == 0.0f);
    CHECK(copy.load(1) == 2.5f);
}
