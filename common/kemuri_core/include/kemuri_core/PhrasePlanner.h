#pragma once

#include <algorithm>
#include <cmath>

namespace kemuri::core
{

// 1 小節ぶんのフレーズ展開フラグ。
// 展開はループ後半のみ: 4=bar3 turn / 8=bar7 climax / 16=bar11 mid + bar15 climax。
// 正本 JS: buildNotes ループ内のフラグ計算。
struct PhraseFlags
{
    int  fillBars       = 0;
    int  barInPhrase    = 0;
    bool inDevHalf      = false;
    bool isLastOfPhrase = false;
    bool isDevelopment  = false; // final climax (8/16 の最終小節)
    bool midDevelopment = false; // 16 小節の bar11
    bool isFill         = false;
};

inline int fillBarsFor (int fill)
{
    return (fill <= 0) ? 0
                       : std::min (4, static_cast<int> (std::ceil (fill / 25.0)));
}

inline PhraseFlags computePhrase (int bars, int fill, int bar)
{
    PhraseFlags f;
    f.fillBars = fillBarsFor (fill);
    const int half = bars / 2;

    f.barInPhrase   = bar % 4;
    f.inDevHalf     = (bars <= 4) || (bar >= half);
    f.isLastOfPhrase = f.inDevHalf && (f.barInPhrase == 3);
    const bool isLastBarTotal = (bar == bars - 1);
    f.isDevelopment  = (bars >= 8) && isLastBarTotal;
    f.midDevelopment = (bars >= 16) && (bar == half + 3);
    f.isFill         = f.inDevHalf && f.fillBars > 0 && f.barInPhrase >= (4 - f.fillBars);
    return f;
}

} // namespace kemuri::core
