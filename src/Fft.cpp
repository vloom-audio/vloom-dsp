// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "vloom/dsp/Fft.hpp"

#include <numbers>
#include <utility>

namespace vloom::dsp {

void Fft::prepare(std::size_t size)
{
    if (!isPowerOfTwo(size) || size > kMaxSize) {
        size_ = 0;
        twiddles_.clear();
        reversed_.clear();
        return;
    }
    size_ = size;
    twiddles_.resize(size / 2);
    for (std::size_t k = 0; k < size / 2; ++k)
        twiddles_[k]
            = std::polar(1.0, -2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(size));
    reversed_.resize(size);
    int bits = 0;
    while ((std::size_t{1} << bits) < size)
        ++bits;
    for (std::size_t i = 0; i < size; ++i) {
        std::size_t r = 0;
        for (int b = 0; b < bits; ++b)
            r |= ((i >> b) & 1u) << (bits - 1 - b);
        reversed_[i] = static_cast<std::uint32_t>(r);
    }
}

void Fft::forward(std::span<std::complex<double>> data) const noexcept { transform(data, false); }

void Fft::inverse(std::span<std::complex<double>> data) const noexcept
{
    transform(data, true);
    if (data.size() != size_ || size_ == 0)
        return;
    const double scale = 1.0 / static_cast<double>(size_);
    for (std::complex<double>& value : data)
        value *= scale;
}

void Fft::transform(std::span<std::complex<double>> data, bool inverse) const noexcept
{
    if (size_ == 0 || data.size() != size_)
        return;
    for (std::size_t i = 0; i < size_; ++i) {
        const std::size_t r = reversed_[i];
        if (i < r)
            std::swap(data[i], data[r]);
    }
    for (std::size_t half = 1; half < size_; half *= 2) {
        const std::size_t stride = size_ / (2 * half); // twiddle step for this stage
        for (std::size_t start = 0; start < size_; start += 2 * half) {
            for (std::size_t k = 0; k < half; ++k) {
                const std::complex<double> w
                    = inverse ? std::conj(twiddles_[k * stride]) : twiddles_[k * stride];
                const std::complex<double> odd = w * data[start + k + half];
                const std::complex<double> even = data[start + k];
                data[start + k] = even + odd;
                data[start + k + half] = even - odd;
            }
        }
    }
}

} // namespace vloom::dsp
