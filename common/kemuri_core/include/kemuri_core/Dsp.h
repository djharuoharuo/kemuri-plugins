#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

// 解析用の最小 DSP 部品（JUCE 非依存・message thread 用）。
// v2: うわネタはサンプルのチョップ（MIDI ノート番号は音程ではない）なので、
// サイドチェインで受けたオーディオから音程を読む。
namespace kemuri::core::dsp
{

inline constexpr double kPi = 3.14159265358979323846;

// in-place iterative radix-2 FFT（size は 2 の冪）
inline void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * kPi / static_cast<double> (len);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j];
                const auto v = a[i + j + len / 2] * w;
                a[i + j]           = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// RBJ cookbook biquad（Direct Form II transposed）
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    static Biquad lowpass (double sampleRate, double cutoff, double q = 0.70710678)
    {
        Biquad f;
        const double w0 = 2.0 * kPi * cutoff / sampleRate;
        const double cs = std::cos (w0), sn = std::sin (w0);
        const double alpha = sn / (2.0 * q);
        const double a0 = 1.0 + alpha;
        f.b0 = ((1.0 - cs) * 0.5) / a0;
        f.b1 = (1.0 - cs) / a0;
        f.b2 = ((1.0 - cs) * 0.5) / a0;
        f.a1 = (-2.0 * cs) / a0;
        f.a2 = (1.0 - alpha) / a0;
        return f;
    }

    float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return static_cast<float> (y);
    }

    void reset() { z1 = z2 = 0.0; }
};

// YIN 基本周波数推定（de Cheveigné & Kawahara 2002）。
// x[0..n) の窓で f0 を返す（無声なら 0）。conf = 1 - CMND 最小値。
inline double yinF0 (const float* x, size_t n, double sampleRate,
                     double fmin, double fmax, double threshold, double& conf)
{
    conf = 0.0;
    const int tauMin = std::max (2, static_cast<int> (sampleRate / fmax));
    const int tauMax = std::min (static_cast<int> (n / 2), static_cast<int> (sampleRate / fmin) + 1);
    if (tauMax <= tauMin + 2) return 0.0;

    const size_t W = n - static_cast<size_t> (tauMax);
    std::vector<double> d (static_cast<size_t> (tauMax) + 1, 0.0);
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double s = 0.0;
        for (size_t j = 0; j < W; ++j)
        {
            const double diff = static_cast<double> (x[j]) - static_cast<double> (x[j + static_cast<size_t> (tau)]);
            s += diff * diff;
        }
        d[static_cast<size_t> (tau)] = s;
    }

    // cumulative mean normalized difference
    std::vector<double> cm (d.size(), 1.0);
    double running = 0.0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        running += d[static_cast<size_t> (tau)];
        cm[static_cast<size_t> (tau)] = running > 0.0 ? d[static_cast<size_t> (tau)] * tau / running : 1.0;
    }

    int best = -1;
    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        if (cm[static_cast<size_t> (tau)] < threshold)
        {
            while (tau + 1 <= tauMax && cm[static_cast<size_t> (tau + 1)] < cm[static_cast<size_t> (tau)]) ++tau;
            best = tau;
            break;
        }
    }
    if (best < 0) return 0.0;

    // parabolic interpolation
    double tauF = best;
    if (best > tauMin && best < tauMax)
    {
        const double s0 = cm[static_cast<size_t> (best - 1)];
        const double s1 = cm[static_cast<size_t> (best)];
        const double s2 = cm[static_cast<size_t> (best + 1)];
        const double denom = s0 - 2.0 * s1 + s2;
        if (std::abs (denom) > 1e-12) tauF = best + 0.5 * (s0 - s2) / denom;
    }
    conf = 1.0 - cm[static_cast<size_t> (best)];
    return sampleRate / tauF;
}

inline double hzToMidi (double hz) { return 69.0 + 12.0 * std::log2 (hz / 440.0); }

} // namespace kemuri::core::dsp
