#pragma once

#include <array>
#include <cmath>
#include <limits>

#include "MusicTheory.h"

namespace kemuri::core
{

struct KeyResult
{
    int    root  = 0;   // 0-11
    int    mode  = 0;   // 0=major, 1=minor
    double score = 0.0;
};

// Pearson correlation between a pitch-class vector and a rotated key profile.
// 正本 JS: _ksCorr。
inline double ksCorr (const std::array<double, 12>& vec,
                      const std::array<double, 12>& profile,
                      int root)
{
    constexpr int n = 12;
    double sX = 0, sY = 0, sXY = 0, sX2 = 0, sY2 = 0;
    for (int i = 0; i < n; ++i)
    {
        const double x = vec[static_cast<size_t> (i)];
        const double y = profile[static_cast<size_t> ((i - root + 12) % 12)];
        sX += x; sY += y; sXY += x * y; sX2 += x * x; sY2 += y * y;
    }
    const double d = std::sqrt ((n * sX2 - sX * sX) * (n * sY2 - sY * sY));
    return d < 1e-10 ? 0.0 : (n * sXY - sX * sY) / d;
}

// 24 候補（12 roots × maj/min）から最良キーを選ぶ共通実装。
inline KeyResult detectKeyWithProfiles (const std::array<double, 12>& hist,
                                        const std::array<double, 12>& majorProfile,
                                        const std::array<double, 12>& minorProfile)
{
    double total = 0.0;
    for (double v : hist) total += v;

    std::array<double, 12> norm {};
    for (int i = 0; i < 12; ++i)
        norm[static_cast<size_t> (i)] = total > 0.0 ? hist[static_cast<size_t> (i)] / total : 0.0;

    KeyResult best;
    best.score = -std::numeric_limits<double>::infinity();
    for (int root = 0; root < 12; ++root)
    {
        const double mj = ksCorr (norm, majorProfile, root);
        const double mn = ksCorr (norm, minorProfile, root);
        if (mj > best.score) { best.score = mj; best.root = root; best.mode = 0; }
        if (mn > best.score) { best.score = mn; best.root = root; best.mode = 1; }
    }
    return best;
}

// Krumhansl-Schmuckler（JS 正本互換・G3 参照テスト用）。
inline KeyResult detectKey (const std::array<double, 12>& hist)
{
    return detectKeyWithProfiles (hist, kKsMajor, kKsMinor);
}

// Temperley-Kostka-Payne（v1.2: 解析経路の既定。K-S より高精度）。
inline KeyResult detectKeyTemperley (const std::array<double, 12>& hist)
{
    return detectKeyWithProfiles (hist, kTkpMajor, kTkpMinor);
}

} // namespace kemuri::core
