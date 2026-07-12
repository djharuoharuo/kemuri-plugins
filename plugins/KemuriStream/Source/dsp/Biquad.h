#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// RBJ Audio EQ Cookbook biquad — JUCE 非依存。
// 係数の算出式は kemuri-stream-checker の codec_eq.py / codec_filter.js と
// バイト単位で同一（G3 で係数一致を検証する）。
//
// 係数は cascade~ / codec_eq.py と同じ「正規化済み」表現:
//   [b0/a0, b1/a0, b2/a0, a1/a0, a2/a0]
namespace kemuri::dsp
{

// MSVC は _USE_MATH_DEFINES 無しだと M_PI を定義しないため自前で持つ
inline constexpr double kPi = 3.14159265358979323846;

enum class FilterType { Peaking, HighShelf, LowShelf, HighPass, LowPass, Passthrough };

inline FilterType filterTypeFromString (const char* s)
{
    const std::string t (s);
    if (t == "peaking")   return FilterType::Peaking;
    if (t == "highshelf") return FilterType::HighShelf;
    if (t == "lowshelf")  return FilterType::LowShelf;
    if (t == "highpass")  return FilterType::HighPass;
    if (t == "lowpass")   return FilterType::LowPass;
    return FilterType::Passthrough;
}

// 正規化済み係数 [b0, b1, b2, a1, a2]（a0 で割った後）
struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    std::array<double, 5> toArray() const { return { b0, b1, b2, a1, a2 }; }
};

// codec_eq.py::biquad_coeffs と同一の式・同一の順序。
inline BiquadCoeffs makeBiquad (FilterType type, double freq, double q,
                                double gainDb, double sampleRate)
{
    if (type == FilterType::Passthrough || freq <= 0.0 || sampleRate <= 0.0)
        return {};  // 素通り

    const double A     = std::pow (10.0, gainDb / 40.0);
    const double w0    = 2.0 * kPi * freq / sampleRate;
    const double cosw0 = std::cos (w0);
    const double sinw0 = std::sin (w0);
    const double alpha = sinw0 / (2.0 * std::max (q, 1e-6));

    double a0, a1, a2, b0, b1, b2;

    switch (type)
    {
        case FilterType::Peaking:
            b0 = 1.0 + alpha * A;  b1 = -2.0 * cosw0;  b2 = 1.0 - alpha * A;
            a0 = 1.0 + alpha / A;  a1 = -2.0 * cosw0;  a2 = 1.0 - alpha / A;
            break;

        case FilterType::HighShelf:
        {
            const double sq = std::sqrt (A);
            b0 = A * ((A + 1.0) + (A - 1.0) * cosw0 + 2.0 * sq * alpha);
            b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0);
            b2 = A * ((A + 1.0) + (A - 1.0) * cosw0 - 2.0 * sq * alpha);
            a0 = (A + 1.0) - (A - 1.0) * cosw0 + 2.0 * sq * alpha;
            a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosw0);
            a2 = (A + 1.0) - (A - 1.0) * cosw0 - 2.0 * sq * alpha;
            break;
        }

        case FilterType::LowShelf:
        {
            const double sq = std::sqrt (A);
            b0 = A * ((A + 1.0) - (A - 1.0) * cosw0 + 2.0 * sq * alpha);
            b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0);
            b2 = A * ((A + 1.0) - (A - 1.0) * cosw0 - 2.0 * sq * alpha);
            a0 = (A + 1.0) + (A - 1.0) * cosw0 + 2.0 * sq * alpha;
            a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw0);
            a2 = (A + 1.0) + (A - 1.0) * cosw0 - 2.0 * sq * alpha;
            break;
        }

        case FilterType::HighPass:
            b0 = (1.0 + cosw0) / 2.0;  b1 = -(1.0 + cosw0);  b2 = (1.0 + cosw0) / 2.0;
            a0 = 1.0 + alpha;          a1 = -2.0 * cosw0;     a2 = 1.0 - alpha;
            break;

        case FilterType::LowPass:
            b0 = (1.0 - cosw0) / 2.0;  b1 = 1.0 - cosw0;      b2 = (1.0 - cosw0) / 2.0;
            a0 = 1.0 + alpha;          a1 = -2.0 * cosw0;     a2 = 1.0 - alpha;
            break;

        default:
            return {};
    }

    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

// Direct Form II Transposed の 1 段 biquad。1 チャンネル分の状態を持つ。
class BiquadFilter
{
public:
    void setCoeffs (const BiquadCoeffs& c) { coeffs = c; }
    void reset() { z1 = z2 = 0.0; }

    inline float processSample (float x) noexcept
    {
        const double in  = static_cast<double> (x);
        const double out = coeffs.b0 * in + z1;
        z1 = coeffs.b1 * in - coeffs.a1 * out + z2;
        z2 = coeffs.b2 * in - coeffs.a2 * out;
        return static_cast<float> (out);
    }

    void processBlock (float* data, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            data[i] = processSample (data[i]);
    }

private:
    BiquadCoeffs coeffs;
    double z1 = 0.0, z2 = 0.0;
};

// 複数段の biquad を直列に並べたカスケード（1 チャンネル分の状態）。
class BiquadCascade
{
public:
    void setStages (const std::vector<BiquadCoeffs>& stages)
    {
        filters.assign (stages.size(), BiquadFilter{});
        for (size_t i = 0; i < stages.size(); ++i)
            filters[i].setCoeffs (stages[i]);
    }

    void reset() { for (auto& f : filters) f.reset(); }

    inline float processSample (float x) noexcept
    {
        for (auto& f : filters)
            x = f.processSample (x);
        return x;
    }

    void processBlock (float* data, int numSamples) noexcept
    {
        for (auto& f : filters)
            f.processBlock (data, numSamples);
    }

    int numStages() const { return static_cast<int> (filters.size()); }

private:
    std::vector<BiquadFilter> filters;
};

} // namespace kemuri::dsp
