// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "AllocationCounter.hpp"
#include "TestSignals.hpp"
#include "vloom/dsp/Sample.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

using Catch::Approx;
using vloom::dsp::hermite;
using vloom::dsp::SampleBuffer;
using vloom::dsp::SampleInfo;
using vloom::dsp::SampleSlot;
using vloom::dsp::test::AllocationCounter;

TEST_CASE("A sample buffer is a clean copy of what the host decoded", "[sample]")
{
    std::vector<float> left{0.5f, std::numeric_limits<float>::quiet_NaN(), -0.25f};
    std::vector<float> right{1.0f, 2.0f, std::numeric_limits<float>::infinity()};
    std::array<const float*, 2> channels{left.data(), right.data()};
    const auto buffer = SampleBuffer::copy(channels.data(), 2, 3, 44100.0);
    REQUIRE(buffer != nullptr);
    CHECK(buffer->info() == SampleInfo{2, 3, 44100.0});
    CHECK(buffer->channel(0)[0] == 0.5f);
    CHECK(buffer->channel(0)[1] == 0.0f); // NaN dropped
    CHECK(buffer->channel(1)[2] == 0.0f); // and infinity
    CHECK(buffer->at(1, 1) == 2.0f);
    CHECK(buffer->at(0, -1) == 0.0f); // outside: silence
    CHECK(buffer->at(0, 3) == 0.0f);
    left[0] = 9.0f;
    CHECK(buffer->at(0, 0) == 0.5f); // a copy

    CHECK(SampleBuffer::copy(nullptr, 1, 3, 44100.0) == nullptr);
    CHECK(SampleBuffer::copy(channels.data(), 0, 3, 44100.0) == nullptr);
    CHECK(SampleBuffer::copy(channels.data(), 3, 3, 44100.0) == nullptr); // the host mixes down
    CHECK(SampleBuffer::copy(channels.data(), 1, 0, 44100.0) == nullptr);
    CHECK(SampleBuffer::copy(channels.data(), 1, 3, 0.0) == nullptr);
    CHECK(SampleBuffer::copy(channels.data(), 1, 3, std::numeric_limits<double>::quiet_NaN()) == nullptr);
    std::array<const float*, 2> missing{left.data(), nullptr};
    CHECK(SampleBuffer::copy(missing.data(), 2, 3, 44100.0) == nullptr);
}

TEST_CASE("Hermite interpolation passes through the points and follows a line", "[sample]")
{
    CHECK(hermite(3.0f, 1.0f, -2.0f, 5.0f, 0.0f) == 1.0f);
    CHECK(hermite(3.0f, 1.0f, -2.0f, 5.0f, 1.0f) == Approx(-2.0f));
    for (float t : {0.1f, 0.5f, 0.9f})
        CHECK(hermite(0.0f, 1.0f, 2.0f, 3.0f, t) == Approx(1.0f + t));
    // A sine read between its samples comes out close to the sine.
    const double w = 0.2;
    double worst = 0.0;
    for (int i = 1; i < 30; ++i) {
        const auto y = [w](int n) { return static_cast<float>(std::sin(w * n)); };
        const float between = hermite(y(i - 1), y(i), y(i + 1), y(i + 2), 0.5f);
        worst = std::max(worst, std::abs(between - std::sin(w * (i + 0.5))));
    }
    CHECK(worst < 1.0e-3);
}

TEST_CASE("A slot hands a sample to the audio side and frees the old one only when let go", "[sample]")
{
    SampleSlot slot;
    CHECK(slot.info().empty());
    auto first = vloom::dsp::test::sineSample(100.0, 48000.0, 0.1);
    const std::weak_ptr<const SampleBuffer> firstAlive = first;
    slot.set(first);
    first.reset(); // the slot holds the only copy now
    CHECK(slot.info() == SampleInfo{1, 4800, 48000.0});
    CHECK(slot.active() == nullptr); // the audio side has not looked yet

    auto update = slot.update();
    CHECK(update.changed);
    CHECK(update.fading == nullptr);
    CHECK(slot.active() == slot.buffer().get());
    CHECK_FALSE(slot.update().changed); // nothing new

    slot.set(vloom::dsp::test::sineSample(200.0, 48000.0, 0.1));
    CHECK(slot.pendingRelease() == 1);
    CHECK_FALSE(firstAlive.expired()); // the audio side still reads it
    update = slot.update();
    CHECK(update.changed);
    CHECK(update.fading == firstAlive.lock().get());
    slot.collectGarbage();
    CHECK_FALSE(firstAlive.expired()); // notes still fade out on it
    slot.fadeDone();
    slot.collectGarbage();
    CHECK(firstAlive.expired());
    CHECK(slot.pendingRelease() == 0);

    // Replaced before the audio side ever saw it: freed at once.
    auto unseen = vloom::dsp::test::sineSample(300.0, 48000.0, 0.1);
    const std::weak_ptr<const SampleBuffer> unseenAlive = unseen;
    slot.set(std::move(unseen));
    slot.set(nullptr);
    CHECK(unseenAlive.expired());
    CHECK(slot.info().empty());
    update = slot.update();
    CHECK(slot.active() == nullptr);
    CHECK(update.fading != nullptr); // the 200 Hz one, fading
}

TEST_CASE("The audio side of a slot neither allocates nor frees", "[sample]")
{
    SampleSlot slot;
    slot.set(vloom::dsp::test::sineSample(100.0, 48000.0, 0.01));
    {
        AllocationCounter counter;
        (void)slot.update();
        CHECK(counter.allocations() == 0);
        CHECK(counter.deallocations() == 0);
    }
    slot.set(vloom::dsp::test::sineSample(200.0, 48000.0, 0.01));
    AllocationCounter counter;
    (void)slot.update();
    slot.fadeDone();
    CHECK(counter.allocations() == 0);
    CHECK(counter.deallocations() == 0);
}

TEST_CASE("A slot survives a host swapping samples while the audio side reads them", "[sample]")
{
    SampleSlot slot;
    std::atomic<bool> done{false};
    std::atomic<long> reads{0};
    std::atomic<long> torn{0};
    std::thread audio([&] {
        // What a processor does each block: pick up, read, let go of the old one.
        while (!done.load()) {
            (void)slot.update();
            if (const SampleBuffer* buffer = slot.active()) {
                float sum = 0.0f;
                for (int i = 0; i < buffer->frames(); ++i)
                    sum += buffer->at(0, i);
                reads.fetch_add(1);
                if (sum != buffer->at(0, 0) * static_cast<float>(buffer->frames()))
                    torn.fetch_add(1);
            }
            slot.fadeDone();
        }
    });
    for (int i = 0; i < 2000; ++i) {
        const float value = static_cast<float>(i % 7) * 0.125f;
        slot.set(vloom::dsp::test::makeSample(64, 48000.0, [value](int) { return value; }));
        if (i % 3 == 0)
            slot.collectGarbage();
    }
    done.store(true);
    audio.join();
    slot.set(nullptr);
    (void)slot.update();
    slot.fadeDone();
    slot.collectGarbage();
    CHECK(slot.pendingRelease() == 0);
    CHECK(reads.load() > 0);
    CHECK(torn.load() == 0); // every buffer read was whole: a constant sums to frames times itself
}
