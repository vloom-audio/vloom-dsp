// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "../TestSignals.hpp"
#include "../support/AllocationCounter.hpp"
#include "vloom/dsp/instruments/DrumSampler.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using Catch::Approx;
using vloom::dsp::DrumSampler;
using vloom::dsp::SampleBuffer;
using vloom::dsp::SampleInfo;
using vloom::dsp::test::AllocationCounter;
using vloom::dsp::test::frequencyOf;
using vloom::dsp::test::makeSample;
using vloom::dsp::test::peak;
using vloom::dsp::test::render;
using vloom::dsp::test::rms;
using vloom::dsp::test::sineSample;
using vloom::dsp::test::Stereo;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr int kSecond = 48000;

using enum DrumSampler::PadField;

std::unique_ptr<DrumSampler> makeDrums()
{
    auto drums = std::make_unique<DrumSampler>();
    drums->prepare(kRate, kBlock);
    return drums;
}

std::shared_ptr<const SampleBuffer> constant(float value, int frames = kSecond)
{
    return makeSample(frames, kRate, [value](int) { return value; });
}

} // namespace

TEST_CASE("The drum sampler describes itself and its parameters", "[drums]")
{
    CHECK(std::string_view(DrumSampler::spec().id) == "vloom.drums");
    const auto parameters = DrumSampler::parameters();
    REQUIRE(parameters.size() == DrumSampler::kParameterCount);
    REQUIRE(parameters.size() == 1 + 16 * 12);
    std::set<std::uint32_t> ids;
    std::set<std::string_view> names;
    for (const auto& parameter : parameters) {
        CHECK(ids.insert(parameter.id).second);
        CHECK(names.insert(parameter.name).second);
        CHECK(parameter.maxValue > parameter.minValue);
        CHECK(parameter.defaultValue >= parameter.minValue);
        CHECK(parameter.defaultValue <= parameter.maxValue);
    }
    // Saved in projects: these never move.
    CHECK(parameters[0].id == DrumSampler::kMasterGain);
    CHECK(DrumSampler::kMasterGain == 0);
    CHECK(DrumSampler::padParameter(0, kKey) == 16);
    CHECK(DrumSampler::padParameter(3, kTune) == 65);
    CHECK(DrumSampler::padParameter(15, kVelocity) == 16 + 15 * 16 + 11);
    for (int pad = 0; pad < DrumSampler::kPads; ++pad) {
        const auto* key = vloom::dsp::findParameter(parameters, DrumSampler::padParameter(pad, kKey));
        REQUIRE(key != nullptr);
        CHECK(key->defaultValue == 36 + pad); // C1 upward
        CHECK(std::string(key->group) == "Pad " + std::to_string(pad + 1));
        CHECK(std::string(key->name) == "Pad " + std::to_string(pad + 1) + " Key");
        const auto* choke = vloom::dsp::findParameter(parameters, DrumSampler::padParameter(pad, kChoke));
        REQUIRE(choke != nullptr);
        CHECK(choke->maxValue == 8);
        CHECK(choke->stepped);
    }
    const DrumSampler drums;
    CHECK(drums.sampleSlotCount() == 16);
}

TEST_CASE("Pads without samples, and keys without pads, are silent", "[drums]")
{
    auto drums = makeDrums();
    drums->setSample(1, constant(0.5f));
    drums->noteOn(36, 1.0f); // pad 1 has no sample
    drums->noteOn(20, 1.0f); // no pad
    CHECK(drums->activeVoices() == 0);
    CHECK(peak(render(*drums, 1024).left) == 0.0);
    CHECK(drums->sampleInfo(0).empty());
    CHECK(drums->sampleInfo(1) == SampleInfo{1, kSecond, kRate});
    CHECK(drums->sampleInfo(16).empty()); // no such slot
    drums->setSample(-1, constant(0.5f)); // ignored
    CHECK(drums->sample(-1) == nullptr);
}

TEST_CASE("A pad plays its sample on its key, at its own pitch", "[drums]")
{
    auto drums = makeDrums();
    const auto kick = sineSample(441.0, kRate, 0.5);
    drums->setSample(0, kick);
    drums->noteOn(36, 1.0f);
    const Stereo out = render(*drums, kSecond);
    const auto original = kick->channel(0);
    for (std::size_t i = 48; i < 24000; ++i)
        REQUIRE(out.left[i] == original[i]); // past the 0.5 ms attack: sample for sample
    CHECK(peak(out.left, 24001) == 0.0); // it ends with its sample
    CHECK(drums->activeVoices() == 0);

    // Moved to another key.
    drums->setParameter(DrumSampler::padParameter(0, kKey), 60.0);
    drums->noteOn(36, 1.0f);
    CHECK(drums->activeVoices() == 0);
    drums->noteOn(60, 1.0f);
    CHECK(drums->activeVoices() == 1);
}

TEST_CASE("Each pad has its own tune, and plays at its own speed at any host rate", "[drums]")
{
    auto drums = makeDrums();
    for (int pad = 0; pad < 3; ++pad)
        drums->setSample(pad, sineSample(441.0, kRate, 1.0));
    drums->setParameter(DrumSampler::padParameter(1, kTune), 12.0);
    drums->setParameter(DrumSampler::padParameter(2, kTune), -12.0);
    drums->setParameter(DrumSampler::padParameter(2, kFine), 50.0);
    const auto pitch = [&drums](int key) {
        drums->reset();
        drums->noteOn(key, 1.0f);
        return frequencyOf(render(*drums, kSecond / 4).left, kRate, 480, 11000);
    };
    CHECK(pitch(36) == Approx(441.0).margin(0.3));
    CHECK(pitch(37) == Approx(882.0).margin(0.3)); // an octave up: twice the rate
    CHECK(pitch(38) == Approx(220.5 * std::exp2(0.5 / 12.0)).margin(0.3));

    DrumSampler slow;
    slow.prepare(44100.0, kBlock);
    slow.setSample(0, sineSample(480.0, kRate, 0.5));
    slow.noteOn(36, 1.0f);
    const Stereo out = render(slow, 24000);
    CHECK(frequencyOf(out.left, 44100.0, 100, 22000) == Approx(480.0).margin(0.1));
    CHECK(peak(out.left, 21900, 22000) > 0.3);
    CHECK(peak(out.left, 22060) == 0.0);
}

TEST_CASE("A choke group cuts the other pad within a few milliseconds", "[drums]")
{
    auto drums = makeDrums();
    drums->setSample(0, sineSample(441.0, kRate, 2.0)); // open hi-hat
    drums->setSample(1, constant(0.0f)); // closed hi-hat (silent here, to hear the cut)
    drums->setSample(2, sineSample(441.0, kRate, 2.0)); // another pad, no group
    drums->setParameter(DrumSampler::padParameter(0, kChoke), 1.0);
    drums->setParameter(DrumSampler::padParameter(1, kChoke), 1.0);
    drums->noteOn(36, 1.0f);
    (void)render(*drums, 4800);
    drums->noteOn(37, 1.0f);
    const Stereo cut = render(*drums, 4800);
    CHECK(peak(cut.left, 0, 48) > 0.1); // it fades ...
    CHECK(peak(cut.left, 150) == 0.0); // ... and is gone within 3 ms
    CHECK(drums->activeVoices() == 1); // only the closed one

    // Group 0 chokes nothing; another group does not either.
    drums->reset();
    drums->setParameter(DrumSampler::padParameter(1, kChoke), 2.0);
    drums->noteOn(36, 1.0f);
    drums->noteOn(38, 1.0f);
    drums->noteOn(37, 1.0f);
    (void)render(*drums, 4800);
    CHECK(drums->activeVoices() == 3);

    // A pad in a group chokes its own earlier hit.
    drums->reset();
    drums->noteOn(36, 1.0f);
    (void)render(*drums, 480);
    drums->noteOn(36, 1.0f);
    (void)render(*drums, 480);
    CHECK(drums->activeVoices() == 1);
}

TEST_CASE("One-shot pads ignore the key going up; gate pads release", "[drums]")
{
    auto drums = makeDrums();
    drums->setSample(0, constant(0.5f));
    drums->setSample(1, constant(0.5f));
    drums->setParameter(DrumSampler::padParameter(1, kMode), static_cast<double>(DrumSampler::Mode::Gate));
    drums->setParameter(DrumSampler::padParameter(1, kRelease), 50.0);
    drums->noteOn(36, 1.0f);
    drums->noteOn(37, 1.0f);
    (void)render(*drums, 4800);
    drums->noteOff(36);
    drums->noteOff(37);
    const Stereo out = render(*drums, kSecond / 2);
    CHECK(drums->activeVoices() == 1); // the one-shot plays on
    CHECK(out.left[out.left.size() - 1] == Approx(0.5f)); // at full level, the gate pad gone
    CHECK(out.left[2400] == Approx(0.5 + 0.5 * std::exp(-5.0)).margin(0.002)); // 50 ms into the release
}

TEST_CASE("Decay shortens a pad; at its longest it holds", "[drums]")
{
    auto drums = makeDrums();
    drums->setSample(0, constant(1.0f));
    drums->setParameter(DrumSampler::padParameter(0, kDecay), 100.0);
    drums->noteOn(36, 1.0f);
    const Stereo out = render(*drums, kSecond / 2);
    CHECK(out.left[24 + 4800] == Approx(std::exp(-5.0)).margin(0.001)); // down 43 dB in 100 ms
    CHECK(peak(out.left, 12000) == 0.0);
    CHECK(drums->activeVoices() == 0);

    drums->setParameter(DrumSampler::padParameter(0, kDecay), DrumSampler::kNoDecayMs);
    drums->noteOn(36, 1.0f);
    CHECK(render(*drums, kSecond / 2).left[20000] == 1.0f);
}

TEST_CASE("Reverse plays a pad from its end; start skips into it", "[drums]")
{
    auto drums = makeDrums();
    drums->setSample(0, makeSample(1000, kRate, [](int i) { return static_cast<float>(i) / 1000.0f; }));
    drums->setParameter(DrumSampler::padParameter(0, kReverse), 1.0);
    drums->noteOn(36, 1.0f);
    std::vector<float> out = render(*drums, 1200).left;
    CHECK(out[100] == Approx(0.899f)); // frame 999 - 100
    CHECK(out[500] == Approx(0.499f));
    CHECK(out[998] == Approx(0.001f));
    CHECK(peak(out, 1000) == 0.0);

    drums->setParameter(DrumSampler::padParameter(0, kStart), 50.0);
    drums->noteOn(36, 1.0f);
    out = render(*drums, 600).left;
    CHECK(out[100] == Approx(0.399f)); // from frame 499 down
    CHECK(peak(out, 500) == 0.0);

    drums->setParameter(DrumSampler::padParameter(0, kReverse), 0.0);
    drums->noteOn(36, 1.0f);
    out = render(*drums, 600).left;
    CHECK(out[100] == Approx(0.6f)); // from frame 500 up
    CHECK(peak(out, 500) == 0.0);
}

TEST_CASE("Velocity, gain, pan and the master gain set a pad's level", "[drums]")
{
    auto drums = makeDrums();
    drums->setSample(0, constant(0.5f));
    drums->setParameter(DrumSampler::padParameter(0, kPan), -100.0);
    drums->setParameter(DrumSampler::padParameter(0, kGain), 6.0);
    drums->noteOn(36, 0.5f);
    Stereo out = render(*drums, 4800);
    CHECK(out.left[2400] == Approx(0.5 * 0.5 * std::pow(10.0, 6.0 / 20.0)).epsilon(1e-5));
    CHECK(out.right[2400] == 0.0f);

    drums->reset();
    drums->setParameter(DrumSampler::padParameter(0, kVelocity), 0.0);
    drums->setParameter(DrumSampler::kMasterGain, -6.0);
    drums->noteOn(36, 0.5f);
    out = render(*drums, 4800);
    CHECK(out.left[2400]
        == Approx(0.5 * std::pow(10.0, 6.0 / 20.0) * std::pow(10.0, -6.0 / 20.0)).epsilon(1e-5));

    drums->setParameter(DrumSampler::kMasterGain, -60.0); // silent
    (void)render(*drums, 4800);
    CHECK(peak(render(*drums, 4800).left) == 0.0);
}

TEST_CASE("More hits than voices: the oldest fades out", "[drums]")
{
    auto drums = makeDrums();
    drums->setSample(0, constant(0.01f, 4 * kSecond));
    for (int hit = 0; hit < 100; ++hit)
        drums->noteOn(36, 1.0f);
    (void)render(*drums, 480);
    CHECK(drums->activeVoices() == DrumSampler::kMaxVoices);
}

TEST_CASE("reset() silences the drums", "[drums]")
{
    auto drums = makeDrums();
    drums->setSample(0, constant(0.5f));
    drums->noteOn(36, 1.0f);
    (void)render(*drums, 1024);
    drums->reset();
    CHECK(drums->activeVoices() == 0);
    CHECK(peak(render(*drums, 1024).left) == 0.0);
}

TEST_CASE("Swapping a pad's sample while it plays neither allocates nor frees in process()", "[drums]")
{
    auto drums = makeDrums();
    auto first = constant(0.5f);
    const std::weak_ptr<const SampleBuffer> firstAlive = first;
    drums->setSample(3, std::move(first));
    drums->setSample(4, drums->sample(3)); // the same buffer on another pad
    drums->noteOn(39, 1.0f);
    drums->noteOn(40, 1.0f);
    (void)render(*drums, 1024);
    drums->setSample(3, constant(0.25f));

    std::vector<float> left(kBlock);
    std::vector<float> right(kBlock);
    std::array<float*, 2> channels{left.data(), right.data()};
    {
        AllocationCounter counter;
        for (int block = 0; block < 20; ++block) {
            std::fill(left.begin(), left.end(), 0.0f);
            drums->process(channels.data(), 2, kBlock);
        }
        CHECK(counter.allocations() == 0);
        CHECK(counter.deallocations() == 0);
    }
    CHECK(drums->activeVoices() == 1); // pad 3's hit faded; pad 4's, on the same buffer, plays on
    CHECK(left[kBlock - 1] == Approx(0.5f));
    drums->collectGarbage();
    CHECK_FALSE(firstAlive.expired()); // pad 4 still has it
    drums->setSample(4, nullptr);
    {
        AllocationCounter counter;
        for (int block = 0; block < 20; ++block)
            drums->process(channels.data(), 2, kBlock);
        CHECK(counter.deallocations() == 0);
    }
    drums->collectGarbage();
    CHECK(firstAlive.expired());
}

TEST_CASE("No setting, however extreme, breaks the drums", "[drums]")
{
    const auto stereo = makeSample(
        2000, 8000.0, [](int i) { return (i % 9) < 4 ? 1.0f : -1.0f; },
        [](int i) { return static_cast<float>(std::sin(0.3 * i)); });
    for (double rate : {44100.0, 192000.0}) {
        for (bool high : {false, true}) {
            DrumSampler drums;
            drums.prepare(rate, kBlock);
            for (int pad = 0; pad < DrumSampler::kPads; ++pad)
                drums.setSample(pad, stereo);
            for (const auto& parameter : DrumSampler::parameters())
                drums.setParameter(parameter.id, high ? parameter.maxValue : parameter.minValue);
            drums.setParameter(DrumSampler::kMasterGain, 0.0);
            drums.setParameter(DrumSampler::padParameter(0, kTune), std::numeric_limits<double>::quiet_NaN());
            drums.setParameter(5, 1.0); // between the master and the pads: nothing
            drums.setParameter(DrumSampler::padParameter(16, kKey), 1.0); // no such pad
            for (int key : {0, 36, 60, 127, 300, -5})
                drums.noteOn(key, 2.0f);
            const Stereo out = render(drums, 8192);
            for (std::size_t i = 0; i < out.left.size(); ++i) {
                REQUIRE(std::isfinite(out.left[i]));
                REQUIRE(std::isfinite(out.right[i]));
            }
            CHECK(peak(out.left) < 128.0);
            std::vector<float> mono(512, 0.0f);
            std::array<float*, 1> channel{mono.data()};
            drums.process(channel.data(), 1, 512);
            CHECK(std::isfinite(rms(mono)));
        }
    }
}
