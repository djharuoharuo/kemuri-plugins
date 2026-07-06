#pragma once

#include <array>
#include <cmath>

#include "Pattern.h"

namespace kemuri::core
{

// トップラインのオンセット密度（16分×1小節、0-1 正規化）に対し、
// パターンがどれだけ「応答」するかをスコア化する。
// 表拍 (slot%4==0) はアクセントと合わせると高得点、裏はギャップを埋めると高得点。
// 正本 JS: _interactionScore。
inline double interactionScore (const Pattern& pat, const std::array<double, 16>& onsetHist)
{
    if (pat.notes.empty()) return 0.0;
    double s = 0.0;
    for (const auto& n : pat.notes)
    {
        int slot = static_cast<int> (std::lround (n.pos / 0.25)) % 16;
        if (slot < 0) slot += 16;
        if (slot % 4 == 0)
            s += 0.6 + 0.4 * onsetHist[static_cast<size_t> (slot)];
        else
            s += 1.0 - onsetHist[static_cast<size_t> (slot)];
    }
    return s / static_cast<double> (pat.notes.size());
}

} // namespace kemuri::core
