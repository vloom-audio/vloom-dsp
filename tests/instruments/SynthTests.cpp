// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/instruments/Synth.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <string_view>
#include <vector>

using Catch::Approx;
using vloom::dsp::Synth;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr int kSecond = 48000;

struct Stereo {
    std::vector<float> left;
    std::vector<float> right;
};

/// Renders `frames` in blocks of `block`, into silence.
Stereo render(Synth& synth, int frames, int block = kBlock)
{
    Stereo out{std::vector<float>(static_cast<std::size_t>(frames), 0.0f),
        std::vector<float>(static_cast<std::size_t>(frames), 0.0f)};
    for (int done = 0; done < frames; done += block) {
        std::array<float*, 2> channels{out.left.data() + done, out.right.data() + done};
        synth.process(channels.data(), 2, std::min(block, frames - done));
    }
    return out;
}

double peak(const std::vector<float>& signal)
{
    double most = 0.0;
    for (float sample : signal)
        most = std::max(most, static_cast<double>(std::abs(sample)));
    return most;
}

double rms(const std::vector<float>& signal, std::size_t from = 0)
{
    double sum = 0.0;
    for (std::size_t i = from; i < signal.size(); ++i)
        sum += static_cast<double>(signal[i]) * signal[i];
    return std::sqrt(sum / static_cast<double>(signal.size() - from));
}

/// Cycles per second, counted where the signal crosses zero going up.
double frequencyOf(const std::vector<float>& signal, std::size_t from)
{
    int crossings = 0;
    std::size_t first = 0;
    std::size_t last = 0;
    for (std::size_t i = from + 1; i < signal.size(); ++i) {
        if (signal[i - 1] < 0.0f && signal[i] >= 0.0f) {
            if (crossings == 0)
                first = i;
            last = i;
            ++crossings;
        }
    }
    return crossings > 1 ? (crossings - 1) * kRate / static_cast<double>(last - first) : 0.0;
}

/// How rough a signal is: the level of its sample-to-sample steps.
double brightness(const std::vector<float>& signal)
{
    std::vector<float> steps(signal.size() - 1);
    for (std::size_t i = 1; i < signal.size(); ++i)
        steps[i - 1] = signal[i] - signal[i - 1];
    return rms(steps);
}

/// A plain sine: oscillator A at the start of the basic table, no filter.
Synth sineSynth()
{
    Synth synth;
    synth.prepare(kRate, kBlock);
    synth.setParameter(Synth::kAPosition, 0.0);
    synth.setParameter(Synth::kFilterOn, 0.0);
    return synth;
}

} // namespace

TEST_CASE("The synth describes itself and its parameters", "[synth]")
{
    CHECK(std::string_view(Synth::spec().id) == "vloom.synth");
    const auto parameters = Synth::parameters();
    REQUIRE(parameters.size() == Synth::kParameterCount);
    std::set<std::string_view> names;
    for (std::uint32_t id = 0; id < parameters.size(); ++id) {
        const auto& parameter = parameters[id];
        CHECK(parameter.id == id); // ids are positions: saved projects rely on them
        CHECK(parameter.maxValue > parameter.minValue);
        CHECK(parameter.defaultValue >= parameter.minValue);
        CHECK(parameter.defaultValue <= parameter.maxValue);
        CHECK(names.insert(parameter.name).second); // no two alike
    }
}

TEST_CASE("Without notes, or before prepare, the synth adds nothing", "[synth]")
{
    Synth unprepared;
    unprepared.noteOn(60, 1.0f);
    std::vector<float> left(64, 0.25f);
    std::vector<float> right(64, 0.25f);
    std::array<float*, 2> channels{left.data(), right.data()};
    unprepared.process(channels.data(), 2, 64);
    CHECK(left == std::vector<float>(64, 0.25f));

    Synth synth;
    synth.prepare(kRate, kBlock);
    synth.process(channels.data(), 2, 64);
    CHECK(left == std::vector<float>(64, 0.25f)); // what was there stays: it adds
    CHECK(synth.activeVoices() == 0);
}

TEST_CASE("A key sounds at its pitch", "[synth]")
{
    Synth synth = sineSynth();
    synth.noteOn(69, 1.0f); // A4
    const Stereo a4 = render(synth, kSecond);
    CHECK(frequencyOf(a4.left, 2400) == Approx(440.0).margin(0.5));
    CHECK(peak(a4.left) > 0.05);
    CHECK(peak(a4.left) < 1.0);
    CHECK(a4.left == a4.right); // one copy, in the centre

    synth.reset();
    synth.setParameter(Synth::kAOctave, -1.0);
    synth.setParameter(Synth::kASemi, 7.0);
    synth.noteOn(69, 1.0f);
    // An octave down and a fifth up: E4.
    CHECK(frequencyOf(render(synth, kSecond).left, 2400) == Approx(329.63).margin(0.5));
}

TEST_CASE("A released note rings out and its voice is free again", "[synth]")
{
    Synth synth = sineSynth();
    synth.setParameter(Synth::kEnv1Release, 100.0);
    synth.noteOn(60, 0.8f);
    (void)render(synth, 4800);
    CHECK(synth.activeVoices() == 1);
    synth.noteOff(60);
    const Stereo tail = render(synth, kSecond / 2);
    CHECK(peak(tail.left) > 0.0); // it does not stop dead
    CHECK(synth.activeVoices() == 0);
    CHECK(rms(tail.left, tail.left.size() - 4800) == 0.0); // and then it is silent

    synth.noteOn(60, 0.8f);
    synth.reset();
    CHECK(synth.activeVoices() == 0);
    CHECK(peak(render(synth, 256).left) == 0.0);
}

TEST_CASE("Harder keys are louder; no velocity is a note off", "[synth]")
{
    Synth soft = sineSynth();
    soft.noteOn(60, 0.25f);
    Synth hard = sineSynth();
    hard.noteOn(60, 1.0f);
    CHECK(rms(render(hard, 9600).left) == Approx(4.0 * rms(render(soft, 9600).left)).epsilon(0.01));

    hard.noteOn(60, 0.0f);
    (void)render(hard, kSecond);
    CHECK(hard.activeVoices() == 0);
}

TEST_CASE("More keys than voices: the oldest gives way", "[synth]")
{
    Synth synth = sineSynth();
    synth.noteOn(60, 1.0f);
    synth.noteOn(64, 1.0f);
    synth.noteOn(67, 1.0f);
    (void)render(synth, 256);
    CHECK(synth.activeVoices() == 3);

    synth.reset();
    synth.setParameter(Synth::kVoices, 2.0);
    synth.noteOn(48, 1.0f);
    synth.noteOn(60, 1.0f);
    synth.noteOn(69, 1.0f); // takes the voice of key 48
    (void)render(synth, 256);
    CHECK(synth.activeVoices() == 2);
    synth.noteOff(60);
    (void)render(synth, kSecond);
    CHECK(synth.activeVoices() == 1);
    CHECK(frequencyOf(render(synth, kSecond).left, 0) == Approx(440.0).margin(0.5));

    // Every key at once does no harm.
    for (int key = 0; key < 128; ++key)
        synth.noteOn(key, 1.0f);
    CHECK(peak(render(synth, 2048).left) < 4.0);
    CHECK(synth.activeVoices() == 2);
}

TEST_CASE("Mono plays one key and goes back to the one still held", "[synth]")
{
    Synth synth = sineSynth();
    synth.setParameter(Synth::kMono, 1.0);
    synth.noteOn(57, 1.0f); // A3
    synth.noteOn(69, 1.0f); // A4 over it
    (void)render(synth, 4800);
    CHECK(synth.activeVoices() == 1);
    CHECK(frequencyOf(render(synth, kSecond / 2).left, 0) == Approx(440.0).margin(0.5));
    synth.noteOff(69);
    (void)render(synth, 4800);
    CHECK(frequencyOf(render(synth, kSecond / 2).left, 0) == Approx(220.0).margin(0.5));
    synth.noteOff(57);
    (void)render(synth, kSecond);
    CHECK(synth.activeVoices() == 0);
}

TEST_CASE("Glide slides from the last key to the new one", "[synth]")
{
    Synth synth = sineSynth();
    synth.setParameter(Synth::kMono, 1.0);
    synth.setParameter(Synth::kLegato, 1.0);
    synth.setParameter(Synth::kGlide, 500.0);
    synth.noteOn(57, 1.0f);
    (void)render(synth, 4800);
    synth.noteOn(69, 1.0f);
    const double early = frequencyOf(render(synth, 2400).left, 0);
    CHECK(early > 220.0);
    CHECK(early < 430.0); // on its way
    (void)render(synth, kSecond);
    CHECK(frequencyOf(render(synth, kSecond / 2).left, 0) == Approx(440.0).margin(0.5));
}

TEST_CASE("The filter takes the top off", "[synth]")
{
    const auto bright = [](double cutoff) {
        Synth synth;
        synth.prepare(kRate, kBlock);
        synth.setParameter(Synth::kCutoff, cutoff);
        synth.setParameter(Synth::kResonance, 0.0);
        synth.noteOn(45, 1.0f); // A2, a saw
        return brightness(render(synth, kSecond / 2).left);
    };
    CHECK(bright(300.0) < 0.2 * bright(20000.0));
}

TEST_CASE("Unison spreads the copies left and right", "[synth]")
{
    Synth synth;
    synth.prepare(kRate, kBlock);
    synth.setParameter(Synth::kAUnison, 5.0);
    synth.setParameter(Synth::kADetune, 40.0);
    synth.noteOn(57, 1.0f);
    const Stereo wide = render(synth, kSecond / 2);
    CHECK(wide.left != wide.right);
    CHECK(rms(wide.left) == Approx(rms(wide.right)).epsilon(0.2)); // but not to one side
}

TEST_CASE("An LFO on the level makes a tremolo", "[synth]")
{
    Synth synth = sineSynth();
    synth.setParameter(Synth::kLfo1Target, static_cast<double>(Synth::Target::Level));
    synth.setParameter(Synth::kLfo1Amount, 100.0);
    synth.setParameter(Synth::kLfo1Rate, 4.0);
    synth.setParameter(Synth::kEnv1Sustain, 100.0);
    synth.noteOn(69, 1.0f);
    const Stereo out = render(synth, kSecond);
    // A quarter of a second per cycle: loud a sixteenth in, quiet three sixteenths in.
    const auto level = [&out](std::size_t at) {
        return rms(std::vector<float>(out.left.begin() + static_cast<std::ptrdiff_t>(at),
            out.left.begin() + static_cast<std::ptrdiff_t>(at + 480)));
    };
    CHECK(level(3000 - 240) > 5.0 * level(9000 - 240));
}

TEST_CASE("The output is the same whatever the block size", "[synth]")
{
    const auto play = [](int block) {
        Synth synth;
        synth.prepare(kRate, 512);
        synth.setParameter(Synth::kBOn, 1.0);
        synth.setParameter(Synth::kAUnison, 3.0);
        synth.setParameter(Synth::kCutoff, 1200.0);
        synth.noteOn(52, 0.9f);
        synth.noteOn(59, 0.7f);
        return render(synth, 8192, block);
    };
    const Stereo small = play(64);
    const Stereo large = play(512);
    REQUIRE(small.left.size() == large.left.size());
    for (std::size_t i = 0; i < small.left.size(); i += 37)
        CHECK(small.left[i] == Approx(large.left[i]).margin(1e-6));
}

TEST_CASE("One channel gets both sides; long blocks are done in parts", "[synth]")
{
    Synth synth = sineSynth();
    synth.noteOn(69, 1.0f);
    std::vector<float> mono(4096, 0.0f); // longer than the 256 it was prepared for
    std::array<float*, 1> channel{mono.data()};
    synth.process(channel.data(), 1, 4096);
    CHECK(frequencyOf(mono, 480) == Approx(440.0).margin(1.0));
    CHECK(peak(mono) > 0.05);
}

TEST_CASE("No setting, however extreme, breaks the output", "[synth]")
{
    for (double rate : {44100.0, 192000.0}) {
        for (bool high : {false, true}) {
            Synth synth;
            synth.prepare(rate, kBlock);
            for (const auto& parameter : Synth::parameters())
                synth.setParameter(parameter.id, high ? parameter.maxValue : parameter.minValue);
            // What can make the least sound at the extremes, turned up again.
            synth.setParameter(Synth::kAOn, 1.0);
            synth.setParameter(Synth::kALevel, 100.0);
            synth.setParameter(Synth::kVolume, 0.0);
            synth.setParameter(Synth::kMono, 0.0);
            synth.setParameter(Synth::kVoices, Synth::kMaxVoices);
            synth.setParameter(Synth::kCutoff, std::numeric_limits<double>::quiet_NaN()); // ignored
            synth.setParameter(Synth::kParameterCount, 1.0); // no such parameter
            for (int key : {0, 24, 60, 96, 127, 300, -5})
                synth.noteOn(key, 2.0f); // out of range: clamped
            const Stereo out = render(synth, 16384);
            for (std::size_t i = 0; i < out.left.size(); ++i) {
                REQUIRE(std::isfinite(out.left[i]));
                REQUIRE(std::isfinite(out.right[i]));
            }
            CHECK(peak(out.left) < 16.0);
        }
    }
}

TEST_CASE("A macro moves its target by its amount", "[synth]")
{
    Synth synth = sineSynth();
    synth.setParameter(Synth::kMacro2Target, static_cast<double>(Synth::Target::Pitch));
    synth.setParameter(Synth::kMacro2Amount, 100.0);
    synth.noteOn(69, 1.0f);
    CHECK(
        frequencyOf(render(synth, kSecond / 2).left, 2400) == Approx(440.0).margin(0.5)); // the knob is down
    synth.setParameter(Synth::kMacro2, 100.0); // all the way: an octave
    (void)render(synth, 2400);
    CHECK(frequencyOf(render(synth, kSecond / 2).left, 0) == Approx(880.0).margin(1.0));
    synth.setParameter(Synth::kMacro2, 50.0);
    (void)render(synth, 2400);
    CHECK(
        frequencyOf(render(synth, kSecond / 2).left, 0) == Approx(622.25).margin(1.0)); // half: six semitones
}

TEST_CASE("An LFO waits for its delay and comes in over its rise", "[synth]")
{
    Synth synth = sineSynth();
    synth.setParameter(Synth::kEnv1Sustain, 100.0);
    synth.setParameter(Synth::kLfo3Target, static_cast<double>(Synth::Target::Level));
    synth.setParameter(Synth::kLfo3Amount, 100.0);
    synth.setParameter(Synth::kLfo3Rate, 8.0);
    synth.setParameter(Synth::kLfo3Delay, 500.0);
    synth.setParameter(Synth::kLfo3Rise, 250.0);
    synth.noteOn(69, 1.0f);
    const Stereo out = render(synth, 2 * kSecond);
    // The level over an eighth of a second (one LFO cycle), from `at`: its lowest and highest 10 ms.
    const auto swing = [&out](std::size_t at) {
        double low = 1.0e9;
        double high = 0.0;
        for (std::size_t from = at; from < at + 6000; from += 480) {
            const double level = rms(std::vector<float>(out.left.begin() + static_cast<std::ptrdiff_t>(from),
                out.left.begin() + static_cast<std::ptrdiff_t>(from + 480)));
            low = std::min(low, level);
            high = std::max(high, level);
        }
        return high / std::max(low, 1.0e-9);
    };
    CHECK(swing(4800) == Approx(1.0).margin(0.02)); // still waiting: steady
    const double coming = swing(24000 + 2400); // a little into the rise
    CHECK(coming > 1.05);
    CHECK(swing(60000) > 2.0 * coming); // fully in
}

TEST_CASE("Envelope 3 moves what it is pointed at", "[synth]")
{
    Synth synth = sineSynth();
    synth.setParameter(Synth::kEnv3Target, static_cast<double>(Synth::Target::Pitch));
    synth.setParameter(Synth::kEnv3Amount, 100.0);
    synth.setParameter(Synth::kEnv3Sustain, 100.0); // up an octave and staying there
    synth.noteOn(57, 1.0f);
    (void)render(synth, 4800);
    CHECK(frequencyOf(render(synth, kSecond / 2).left, 0) == Approx(440.0).margin(1.0));
}

TEST_CASE("Parameter ids saved in projects stay where they were", "[synth]")
{
    // The ids before tempo sync and pitch bend were added, unchanged.
    STATIC_CHECK(Synth::kAOn == 0);
    STATIC_CHECK(Synth::kBOn == 11);
    STATIC_CHECK(Synth::kFilterOn == 26);
    STATIC_CHECK(Synth::kEnv1Attack == 37);
    STATIC_CHECK(Synth::kLfo1Rate == 56);
    STATIC_CHECK(Synth::kLfo4Smooth == 87);
    STATIC_CHECK(Synth::kMacro1 == 88);
    STATIC_CHECK(Synth::kMono == 100);
    STATIC_CHECK(Synth::kVolume == 104);
    // The new ones come after them.
    STATIC_CHECK(Synth::kLfo1Sync == 105);
    STATIC_CHECK(Synth::kLfo1Note == 106);
    STATIC_CHECK(Synth::kLfo4Note == 112);
    STATIC_CHECK(Synth::kBendRange == 113);
    STATIC_CHECK(Synth::kModWheelTarget == 114);
    STATIC_CHECK(Synth::kModWheelAmount == 115);
    STATIC_CHECK(Synth::kParameterCount == 116);

    const auto parameters = Synth::parameters();
    CHECK(std::string_view(parameters[Synth::kLfo1Rate].name) == "LFO 1 Rate");
    CHECK(std::string_view(parameters[Synth::kVolume].name) == "Volume");
    CHECK(std::string_view(parameters[Synth::kLfo3Sync].name) == "LFO 3 Sync");
    CHECK(parameters[Synth::kBendRange].defaultValue == 2.0);
    CHECK(parameters[Synth::kLfo1Sync].defaultValue == 0.0); // old projects: free-running, as before
    // A group's parameters follow each other.
    std::vector<std::string_view> groups;
    for (const auto& parameter : parameters) {
        const std::string_view group = parameter.group;
        if (groups.empty() || groups.back() != group) {
            CHECK(std::find(groups.begin(), groups.end(), group) == groups.end());
            groups.push_back(group);
        }
    }
    STATIC_CHECK(vloom::dsp::TempoAware<Synth>);
    STATIC_CHECK(vloom::dsp::PitchBendable<Synth>);
    STATIC_CHECK(vloom::dsp::ModWheelAware<Synth>);
}

TEST_CASE("A synced LFO runs one cycle per note at the tempo", "[synth]")
{
    Synth synth = sineSynth();
    CHECK(synth.tempo() == 120.0);
    CHECK(synth.lfoRateHz(0) == 2.0); // free: its Rate
    synth.setParameter(Synth::kLfo1Sync, 1.0);
    synth.setParameter(Synth::kLfo1Note, vloom::dsp::note::kQuarter);
    CHECK(synth.lfoRateHz(0) == Approx(2.0)); // a quarter at 120 BPM
    synth.setTempo(90.0);
    CHECK(synth.lfoRateHz(0) == Approx(1.5));
    synth.setParameter(Synth::kLfo1Note, vloom::dsp::note::kEighth);
    CHECK(synth.lfoRateHz(0) == Approx(3.0));
    synth.setTempo(std::numeric_limits<double>::quiet_NaN());
    synth.setTempo(-5.0);
    CHECK(synth.tempo() == 90.0); // ignored
    CHECK(synth.lfoRateHz(1) == 5.0); // LFO 2 still free

    // Heard: a tremolo at a quarter note, 60 BPM, is one cycle a second.
    for (const double bpm : {60.0, 120.0}) {
        Synth tremolo = sineSynth();
        tremolo.setTempo(bpm);
        tremolo.setParameter(Synth::kLfo1Target, static_cast<double>(Synth::Target::Level));
        tremolo.setParameter(Synth::kLfo1Amount, 100.0);
        tremolo.setParameter(Synth::kLfo1Rate, 7.3); // not used while synced
        tremolo.setParameter(Synth::kLfo1Sync, 1.0);
        tremolo.setParameter(Synth::kLfo1Note, vloom::dsp::note::kQuarter);
        tremolo.setParameter(Synth::kEnv1Sustain, 100.0);
        tremolo.noteOn(69, 1.0f);
        const Stereo out = render(tremolo, 3 * kSecond);
        const auto level = [&out](double seconds) {
            const auto at = static_cast<std::ptrdiff_t>(seconds * kRate) - 240;
            return rms(std::vector<float>(out.left.begin() + at, out.left.begin() + at + 480));
        };
        const double period = 60.0 / bpm;
        INFO("tempo " << bpm);
        // Loud a quarter of a cycle in, quiet three quarters in, and again a cycle later.
        CHECK(level(0.25 * period) > 5.0 * level(0.75 * period));
        CHECK(level(1.25 * period) > 5.0 * level(1.75 * period));
        CHECK(level(1.25 * period) == Approx(level(0.25 * period)).epsilon(0.02));
        CHECK(level(1.75 * period) == Approx(level(0.75 * period)).margin(0.01));
    }
}

TEST_CASE("Pitch bend moves every voice by up to the bend range", "[synth]")
{
    const auto pitchWith = [](float bend, double range) {
        Synth synth = sineSynth();
        synth.setParameter(Synth::kBendRange, range);
        synth.pitchBend(bend);
        synth.noteOn(69, 1.0f);
        return frequencyOf(render(synth, kSecond / 2).left, 2400);
    };
    CHECK(pitchWith(0.0f, 2.0) == Approx(440.0).margin(0.5));
    CHECK(pitchWith(1.0f, 2.0) == Approx(440.0 * std::pow(2.0, 2.0 / 12.0)).margin(0.5));
    CHECK(pitchWith(-1.0f, 2.0) == Approx(440.0 * std::pow(2.0, -2.0 / 12.0)).margin(0.5));
    CHECK(pitchWith(0.5f, 12.0) == Approx(440.0 * std::pow(2.0, 6.0 / 12.0)).margin(0.5));
    CHECK(pitchWith(-1.0f, 24.0) == Approx(110.0).margin(0.5));
    CHECK(pitchWith(3.0f, 2.0) == Approx(440.0 * std::pow(2.0, 2.0 / 12.0)).margin(0.5)); // clamped
    CHECK(pitchWith(1.0f, 0.0) == Approx(440.0).margin(0.5)); // no range: no bend

    // A held note follows the wheel, and NaN leaves it where it was.
    Synth synth = sineSynth();
    synth.noteOn(69, 1.0f);
    (void)render(synth, 4800);
    synth.pitchBend(1.0f);
    synth.pitchBend(std::numeric_limits<float>::quiet_NaN());
    CHECK(frequencyOf(render(synth, kSecond / 2).left, 0) == Approx(493.88).margin(0.5));
    synth.pitchBend(0.0f);
    CHECK(frequencyOf(render(synth, kSecond / 2).left, 0) == Approx(440.0).margin(0.5));
}

TEST_CASE("The mod wheel moves its target like a macro", "[synth]")
{
    Synth synth = sineSynth();
    synth.setParameter(Synth::kModWheelTarget, static_cast<double>(Synth::Target::Pitch));
    synth.setParameter(Synth::kModWheelAmount, 100.0);
    synth.noteOn(57, 1.0f);
    CHECK(frequencyOf(render(synth, kSecond / 2).left, 0) == Approx(220.0).margin(0.5)); // at rest
    synth.modWheel(1.0f); // up an octave
    CHECK(frequencyOf(render(synth, kSecond / 2).left, 0) == Approx(440.0).margin(1.0));
    synth.modWheel(0.5f);
    CHECK(frequencyOf(render(synth, kSecond / 2).left, 0) == Approx(311.13).margin(0.5));
    synth.modWheel(-3.0f); // clamped to 0
    CHECK(frequencyOf(render(synth, kSecond / 2).left, 0) == Approx(220.0).margin(0.5));
}
