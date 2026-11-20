// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace vloom::dsp {

/// A complex fast Fourier transform of a power-of-two size (radix 2, in
/// place, double precision).
///
/// prepare() allocates its tables (not real-time); forward() and inverse()
/// then allocate nothing and are real-time safe.
class Fft {
public:
    static constexpr std::size_t kMaxSize = std::size_t{1} << 20;

    /// Sets up for `size` points: a power of two, 2 .. kMaxSize. Any other
    /// size leaves it unprepared (size() 0, transforms do nothing).
    void prepare(std::size_t size);
    /// The size prepared for; 0 if none.
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    /// X[k] = sum x[n] e^(-2 pi i k n / N), in place. Does nothing unless
    /// `data` holds exactly size() points.
    void forward(std::span<std::complex<double>> data) const noexcept;
    /// The inverse of forward(), scaled by 1/N, so inverse(forward(x)) is x.
    void inverse(std::span<std::complex<double>> data) const noexcept;

    [[nodiscard]] static constexpr bool isPowerOfTwo(std::size_t n) noexcept
    {
        return n >= 2 && (n & (n - 1)) == 0;
    }

private:
    void transform(std::span<std::complex<double>> data, bool inverse) const noexcept;

    std::size_t size_ = 0;
    std::vector<std::complex<double>> twiddles_; ///< e^(-2 pi i k / N), k < N/2
    std::vector<std::uint32_t> reversed_; ///< bit-reversed index of each point
};

} // namespace vloom::dsp
