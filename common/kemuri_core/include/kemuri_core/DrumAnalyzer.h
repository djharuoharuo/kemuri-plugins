#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

#include "MidiAnalyzer.h"

// v2: ドラム（Drum Rack の MIDI）からキックの位置を読む。
// ベースのリズムはキックと一体で鳴るのが 90 年代ブームバップの本質なので、
// ユーザー自身のキックにベースを同期させる。JUCE 非依存。
namespace kemuri::core
{

struct KickAnalysis
{
    bool ok        = false;
    int  kickNote  = -1;                       // 推定したキックのノート番号
    int  loopBars  = 0;                        // キックパターンの周期 L（index 0 = ソング小節 ≡ 0 mod L）
    std::vector<std::vector<double>> bars;     // L 小節分。各小節内のキック位置（beats 0..4、スイング込みの実位置）
    bool hasSwing     = false;
    int  swingPercent = 50;
};

// events: ドラムトラックのノートオン/オフ（ソング絶対位置 ppq）。
// [firstBar, endBar) の区間を解析する。
inline KickAnalysis analyzeKick (const std::vector<RawEvent>& events, long firstBar, long endBar)
{
    KickAnalysis r;
    if (endBar <= firstBar) return r;

    // キックのノート番号を推定:
    //  - 小節頭（1 拍目）に来る割合を最重視
    //  - 打点が密すぎる（ハイハット等, 1 小節 6 打超）ものは大きく減点
    //  - Drum Rack 既定のキック（C1 = 36）/ GM キック 35、低いノート番号に加点
    const int numBars = static_cast<int> (endBar - firstBar);
    std::map<int, int> hits, downbeats;
    for (const auto& e : events)
    {
        if (! e.isOn || e.ppq < firstBar * 4.0 || e.ppq >= endBar * 4.0) continue;
        hits[e.pitch]++;
        const double barPos = e.ppq - std::floor (e.ppq / 4.0 + 1e-9) * 4.0;
        if (barPos < 0.08 || barPos > 3.92) downbeats[e.pitch]++;   // 小節頭（わずかな前ノリも許容）
    }
    if (hits.empty()) return r;
    int best = -1;
    double bestScore = -1e9;
    for (const auto& kv : hits)
    {
        const double density      = kv.second / static_cast<double> (numBars);
        const double downbeatRate = std::min (1.0, downbeats[kv.first] / static_cast<double> (numBars));
        double sc = downbeatRate * 10.0 - std::max (0.0, density - 6.0) * 2.0;
        if (kv.first == 36 || kv.first == 35) sc += 4.0;
        if (kv.first <= 40)                   sc += 1.0;
        if (sc > bestScore) { bestScore = sc; best = kv.first; }
    }
    r.kickNote = best;

    // 小節ごとのキック位置
    std::vector<std::vector<double>> perBar (static_cast<size_t> (numBars));
    for (const auto& e : events)
    {
        if (! e.isOn || e.pitch != best) continue;
        // わずかな前ノリ（小節頭直前）は次の小節頭として扱う
        double ppq = e.ppq;
        long bar = static_cast<long> (std::floor (ppq / 4.0 + 1e-9));
        double pos = ppq - bar * 4.0;
        if (pos > 3.92) { ++bar; pos = 0.0; }
        if (bar < firstBar || bar >= endBar) continue;
        perBar[static_cast<size_t> (bar - firstBar)].push_back (std::max (0.0, pos));
    }
    for (auto& v : perBar) std::sort (v.begin(), v.end());

    // 周期（16 分に丸めたステップ集合の Dice 類似, 候補 1/2/4/8）
    auto stepSet = [] (const std::vector<double>& v)
    {
        std::set<int> s;
        for (double p : v) s.insert (static_cast<int> (std::lround (p * 4.0)) % 16);
        return s;
    };
    auto dice = [] (const std::set<int>& a, const std::set<int>& b)
    {
        if (a.empty() && b.empty()) return 1.0;
        int common = 0;
        for (int x : a) if (b.count (x)) ++common;
        return 2.0 * common / static_cast<double> (a.size() + b.size());
    };
    int L = 0;
    for (int p : { 1, 2, 4, 8 })
    {
        if (p >= numBars) continue;
        double sum = 0.0; int cnt = 0;
        for (int b = p; b < numBars; ++b) { sum += dice (stepSet (perBar[static_cast<size_t> (b)]), stepSet (perBar[static_cast<size_t> (b - p)])); ++cnt; }
        if (cnt > 0 && sum / cnt >= 0.85) { L = p; break; }
    }
    if (L == 0) { L = 1; while (L * 2 <= std::min (numBars, 8)) L *= 2; }
    r.loopBars = L;

    // 位相合わせ: ループ内位置 i ごとに、最新の出現小節の実位置を採用
    r.bars.assign (static_cast<size_t> (L), {});
    for (int i = 0; i < L; ++i)
        for (int b = numBars - 1; b >= 0; --b)
            if (static_cast<int> ((((firstBar + b) % L) + L) % L) == i && ! perBar[static_cast<size_t> (b)].empty())
            { r.bars[static_cast<size_t> (i)] = perBar[static_cast<size_t> (b)]; break; }

    // スイング: ドラム全体（ハット含む）の 16 分オフビートの遅れから推定
    std::vector<double> samples;
    for (const auto& e : events)
    {
        if (! e.isOn || e.ppq < firstBar * 4.0 || e.ppq >= endBar * 4.0) continue;
        double frac = std::fmod (e.ppq, 0.5);
        if (frac < 0.0) frac += 0.5;
        if (frac >= 0.15 && frac <= 0.42) samples.push_back (frac / 0.5 * 100.0);
    }
    if (samples.size() >= 4)
    {
        std::nth_element (samples.begin(), samples.begin() + static_cast<long> (samples.size() / 2), samples.end());
        const double med = samples[samples.size() / 2];
        if (med >= 52.0) { r.hasSwing = true; r.swingPercent = std::clamp (static_cast<int> (std::lround (med)), 50, 66); }
    }

    bool any = false;
    for (const auto& b : r.bars) if (! b.empty()) any = true;
    r.ok = any;
    return r;
}

} // namespace kemuri::core
