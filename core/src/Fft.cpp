#include "chopper/Fft.h"
#include <cmath>
#include <stdexcept>

namespace chopper {

Fft::Fft(int size) : n_(size)
{
    if (size < 2 || (size & (size - 1)) != 0) throw std::invalid_argument("Fft size must be a power of two");
    bitrev_.resize(size_t(size));
    int bits = 0; while ((1 << bits) < size) ++bits;
    for (int i = 0; i < size; ++i) {
        int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        bitrev_[size_t(i)] = r;
    }
    twiddle_.resize(size_t(size / 2));
    for (int k = 0; k < size / 2; ++k) {
        double a = -2.0 * M_PI * k / size;
        twiddle_[size_t(k)] = { float(std::cos(a)), float(std::sin(a)) };
    }
    scratch_.resize(size_t(size));
}

void Fft::forward(std::vector<std::complex<float>>& buf) const
{
    const int n = n_;
    for (int i = 0; i < n; ++i) { int j = bitrev_[size_t(i)]; if (j > i) std::swap(buf[size_t(i)], buf[size_t(j)]); }
    for (int len = 2; len <= n; len <<= 1) {
        const int half = len / 2, step = n / len;
        for (int i = 0; i < n; i += len)
            for (int k = 0; k < half; ++k) {
                auto t = twiddle_[size_t(k * step)] * buf[size_t(i + k + half)];
                auto u = buf[size_t(i + k)];
                buf[size_t(i + k)] = u + t;
                buf[size_t(i + k + half)] = u - t;
            }
    }
}

void Fft::magnitudes(const float* input, std::vector<float>& mags) const
{
    for (int i = 0; i < n_; ++i) scratch_[size_t(i)] = { input[i], 0.0f };
    forward(scratch_);
    mags.resize(size_t(n_ / 2 + 1));
    for (int k = 0; k <= n_ / 2; ++k) mags[size_t(k)] = std::abs(scratch_[size_t(k)]);
}

} // namespace chopper
