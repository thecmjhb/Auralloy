// Auralloy - small radix-2 FFT (complex, in place) and STFT helpers. No dependencies.
#pragma once
#include <cmath>
#include <complex>
#include <vector>

namespace al
{
class Fft
{
public:
    explicit Fft (int sizePow2) : n (sizePow2)
    {
        rev.resize ((size_t) n);
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
        tw.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i)
            tw[(size_t) i] = { (float) std::cos (-2.0 * 3.14159265358979 * i / n), (float) std::sin (-2.0 * 3.14159265358979 * i / n) };
    }

    int size() const { return n; }

    // forward transform (in place)
    void forward (std::complex<float>* x) const { run (x, false); }
    // inverse transform (in place), scaled by 1/n
    void inverse (std::complex<float>* x) const
    {
        run (x, true);
        const float s = 1.0f / (float) n;
        for (int i = 0; i < n; ++i) x[i] *= s;
    }

private:
    void run (std::complex<float>* x, bool inv) const
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = rev[(size_t) i];
            if (j > i) std::swap (x[i], x[j]);
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int k = 0; k < half; ++k)
                {
                    std::complex<float> w = tw[(size_t) (k * step)];
                    if (inv) w = std::conj (w);
                    const std::complex<float> a = x[i + k], b = x[i + k + half] * w;
                    x[i + k] = a + b;
                    x[i + k + half] = a - b;
                }
        }
    }

    int n;
    std::vector<int> rev;
    std::vector<std::complex<float>> tw;
};

inline std::vector<float> hannWindow (int n)
{
    std::vector<float> w ((size_t) n);
    for (int i = 0; i < n; ++i) w[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * 3.14159265f * (float) i / (float) n);
    return w;
}
} // namespace al
