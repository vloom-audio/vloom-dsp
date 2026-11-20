// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "support/AllocationCounter.hpp"
#include "vloom/dsp/Fft.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <vector>

using Catch::Approx;
using vloom::dsp::Fft;

TEST_CASE("Fft matches the plain DFT and inverts", "[fft]")
{
    for (const std::size_t size : {2u, 8u, 64u, 1024u}) {
        Fft fft;
        fft.prepare(size);
        REQUIRE(fft.size() == size);
        std::vector<std::complex<double>> data(size);
        for (std::size_t n = 0; n < size; ++n)
            data[n] = {std::sin(0.37 * static_cast<double>(n)) + 0.1 * static_cast<double>(n % 5),
                std::cos(1.3 * static_cast<double>(n))};
        const std::vector<std::complex<double>> input = data;

        fft.forward(data);
        for (std::size_t k = 0; k < size; k += std::max<std::size_t>(1, size / 16)) {
            std::complex<double> expected = 0.0;
            for (std::size_t n = 0; n < size; ++n)
                expected += input[n]
                    * std::polar(1.0,
                        -2.0 * std::numbers::pi * static_cast<double>(k * n) / static_cast<double>(size));
            INFO("size " << size << ", bin " << k);
            CHECK(std::abs(data[k] - expected) < 1.0e-9 * static_cast<double>(size));
        }

        fft.inverse(data);
        for (std::size_t n = 0; n < size; ++n)
            CHECK(std::abs(data[n] - input[n]) < 1.0e-12 * static_cast<double>(size));
    }
}

TEST_CASE("Fft refuses sizes it cannot do, and allocates nothing when it runs", "[fft]")
{
    Fft fft;
    fft.prepare(1000);
    CHECK(fft.size() == 0);
    std::vector<std::complex<double>> data(1000, {1.0, 0.0});
    fft.forward(data); // does nothing
    CHECK(data[0] == std::complex<double>(1.0, 0.0));

    fft.prepare(4096);
    std::vector<std::complex<double>> wrong(2048, {1.0, 0.0});
    fft.forward(wrong); // the wrong size: does nothing
    CHECK(wrong[1] == std::complex<double>(1.0, 0.0));

    std::vector<std::complex<double>> signal(4096, {0.0, 0.0});
    signal[0] = 1.0;
    const vloom::dsp::test::AllocationCounter counter;
    fft.forward(signal);
    fft.inverse(signal);
    CHECK(counter.allocations() == 0);
    CHECK(signal[0].real() == Approx(1.0));
    CHECK(std::abs(signal[1]) < 1.0e-12);
}
