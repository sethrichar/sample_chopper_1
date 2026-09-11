#pragma once
#include <complex>
#include <vector>

namespace chopper {

/// Minimal iterative radix-2 complex FFT. Size must be a power of two.
class Fft {
public:
    explicit Fft(int size);
    int size() const { return n_; }
    /// In-place forward transform.
    void forward(std::vector<std::complex<float>>& buf) const;
    /// Real input (length size) -> magnitudes for bins [0, size/2].
    void magnitudes(const float* input, std::vector<float>& mags) const;

private:
    int n_;
    std::vector<int> bitrev_;
    std::vector<std::complex<float>> twiddle_;
    mutable std::vector<std::complex<float>> scratch_;
};

} // namespace chopper
