#pragma once

#include <array>
#include <string>

#include "MusicTheory.h"

namespace kemuri::core
{

// 特定コード（root pitch-class + quality）に対するスケール/コード文脈。
// 正本 JS: makeKeyContext。
struct KeyContext
{
    int  root      = 0;
    bool isMinor   = false;
    std::array<int, 7> scale = kScaleMajor;
    std::array<int, 3> chord = kChordMajor;
    std::array<int, 5> penta = kPentaMajor;
    int  lowAnchor = 24;   // C1 + root → sub bass
    int  midAnchor = 36;   // C2 + root → mid bass
};

inline KeyContext makeKeyContext (int rootPc, bool isMinor)
{
    KeyContext c;
    c.root      = rootPc;
    c.isMinor   = isMinor;
    c.scale     = isMinor ? kScaleMinor : kScaleMajor;
    c.chord     = isMinor ? kChordMinor : kChordMajor;
    c.penta     = isMinor ? kPentaMinor : kPentaMajor;
    c.lowAnchor = 24 + rootPc;
    c.midAnchor = 36 + rootPc;
    return c;
}

inline KeyContext makeKeyContext (int rootPc, const std::string& quality)
{
    return makeKeyContext (rootPc, quality == "min");
}

} // namespace kemuri::core
