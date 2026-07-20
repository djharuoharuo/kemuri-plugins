#pragma once

#include <array>

// 音楽理論の定数と、ベース音域へのスナップ関数。
// 正本: docs/reference/kemuri_generator.js（同名の定数・関数と一致させる）。
namespace kemuri::core
{

inline constexpr std::array<int, 7> kScaleMajor { 0, 2, 4, 5, 7, 9, 11 };
inline constexpr std::array<int, 7> kScaleMinor { 0, 2, 3, 5, 7, 8, 10 };
inline constexpr std::array<int, 3> kChordMajor { 0, 4, 7 };
inline constexpr std::array<int, 3> kChordMinor { 0, 3, 7 };
inline constexpr std::array<int, 5> kPentaMajor { 0, 2, 4, 7, 9 };
inline constexpr std::array<int, 5> kPentaMinor { 0, 3, 5, 7, 10 };

inline constexpr int kBassMin = 28;
inline constexpr int kBassMax = 47;

// Krumhansl-Schmuckler key profiles（JS 正本互換・G3 参照テスト用）
inline constexpr std::array<double, 12> kKsMajor {
    6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
inline constexpr std::array<double, 12> kKsMinor {
    6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

// Temperley-Kostka-Payne key profiles（v1.2: 解析経路の既定）。
// 比較研究で K-S 素のプロファイルより高精度（Temperley "Music and Probability" 2007）。
inline constexpr std::array<double, 12> kTkpMajor {
    0.748, 0.060, 0.488, 0.082, 0.670, 0.460, 0.096, 0.715, 0.104, 0.366, 0.057, 0.400 };
inline constexpr std::array<double, 12> kTkpMinor {
    0.712, 0.084, 0.474, 0.618, 0.049, 0.460, 0.105, 0.747, 0.404, 0.067, 0.133, 0.330 };

// Chord triad templates (root-relative)
inline constexpr std::array<double, 12> kChordTmplMaj {
    1.0, 0.05, 0.2, 0.05, 0.8, 0.2, 0.05, 0.8, 0.05, 0.2, 0.05, 0.3 };
inline constexpr std::array<double, 12> kChordTmplMin {
    1.0, 0.05, 0.2, 0.8, 0.05, 0.2, 0.05, 0.8, 0.05, 0.2, 0.4, 0.05 };

// Seventh-chord templates（v1.5: ソウル/ジャズ系うわネタの root 精度向上）。
// 出力 quality へは dom7/maj7→"maj"、m7→"min" とマップする。
inline constexpr std::array<double, 12> kChordTmplDom7 {
    1.0, 0.05, 0.2, 0.05, 0.8, 0.1, 0.05, 0.8, 0.05, 0.2, 0.7, 0.05 };
inline constexpr std::array<double, 12> kChordTmplMin7 {
    1.0, 0.05, 0.2, 0.8, 0.05, 0.2, 0.05, 0.8, 0.05, 0.2, 0.7, 0.05 };
inline constexpr std::array<double, 12> kChordTmplMaj7 {
    1.0, 0.05, 0.2, 0.05, 0.8, 0.1, 0.05, 0.8, 0.05, 0.2, 0.05, 0.7 };

// Snap a pitch into the bass range without changing pitch class.
inline int clampBass (int midi)
{
    while (midi < kBassMin) midi += 12;
    while (midi > kBassMax) midi -= 12;
    return midi;
}

// Snap a pitch toward an anchor octave, then into the bass range.
inline int snapNear (int midi, int anchor)
{
    while (midi - anchor > 6) midi -= 12;
    while (anchor - midi > 6) midi += 12;
    return clampBass (midi);
}

} // namespace kemuri::core
