// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "../AllocationCounter.hpp"
#include "../TestSignals.hpp"
#include "vloom/dsp/instruments/Sampler.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <set>
#include <string_view>
#include <thread>
#include <vector>

using Catch::Approx;
using vloom::dsp::SampleBuffer;
using vloom::dsp::SampleInfo;
using vloom::dsp::Sampler;
using vloom::dsp::test::AllocationCounter;
using vloom::dsp::test::frequencyOf;
using vloom::dsp::test::largestStep;
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

/// Prepared at kRate, playing `sample` with a quick attack and full sustain.
std::unique_ptr<Sampler> makeSampler(std::shared_ptr<const SampleBuffer> sample, double rate = kRate)
{
    auto sampler = std::make_unique<Sampler>();
    sampler->prepare(rate, kBlock);
    sampler->setParameter(Sampler::kAttack, 0.0);
    sampler->setSample(0, std::move(sample));
    return sampler;
}

} // namespace

TEST_CASE("The sampler describes itself and its parameters", "[sampler]")
{
    CHECK(std::string_view(Sampler::spec().id) == "vloom.sampler");
    const auto parameters = Sampler::parameters();
    REQUIRE(parameters.size() == Sampler::kParameterCount);
    std::set<std::string_view> names;
    for (std::uint32_t id = 0; id < parameters.size(); ++id) {
        const auto& parameter = parameters[id];
        CHECK(parameter.id == id); // ids are positions: saved projects rely on them
        CHECK(parameter.maxValue > parameter.minValue);
        CHECK(parameter.defaultValue >= parameter.minValue);
        CHECK(parameter.defaultValue <= parameter.maxValue);
        CHECK(names.insert(parameter.name).second); // no two alike
        CHECK(std::string_view(parameter.group) != "");
    }
    // Saved in projects: these never move.
    CHECK(Sampler::kRootKey == 0);
    CHECK(Sampler::kLoopMode == 7);
    CHECK(Sampler::kAttack == 11);
    CHECK(Sampler::kFilterType == 15);
    CHECK(Sampler::kMono == 24);
    CHECK(Sampler::kPan == 29);
    const Sampler sampler;
    CHECK(sampler.sampleSlotCount() == 1);
}

TEST_CASE("Without a sample, or before prepare, the sampler adds nothing", "[sampler]")
{
    std::vector<float> left(64, 0.25f);
    std::vector<float> right(64, 0.25f);
    std::array<float*, 2> channels{left.data(), right.data()};

    Sampler unprepared;
    unprepared.setSample(0, sineSample(440.0, kRate, 0.1));
    unprepared.noteOn(60, 1.0f);
    unprepared.process(channels.data(), 2, 64);
    CHECK(left == std::vector<float>(64, 0.25f));

    Sampler empty;
    empty.prepare(kRate, kBlock);
    empty.noteOn(60, 1.0f);
    empty.process(channels.data(), 2, 64);
    CHECK(left == std::vector<float>(64, 0.25f)); // what was there stays: it adds
    CHECK(empty.activeVoices() == 0);
    CHECK(empty.sampleInfo(0).empty());
    CHECK(empty.sample(0) == nullptr);

    empty.setSample(1, sineSample(440.0, kRate, 0.1)); // no such slot: ignored
    CHECK(empty.sampleInfo(1).empty());
    CHECK(empty.sampleInfo(0).empty());
}

TEST_CASE("The root key plays the sample as it is", "[sampler]")
{
    const auto sample = sineSample(441.0, kRate, 0.5);
    auto sampler = makeSampler(sample);
    CHECK(sampler->sampleInfo(0) == SampleInfo{1, 24000, kRate});
    CHECK(sampler->sample(0) == sample);
    sampler->noteOn(60, 1.0f);
    const Stereo out = render(*sampler, kSecond / 4);
    const auto original = sample->channel(0);
    for (std::size_t i = 48; i < out.left.size(); ++i) { // past the 0.5 ms attack: sample for sample
        REQUIRE(out.left[i] == original[i]);
        REQUIRE(out.right[i] == original[i]); // a mono sample in the centre
    }
}

TEST_CASE("Each key is a semitone; transpose and fine tune move them all", "[sampler]")
{
    auto sampler = makeSampler(sineSample(441.0, kRate, 2.0));
    const auto pitch = [&sampler](int key) {
        sampler->reset();
        sampler->noteOn(key, 1.0f);
        return frequencyOf(render(*sampler, kSecond / 2).left, kRate, 480);
    };
    CHECK(pitch(60) == Approx(441.0).margin(0.2));
    CHECK(pitch(72) == Approx(882.0).margin(0.3)); // an octave up: twice the rate
    CHECK(pitch(48) == Approx(220.5).margin(0.2));
    CHECK(pitch(67) == Approx(441.0 * std::exp2(7.0 / 12.0)).margin(0.3));
    sampler->setParameter(Sampler::kRootKey, 72.0);
    CHECK(pitch(72) == Approx(441.0).margin(0.2));
    sampler->setParameter(Sampler::kTranspose, -12.0);
    CHECK(pitch(72) == Approx(220.5).margin(0.2));
    sampler->setParameter(Sampler::kTranspose, 0.0);
    sampler->setParameter(Sampler::kFineTune, 100.0);
    CHECK(pitch(72) == Approx(441.0 * std::exp2(1.0 / 12.0)).margin(0.3));
}

TEST_CASE("A sample keeps its pitch and length whatever the host's rate", "[sampler]")
{
    // A 48 kHz recording of 480 Hz, half a second, in a 44.1 kHz host.
    constexpr double kHost = 44100.0;
    auto sampler = makeSampler(sineSample(480.0, kRate, 0.5), kHost);
    sampler->noteOn(60, 1.0f);
    const Stereo out = render(*sampler, 22050 + 2048);
    CHECK(frequencyOf(out.left, kHost, 100, 22000) == Approx(480.0).margin(0.1));
    // Read between the samples, it is still the sine.
    double worst = 0.0;
    for (std::size_t i = 100; i < 22000; ++i) {
        const double ideal = 0.5 * std::sin(2.0 * std::numbers::pi * 480.0 * static_cast<double>(i) / kHost);
        worst = std::max(worst, std::abs(out.left[i] - ideal));
    }
    CHECK(worst < 1.0e-4);
    CHECK(peak(out.left, 21900, 22000) > 0.3); // still playing just before the end ...
    CHECK(peak(out.left, 22060) == 0.0); // ... and over half a second later on this clock
    CHECK(sampler->activeVoices() == 0);
}

TEST_CASE("Loop points are checked against the sample", "[sampler]")
{
    const SampleInfo info{1, 1000, 48000.0};
    CHECK(Sampler::loopFrames(info, 25.0, 75.0, 0.0) == Sampler::LoopFrames{250, 750, 0});
    CHECK(Sampler::loopFrames(info, 80.0, 20.0, 0.0) == Sampler::LoopFrames{800, 804, 0}); // the shortest
    CHECK(Sampler::loopFrames(info, 100.0, 100.0, 0.0) == Sampler::LoopFrames{996, 1000, 0});
    CHECK(Sampler::loopFrames(info, -5.0, 300.0, 0.0) == Sampler::LoopFrames{0, 1000, 0});
    // 10 ms is 480 frames; at most half the loop and the audio before it.
    CHECK(Sampler::loopFrames(info, 25.0, 75.0, 10.0).crossfade == 250);
    CHECK(Sampler::loopFrames(info, 40.0, 60.0, 10.0).crossfade == 100);
    CHECK(Sampler::loopFrames(info, 0.0, 60.0, 10.0).crossfade == 0);
    CHECK(Sampler::loopFrames(SampleInfo{}, 25.0, 75.0, 10.0) == Sampler::LoopFrames{});
    CHECK(Sampler::loopFrames(info, std::numeric_limits<double>::quiet_NaN(), 75.0, 0.0)
        == Sampler::LoopFrames{});
    CHECK(Sampler::loopFrames(SampleInfo{1, 2, 48000.0}, 0.0, 100.0, 0.0) == Sampler::LoopFrames{0, 2, 0});

    // A UI can put a loop on any frame of a ten-minute sample.
    const SampleInfo longOne{2, 600 * 48000, 48000.0};
    for (int frame : {0, 1, 12345, 9999999, 28799995}) {
        const double percent = Sampler::percentForFrame(frame, longOne.frames);
        CHECK(Sampler::loopFrames(longOne, percent, 100.0, 0.0).start == frame);
    }
}

TEST_CASE("A forward loop repeats, and its crossfade smooths the seam", "[sampler]")
{
    // 441 Hz: the loop (frames 12000 .. 28944) is not a whole number of cycles.
    const auto play = [](double crossfadeMs) {
        auto sampler = makeSampler(sineSample(441.0, kRate, 1.0));
        sampler->setParameter(Sampler::kLoopMode, static_cast<double>(Sampler::LoopMode::Forward));
        sampler->setParameter(Sampler::kLoopStart, 25.0);
        sampler->setParameter(Sampler::kLoopEnd, 60.3);
        sampler->setParameter(Sampler::kLoopCrossfade, crossfadeMs);
        sampler->noteOn(60, 1.0f);
        return render(*sampler, 3 * kSecond).left;
    };
    const double smooth = 0.5 * 2.0 * std::numbers::pi * 441.0 / kRate; // the sine's own steepest step
    const std::vector<float> hard = play(0.0);
    CHECK(largestStep(hard, 100) > 10.0 * smooth); // the seam jumps
    CHECK(rms(hard, 2 * kSecond) > 0.3); // and it plays on, past the sample's one second

    const std::vector<float> faded = play(20.0);
    CHECK(largestStep(faded, 100) < 1.3 * smooth);
    CHECK(rms(faded, 2 * kSecond) > 0.2);
}

TEST_CASE("A ping-pong loop turns without a jump", "[sampler]")
{
    auto sampler = makeSampler(sineSample(441.0, kRate, 1.0));
    sampler->setParameter(Sampler::kLoopMode, static_cast<double>(Sampler::LoopMode::PingPong));
    sampler->setParameter(Sampler::kLoopStart, 25.0);
    sampler->setParameter(Sampler::kLoopEnd, 60.3);
    sampler->noteOn(60, 1.0f);
    const std::vector<float> out = render(*sampler, 3 * kSecond).left;
    const double smooth = 0.5 * 2.0 * std::numbers::pi * 441.0 / kRate;
    CHECK(largestStep(out, 100) < 1.1 * smooth);
    CHECK(rms(out, 2 * kSecond) > 0.3);
    CHECK(sampler->activeVoices() == 1);
}

TEST_CASE("The amp envelope rises, falls to the sustain level and rings out", "[sampler]")
{
    // A steady level, looped: what comes out is the envelope.
    auto sampler = makeSampler(makeSample(2 * kSecond, kRate, [](int) { return 1.0f; }));
    sampler->setParameter(Sampler::kLoopMode, static_cast<double>(Sampler::LoopMode::Forward));
    sampler->setParameter(Sampler::kAttack, 100.0);
    sampler->setParameter(Sampler::kDecay, 200.0);
    sampler->setParameter(Sampler::kSustain, 50.0);
    sampler->setParameter(Sampler::kRelease, 100.0);
    sampler->noteOn(60, 1.0f);
    const std::vector<float> held = render(*sampler, kSecond).left;
    CHECK(held[2399] == Approx(0.5).margin(0.002)); // half way up, in a straight line
    CHECK(held[4799] == Approx(1.0).margin(0.002)); // the top
    CHECK(held[4800 + 9600]
        == Approx(0.5 + 0.5 * std::exp(-5.0)).margin(0.002)); // the decay's time: -43 dB to go
    CHECK(held[kSecond - 1] == Approx(0.5).margin(0.001));
    sampler->noteOff(60);
    const std::vector<float> released = render(*sampler, kSecond / 2).left;
    CHECK(released[4799] == Approx(0.5 * std::exp(-5.0)).margin(0.0005)); // the release's time
    CHECK(peak(released, 12000) == 0.0);
    CHECK(sampler->activeVoices() == 0);
}

TEST_CASE("Velocity sets the level as far as the sensitivity says", "[sampler]")
{
    const auto level = [](double sensitivity, float velocity) {
        auto sampler = makeSampler(sineSample(441.0, kRate, 0.5));
        sampler->setParameter(Sampler::kVelocity, sensitivity);
        sampler->noteOn(60, velocity);
        return rms(render(*sampler, 9600).left, 480);
    };
    const double full = level(100.0, 1.0f);
    CHECK(level(100.0, 0.25f) == Approx(0.25 * full).epsilon(1e-4));
    CHECK(level(0.0, 0.25f) == Approx(full).epsilon(1e-4));
    CHECK(level(50.0, 0.5f) == Approx(0.75 * full).epsilon(1e-4));

    auto sampler = makeSampler(sineSample(441.0, kRate, 0.5));
    sampler->noteOn(60, 1.0f);
    sampler->noteOn(60, 0.0f); // no velocity: a note off
    (void)render(*sampler, kSecond / 2);
    CHECK(sampler->activeVoices() == 0);
}

TEST_CASE("More keys than voices: the oldest fades out; keys outside the range are ignored", "[sampler]")
{
    auto sampler = makeSampler(sineSample(440.0, kRate, 4.0));
    sampler->setParameter(Sampler::kRootKey, 69.0);
    sampler->setParameter(Sampler::kVoices, 2.0);
    sampler->noteOn(48, 1.0f);
    sampler->noteOn(60, 1.0f);
    sampler->noteOn(69, 1.0f); // takes the place of key 48, which fades out
    (void)render(*sampler, 480);
    CHECK(sampler->activeVoices() == 2);
    sampler->noteOff(60);
    (void)render(*sampler, kSecond / 2);
    CHECK(sampler->activeVoices() == 1);
    CHECK(frequencyOf(render(*sampler, kSecond / 2).left, kRate) == Approx(440.0).margin(0.2));

    for (int key = 0; key < 128; ++key) // every key at once does no harm
        sampler->noteOn(key, 1.0f);
    const Stereo all = render(*sampler, 2048);
    CHECK(peak(all.left) < 8.0);
    CHECK(sampler->activeVoices() == 2);

    sampler->reset();
    sampler->setParameter(Sampler::kKeyLow, 60.0);
    sampler->setParameter(Sampler::kKeyHigh, 72.0);
    sampler->noteOn(59, 1.0f);
    sampler->noteOn(73, 1.0f);
    CHECK(sampler->activeVoices() == 0);
    sampler->noteOn(60, 1.0f);
    sampler->noteOn(72, 1.0f);
    CHECK(sampler->activeVoices() == 2);
}

TEST_CASE("Mono plays one key and goes back to the one still held", "[sampler]")
{
    auto sampler = makeSampler(sineSample(440.0, kRate, 4.0));
    sampler->setParameter(Sampler::kRootKey, 69.0);
    sampler->setParameter(Sampler::kMono, 1.0);
    sampler->noteOn(57, 1.0f); // A3
    sampler->noteOn(69, 1.0f); // A4 over it
    (void)render(*sampler, 480);
    CHECK(sampler->activeVoices() == 1);
    CHECK(frequencyOf(render(*sampler, kSecond / 4).left, kRate) == Approx(440.0).margin(0.3));
    sampler->noteOff(69);
    (void)render(*sampler, 480);
    CHECK(frequencyOf(render(*sampler, kSecond / 4).left, kRate) == Approx(220.0).margin(0.3));
    sampler->noteOff(57);
    (void)render(*sampler, kSecond / 2);
    CHECK(sampler->activeVoices() == 0);
}

TEST_CASE("Glide slides from the last key to the new one", "[sampler]")
{
    auto sampler = makeSampler(sineSample(440.0, kRate, 8.0));
    sampler->setParameter(Sampler::kRootKey, 69.0);
    sampler->setParameter(Sampler::kMono, 1.0);
    sampler->setParameter(Sampler::kLegato, 1.0);
    sampler->setParameter(Sampler::kGlide, 500.0);
    sampler->noteOn(57, 1.0f);
    (void)render(*sampler, 4800);
    sampler->noteOn(69, 1.0f);
    CHECK(sampler->activeVoices() == 1); // legato: the same note moves on
    const double early = frequencyOf(render(*sampler, 2400).left, kRate);
    CHECK(early > 225.0);
    CHECK(early < 430.0); // on its way
    (void)render(*sampler, kSecond);
    CHECK(frequencyOf(render(*sampler, kSecond / 4).left, kRate) == Approx(440.0).margin(0.3));
}

TEST_CASE("The filter takes the top off, and its envelope opens it", "[sampler]")
{
    std::uint32_t state = 12345;
    const auto noise = makeSample(kSecond, kRate, [&state](int) {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 16777216.0f - 0.5f;
    });
    const auto brightness = [&noise](double cutoff, double envelope) {
        auto sampler = makeSampler(noise);
        sampler->setParameter(Sampler::kFilterType, static_cast<double>(Sampler::FilterType::LowPass));
        sampler->setParameter(Sampler::kCutoff, cutoff);
        sampler->setParameter(Sampler::kFilterEnvAmount, envelope);
        sampler->setParameter(Sampler::kFilterSustain, 100.0);
        sampler->noteOn(60, 1.0f);
        const std::vector<float> out = render(*sampler, kSecond / 2).left;
        std::vector<float> steps(out.size() - 1);
        for (std::size_t i = 1; i < out.size(); ++i)
            steps[i - 1] = out[i] - out[i - 1];
        return rms(steps, 4800);
    };
    const double open = brightness(20000.0, 0.0);
    const double closed = brightness(300.0, 0.0);
    CHECK(closed < 0.2 * open);
    CHECK(brightness(300.0, 100.0) > 3.0 * closed); // six octaves up
}

TEST_CASE("A stereo sample keeps its sides; pan and gain move them", "[sampler]")
{
    const auto stereo = makeSample(
        kSecond, kRate, [](int i) { return 0.5f * static_cast<float>(std::sin(0.05 * i)); },
        [](int) { return 0.0f; });
    auto sampler = makeSampler(stereo);
    CHECK(sampler->sampleInfo(0).numChannels == 2);
    sampler->noteOn(60, 1.0f);
    Stereo out = render(*sampler, 4800);
    CHECK(peak(out.left) > 0.45);
    CHECK(peak(out.right) == 0.0);

    sampler->reset();
    sampler->setParameter(Sampler::kPan, 100.0); // all the way right: the left side goes
    sampler->setParameter(Sampler::kGain, 6.0);
    sampler->noteOn(60, 1.0f);
    out = render(*sampler, 4800);
    CHECK(peak(out.left, 2400) < 1e-6);

    sampler->reset();
    sampler->setParameter(Sampler::kPan, 0.0);
    sampler->setParameter(Sampler::kGain, -60.0); // silent
    sampler->noteOn(60, 1.0f);
    CHECK(peak(render(*sampler, 4800).left, 2400) == 0.0);
}

TEST_CASE("One channel gets both sides; long blocks are done in parts", "[sampler]")
{
    auto sampler = makeSampler(sineSample(441.0, kRate, 1.0));
    sampler->noteOn(60, 1.0f);
    std::vector<float> mono(4096, 0.0f); // longer than the 256 it was prepared for
    std::array<float*, 1> channel{mono.data()};
    sampler->process(channel.data(), 1, 4096);
    CHECK(frequencyOf(mono, kRate, 100) == Approx(441.0).margin(1.0));
    CHECK(peak(mono) == Approx(0.5).margin(0.01));
}

TEST_CASE("The sampler's output is the same whatever the block size", "[sampler]")
{
    const auto sample = sineSample(441.0, kRate, 1.0);
    const auto play = [&sample](int block) {
        Sampler sampler;
        sampler.prepare(kRate, 512);
        sampler.setSample(0, sample);
        sampler.setParameter(Sampler::kFilterType, 1.0);
        sampler.setParameter(Sampler::kCutoff, 1200.0);
        sampler.setParameter(Sampler::kGlide, 100.0);
        sampler.noteOn(52, 0.9f);
        sampler.noteOn(59, 0.7f);
        return render(sampler, 8192, block);
    };
    const Stereo small = play(64);
    const Stereo large = play(512);
    for (std::size_t i = 0; i < small.left.size(); i += 37)
        CHECK(small.left[i] == Approx(large.left[i]).margin(1e-6));
}

TEST_CASE("reset() silences the sampler", "[sampler]")
{
    auto sampler = makeSampler(sineSample(441.0, kRate, 1.0));
    sampler->setParameter(Sampler::kRelease, 2000.0);
    sampler->noteOn(60, 1.0f);
    sampler->noteOn(64, 1.0f);
    (void)render(*sampler, 2048);
    sampler->noteOff(60);
    sampler->reset();
    CHECK(sampler->activeVoices() == 0);
    CHECK(peak(render(*sampler, 1024).left) == 0.0);
}

TEST_CASE("Swapping the sample while notes play neither allocates nor frees in process()", "[sampler]")
{
    auto first = sineSample(441.0, kRate, 2.0);
    const std::weak_ptr<const SampleBuffer> firstAlive = first;
    auto sampler = makeSampler(std::move(first));
    sampler->noteOn(60, 1.0f);
    (void)render(*sampler, 4800);

    sampler->setSample(0, sineSample(882.0, kRate, 2.0)); // host side
    CHECK_FALSE(firstAlive.expired()); // the note still plays it

    std::vector<float> left(kBlock);
    std::vector<float> right(kBlock);
    std::array<float*, 2> channels{left.data(), right.data()};
    {
        AllocationCounter counter;
        for (int block = 0; block < 40; ++block) { // the old note fades out, the new sample plays
            if (block == 5)
                sampler->noteOn(60, 1.0f);
            sampler->process(channels.data(), 2, kBlock);
        }
        sampler->noteOff(60);
        sampler->reset();
        CHECK(counter.allocations() == 0);
        CHECK(counter.deallocations() == 0);
    }
    CHECK_FALSE(firstAlive.expired()); // let go of, but not freed on the audio thread
    sampler->collectGarbage(); // host side
    CHECK(firstAlive.expired());

    sampler->noteOn(60, 1.0f);
    CHECK(frequencyOf(render(*sampler, kSecond / 4).left, kRate, 480) == Approx(882.0).margin(0.3));
    sampler->setSample(0, nullptr); // emptied: notes fade, new ones are silent
    (void)render(*sampler, 1024);
    sampler->noteOn(64, 1.0f);
    CHECK(sampler->activeVoices() == 0);
    CHECK(peak(render(*sampler, 1024).left) == 0.0);
}

TEST_CASE("A host swapping samples while the audio thread plays does no harm", "[sampler]")
{
    const std::array<std::shared_ptr<const SampleBuffer>, 3> samples{sineSample(220.0, kRate, 0.2),
        makeSample(
            4800, 22050.0, [](int i) { return (i % 100) < 50 ? 0.3f : -0.3f; },
            [](int i) { return (i % 60) < 30 ? 0.2f : -0.2f; }),
        sineSample(1000.0, 96000.0, 0.05)};
    Sampler sampler;
    sampler.prepare(kRate, kBlock);
    sampler.setParameter(Sampler::kLoopMode, 1.0);
    sampler.setParameter(Sampler::kLoopStart, 30.0);
    sampler.setParameter(Sampler::kLoopCrossfade, 5.0);
    std::atomic<bool> done{false};
    std::thread host([&] {
        for (int i = 0; !done.load(); ++i) {
            sampler.setSample(0, i % 4 == 3 ? nullptr : samples[static_cast<std::size_t>(i % 3)]);
            sampler.collectGarbage();
            (void)sampler.sampleInfo(0);
            std::this_thread::yield();
        }
    });
    std::vector<float> left(kBlock);
    std::vector<float> right(kBlock);
    std::array<float*, 2> channels{left.data(), right.data()};
    bool finite = true;
    for (int block = 0; block < 3000; ++block) {
        if (block % 7 == 0)
            sampler.noteOn(48 + block % 24, 1.0f);
        if (block % 11 == 0)
            sampler.noteOff(48 + (block + 5) % 24);
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        sampler.process(channels.data(), 2, kBlock);
        for (int i = 0; i < kBlock; ++i)
            finite = finite && std::isfinite(left[static_cast<std::size_t>(i)])
                && std::abs(left[static_cast<std::size_t>(i)]) < 16.0f;
    }
    done.store(true);
    host.join();
    CHECK(finite);
}

TEST_CASE("No setting, however extreme, breaks the sampler", "[sampler]")
{
    const auto stereo = makeSample(
        2000, 8000.0, [](int i) { return (i % 9) < 4 ? 1.0f : -1.0f; },
        [](int i) { return static_cast<float>(std::sin(0.3 * i)); });
    for (double rate : {44100.0, 192000.0}) {
        for (bool high : {false, true}) {
            Sampler sampler;
            sampler.prepare(rate, kBlock);
            sampler.setSample(0, stereo);
            for (const auto& parameter : Sampler::parameters())
                sampler.setParameter(parameter.id, high ? parameter.maxValue : parameter.minValue);
            // What can make the least sound at the extremes, turned up again.
            sampler.setParameter(Sampler::kGain, 0.0);
            sampler.setParameter(Sampler::kKeyLow, 0.0);
            sampler.setParameter(Sampler::kKeyHigh, 127.0);
            sampler.setParameter(Sampler::kMono, 0.0);
            sampler.setParameter(Sampler::kCutoff, std::numeric_limits<double>::quiet_NaN()); // ignored
            sampler.setParameter(Sampler::kParameterCount, 1.0); // no such parameter
            for (int key : {0, 24, 60, 96, 127, 300, -5})
                sampler.noteOn(key, 2.0f); // out of range: clamped
            const Stereo out = render(sampler, 16384);
            for (std::size_t i = 0; i < out.left.size(); ++i) {
                REQUIRE(std::isfinite(out.left[i]));
                REQUIRE(std::isfinite(out.right[i]));
            }
            CHECK(peak(out.left) < 64.0);
        }
    }
}
