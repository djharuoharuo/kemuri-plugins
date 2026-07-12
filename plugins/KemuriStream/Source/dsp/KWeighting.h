#pragma once

#include <vector>

#include "Biquad.h"

// K-weighting フィルタ（pyloudnorm.Meter デフォルトと厳密一致）。
//   stage 1: high-shelf 1500 Hz, +4.0 dB, Q 1/sqrt(2)
//   stage 2: high-pass    38 Hz, Q 0.5
//
// ※ M4L 版 codec_filter.js は教科書の BS.1770 値（1681.97 / 38.13）を使っていたが、
//    kemuri-stream-checker の Python 側は全て pyln.Meter(sr) のデフォルト係数
//    （下記 1500 / 38）を「真値」として使う（analyzer.py・-14 LUFS 較正ファイル・
//    test_normalize.py）。KemuriStream は Python 正本＝pyloudnorm に合わせることで
//    メーター精度を M4L より向上させる（G3 で ±0.1 LU 一致を検証）。
//    式は RBJ cookbook（pyloudnorm も同一式・alpha=sin/2Q）なので makeBiquad を流用。
namespace kemuri::dsp
{

inline constexpr double kKWeightShelfHz = 1500.0;
inline constexpr double kKWeightShelfQ  = 0.7071067811865475;  // 1/sqrt(2)
inline constexpr double kKWeightShelfDb = 4.0;
inline constexpr double kKWeightHpHz    = 38.0;
inline constexpr double kKWeightHpQ     = 0.5;

// K-weighting の 2 段係数を返す（[shelf, highpass] の順）。
inline std::vector<BiquadCoeffs> makeKWeightingStages (double sampleRate)
{
    return {
        makeBiquad (FilterType::HighShelf, kKWeightShelfHz, kKWeightShelfQ, kKWeightShelfDb, sampleRate),
        makeBiquad (FilterType::HighPass,  kKWeightHpHz,    kKWeightHpQ,    0.0,             sampleRate),
    };
}

class KWeightingFilter
{
public:
    void prepare (double sampleRate)
    {
        cascade.setStages (makeKWeightingStages (sampleRate));
        cascade.reset();
    }

    void reset() { cascade.reset(); }

    inline float processSample (float x) noexcept { return cascade.processSample (x); }

    void processBlock (float* data, int numSamples) noexcept
    {
        cascade.processBlock (data, numSamples);
    }

private:
    BiquadCascade cascade;
};

} // namespace kemuri::dsp
