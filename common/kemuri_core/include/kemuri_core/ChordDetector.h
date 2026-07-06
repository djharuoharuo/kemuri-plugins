#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "MusicTheory.h"
#include "Types.h"

namespace kemuri::core
{

// セグメント単位のコード検出。
// スコア = Σ(duration-weighted pitch-class strength × chord template)。
// 空セグメントは直前のコードを継承（なければ既定キー）。
// 正本 JS: _detectProgression。
inline std::vector<ChordSeg> detectProgression (const std::vector<RawNote>& notes,
                                                double clipLenBeats,
                                                double segBeats,
                                                int defaultRoot,
                                                int defaultMode)
{
    std::vector<ChordSeg> result;
    if (notes.empty()) return result;

    const int numSeg = std::max (1, static_cast<int> (std::lround (clipLenBeats / segBeats)));

    for (int s = 0; s < numSeg; ++s)
    {
        const double sStart = s * segBeats;
        const double sEnd   = sStart + segBeats;
        std::array<double, 12> pch {};
        bool anyHit = false;

        for (const auto& n : notes)
        {
            const double nStart = std::max (n.start, sStart);
            const double nEnd   = std::min (n.start + n.duration, sEnd);
            if (nEnd > nStart)
            {
                pch[static_cast<size_t> (((n.pitch % 12) + 12) % 12)] += (nEnd - nStart);
                anyHit = true;
            }
        }

        if (! anyHit)
        {
            if (! result.empty())
            {
                const auto& prev = result.back();
                result.push_back ({ sStart, segBeats, prev.root, prev.quality });
            }
            else
            {
                result.push_back ({ sStart, segBeats, defaultRoot,
                                    defaultMode == 0 ? "maj" : "min" });
            }
            continue;
        }

        double      bestScore = -std::numeric_limits<double>::infinity();
        int         bestRoot  = 0;
        std::string bestQ     = "maj";
        for (int r = 0; r < 12; ++r)
        {
            for (int qi = 0; qi < 2; ++qi)
            {
                const auto& tmpl = (qi == 0) ? kChordTmplMaj : kChordTmplMin;
                double sc = 0.0;
                for (int j = 0; j < 12; ++j)
                    sc += pch[static_cast<size_t> (j)] * tmpl[static_cast<size_t> ((j - r + 12) % 12)];
                if (sc > bestScore)
                {
                    bestScore = sc;
                    bestRoot  = r;
                    bestQ     = (qi == 0) ? "maj" : "min";
                }
            }
        }
        result.push_back ({ sStart, segBeats, bestRoot, bestQ });
    }
    return result;
}

} // namespace kemuri::core
