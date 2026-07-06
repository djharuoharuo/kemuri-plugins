#pragma once

#include <string>

#include "KeyContext.h"
#include "MusicTheory.h"

namespace kemuri::core
{

// パターン内のピッチ表現。数値はコード根からの半音オフセット、
// それ以外はコード/スケール由来の名前付きトークン。
// 正本 JS: _resolvePitch のトークン一覧。
enum class Tok
{
    Number, Third, Fourth, Fifth, Sixth, FlatSix, FlatSeven, Low5, Octave,
    P2, P3, P4, P5, ApproachDown1, ApproachUp1, ApproachDown2
};

struct PitchToken
{
    Tok kind  = Tok::Number;
    int value = 0;   // Tok::Number のときのみ有効
};

inline PitchToken pitchTokenFromString (const std::string& s)
{
    if (s == "3rd")        return { Tok::Third };
    if (s == "4th")        return { Tok::Fourth };
    if (s == "5th")        return { Tok::Fifth };
    if (s == "6th")        return { Tok::Sixth };
    if (s == "b6")         return { Tok::FlatSix };
    if (s == "b7")         return { Tok::FlatSeven };
    if (s == "low5")       return { Tok::Low5 };
    if (s == "octave")     return { Tok::Octave };
    if (s == "p2")         return { Tok::P2 };
    if (s == "p3")         return { Tok::P3 };
    if (s == "p4")         return { Tok::P4 };
    if (s == "p5")         return { Tok::P5 };
    if (s == "approach-1") return { Tok::ApproachDown1 };
    if (s == "approach+1") return { Tok::ApproachUp1 };
    if (s == "approach-2") return { Tok::ApproachDown2 };
    return { Tok::Number, 0 };
}

// トークンを絶対 MIDI ノート（ベース音域）へ解決する。
inline int resolvePitch (const PitchToken& t, const KeyContext& ctx, const KeyContext& nextCtx)
{
    const int anchor = ctx.lowAnchor;
    switch (t.kind)
    {
        case Tok::Number:        return clampBass (anchor + t.value);
        case Tok::Third:         return clampBass (anchor + (ctx.isMinor ? 3 : 4));
        case Tok::Fourth:        return clampBass (anchor + 5);
        case Tok::Fifth:         return clampBass (anchor + 7);
        case Tok::Sixth:         return clampBass (anchor + 9);
        case Tok::FlatSix:       return clampBass (anchor + 8);
        case Tok::FlatSeven:     return clampBass (anchor + 10);
        case Tok::Low5:          return clampBass (anchor - 5);
        case Tok::Octave:        return clampBass (anchor + 12);
        case Tok::P2:            return clampBass (anchor + ctx.penta[1]);
        case Tok::P3:            return clampBass (anchor + ctx.penta[2]);
        case Tok::P4:            return clampBass (anchor + ctx.penta[3]);
        case Tok::P5:            return clampBass (anchor + ctx.penta[4]);
        case Tok::ApproachDown1: return clampBass (nextCtx.lowAnchor - 1);
        case Tok::ApproachUp1:   return clampBass (nextCtx.lowAnchor + 1);
        case Tok::ApproachDown2: return clampBass (nextCtx.lowAnchor - 2);
    }
    return clampBass (anchor);
}

} // namespace kemuri::core
