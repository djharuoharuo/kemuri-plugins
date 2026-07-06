#pragma once

#include <array>
#include <vector>

#include "Types.h"

namespace kemuri::core
{

// {4, 8, 16} のうち進行が反復する最小周期を返す。該当なしなら全長。
// 正本 JS: _detectLoopBars。
inline int detectLoopBars (const std::vector<ChordSeg>& prog)
{
    if (prog.empty()) return 0;
    constexpr std::array<int, 3> candidates { 4, 8, 16 };
    const int len = static_cast<int> (prog.size());
    for (int p : candidates)
    {
        if (p > len) continue;
        bool match = true;
        for (int i = p; i < len; ++i)
        {
            if (prog[static_cast<size_t> (i)].root != prog[static_cast<size_t> (i - p)].root
                || prog[static_cast<size_t> (i)].quality != prog[static_cast<size_t> (i - p)].quality)
            {
                match = false;
                break;
            }
        }
        if (match) return p;
    }
    return len;
}

} // namespace kemuri::core
