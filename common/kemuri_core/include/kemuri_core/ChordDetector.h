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

// ── Viterbi 平滑化コード検出（v1.2, 解析経路の既定）─────────────────
// セグメント毎のテンプレートスコアを emission とし、24 状態（12 root × maj/min）の
// Viterbi で系列最適化する。自己遷移ボーナス（stay bias）で 1 セグメントだけの
// フラッピング（例: G#m↔Gm）を抑え、検出キーのダイアトニック和音へ弱い事前分布を
// かける。遷移は学習しない（短いループでの過学習を避ける）。
// 空セグメントは一様 emission → stay bias により前和音を自然に継承する。
inline std::vector<ChordSeg> detectProgressionViterbi (const std::vector<RawNote>& notes,
                                                       double clipLenBeats,
                                                       double segBeats,
                                                       int keyRoot,
                                                       int keyMode)
{
    std::vector<ChordSeg> result;
    if (notes.empty()) return result;

    const int numSeg  = std::max (1, static_cast<int> (std::lround (clipLenBeats / segBeats)));
    constexpr int nSt = 24;   // state = root * 2 + (0=maj, 1=min)

    // ダイアトニック事前分布: キーのスケール上の三和音に加点
    const auto& scale = (keyMode == 0) ? kScaleMajor : kScaleMinor;
    std::array<double, nSt> prior {};
    for (int r = 0; r < 12; ++r)
    {
        const int rel = ((r - keyRoot) % 12 + 12) % 12;
        bool inScale = false;
        int  degree  = -1;
        for (size_t d = 0; d < scale.size(); ++d)
            if (scale[d] == rel) { inScale = true; degree = static_cast<int> (d); break; }
        if (! inScale) continue;

        // メジャーキー: I,IV,V=maj / ii,iii,vi=min。マイナーキー: i,iv,v=min / bIII,bVI,bVII=maj
        const bool degMinor = (keyMode == 0) ? (degree == 1 || degree == 2 || degree == 5)
                                             : (degree == 0 || degree == 3 || degree == 4);
        prior[static_cast<size_t> (r * 2 + (degMinor ? 1 : 0))] += 0.35;   // 一致三和音
        prior[static_cast<size_t> (r * 2 + (degMinor ? 0 : 1))] += 0.10;   // 根音のみ一致
    }

    // emission（log スコア、セグメント毎に最大値で正規化）
    std::vector<std::array<double, nSt>> em (static_cast<size_t> (numSeg));
    std::vector<bool> segHasNotes (static_cast<size_t> (numSeg), false);
    for (int s = 0; s < numSeg; ++s)
    {
        const double sStart = s * segBeats;
        const double sEnd   = sStart + segBeats;
        std::array<double, 12> pch {};
        for (const auto& n : notes)
        {
            const double nStart = std::max (n.start, sStart);
            const double nEnd   = std::min (n.start + n.duration, sEnd);
            if (nEnd > nStart)
            {
                pch[static_cast<size_t> (((n.pitch % 12) + 12) % 12)] += (nEnd - nStart);
                segHasNotes[static_cast<size_t> (s)] = true;
            }
        }

        auto& e = em[static_cast<size_t> (s)];
        if (! segHasNotes[static_cast<size_t> (s)])
        {
            e.fill (0.0);   // 一様 → stay bias が前和音を維持
            continue;
        }

        double maxSc = 1e-12;
        std::array<double, nSt> raw {};
        for (int r = 0; r < 12; ++r)
        {
            for (int qi = 0; qi < 2; ++qi)
            {
                const auto& tmpl = (qi == 0) ? kChordTmplMaj : kChordTmplMin;
                double sc = 0.0;
                for (int j = 0; j < 12; ++j)
                    sc += pch[static_cast<size_t> (j)] * tmpl[static_cast<size_t> ((j - r + 12) % 12)];
                raw[static_cast<size_t> (r * 2 + qi)] = sc;
                maxSc = std::max (maxSc, sc);
            }
        }
        // β=4 で emission を鋭くする（1 セグメント = 1 観測のため）。
        // 境界: 現和音のスコアが最良の ~88% を下回ると切替が勝つ較正
        // （純三和音の C→Am は切替わり、~95% の僅差フラッピングは抑止）。
        for (int st = 0; st < nSt; ++st)
            e[static_cast<size_t> (st)] = 4.0 * std::log (raw[static_cast<size_t> (st)] / maxSc + 1e-6);
    }

    // Viterbi（自己遷移 0、切替ペナルティ 0.5 log 単位）
    const double stayLog   = 0.0;
    const double switchLog = -0.5;

    std::vector<std::array<double, nSt>> dp (static_cast<size_t> (numSeg));
    std::vector<std::array<int, nSt>>    bk (static_cast<size_t> (numSeg));

    for (int st = 0; st < nSt; ++st)
        dp[0][static_cast<size_t> (st)] = em[0][static_cast<size_t> (st)] + prior[static_cast<size_t> (st)];

    for (int s = 1; s < numSeg; ++s)
    {
        for (int st = 0; st < nSt; ++st)
        {
            double bestV = -std::numeric_limits<double>::infinity();
            int    bestP = 0;
            for (int pv = 0; pv < nSt; ++pv)
            {
                const double v = dp[static_cast<size_t> (s - 1)][static_cast<size_t> (pv)]
                                 + (pv == st ? stayLog : switchLog);
                if (v > bestV) { bestV = v; bestP = pv; }
            }
            dp[static_cast<size_t> (s)][static_cast<size_t> (st)] =
                bestV + em[static_cast<size_t> (s)][static_cast<size_t> (st)]
                + prior[static_cast<size_t> (st)] * 0.5;   // 事前分布は初期より弱く
            bk[static_cast<size_t> (s)][static_cast<size_t> (st)] = bestP;
        }
    }

    // backtrack
    int cur = 0;
    {
        double bestV = -std::numeric_limits<double>::infinity();
        for (int st = 0; st < nSt; ++st)
            if (dp[static_cast<size_t> (numSeg - 1)][static_cast<size_t> (st)] > bestV)
            { bestV = dp[static_cast<size_t> (numSeg - 1)][static_cast<size_t> (st)]; cur = st; }
    }
    std::vector<int> path (static_cast<size_t> (numSeg));
    for (int s = numSeg - 1; s >= 0; --s)
    {
        path[static_cast<size_t> (s)] = cur;
        if (s > 0) cur = bk[static_cast<size_t> (s)][static_cast<size_t> (cur)];
    }

    for (int s = 0; s < numSeg; ++s)
    {
        const int st = path[static_cast<size_t> (s)];
        result.push_back ({ s * segBeats, segBeats, st / 2, (st % 2 == 0) ? "maj" : "min" });
    }
    return result;
}

} // namespace kemuri::core
