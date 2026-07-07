#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ChordDetector.h"
#include "KeyDetector.h"
#include "LoopDetector.h"
#include "Types.h"

// MIDI 入力解析（R5）。JUCE 非依存で単体テスト可能にするため、
// キャプチャ済みノートイベント列を受け取り解析結果を返す。
// 正本 JS: _finishMidiAnalysis / _detectProgression / _detectLoopBars。
namespace kemuri::core
{

// キャプチャした 1 イベント（beats 単位の絶対時刻）
struct RawEvent
{
    double ppq;
    int    pitch;
    bool   isOn;
};

struct AnalysisResult
{
    bool                  hasInput            = false;
    int                   keyRoot             = 0;   // 0-11
    int                   keyMode             = 0;   // 0=major, 1=minor
    int                   clipBars            = 0;
    std::vector<ChordSeg> progBar;
    std::vector<ChordSeg> progHalfBar;
    int                   loopBars            = 0;
    std::array<double, 16> onsetHist          {};
    bool                  hasOnset            = false;
    double                notesPerBar         = 0.0;
    int                   suggestedComplexity = -1;
};

// note-on / note-off をペアリングして RawNote 列にする。
// start は windowStart 起点、閉じないノートは windowEnd までの長さ（最低 0.25）。
inline std::vector<RawNote> pairEvents (const std::vector<RawEvent>& events,
                                        double windowStart, double windowEnd)
{
    std::vector<RawNote> notes;
    // pitch ごとに未クローズの on を積む（同一ピッチの重なりに対応）
    std::unordered_map<int, std::vector<double>> pending;

    for (const auto& e : events)
    {
        if (e.ppq < windowStart) continue;
        if (e.isOn)
        {
            pending[e.pitch].push_back (e.ppq);
        }
        else
        {
            auto it = pending.find (e.pitch);
            if (it != pending.end() && ! it->second.empty())
            {
                const double onPpq = it->second.front();
                it->second.erase (it->second.begin());
                const double dur = std::max (0.05, e.ppq - onPpq);
                notes.push_back ({ e.pitch, onPpq - windowStart, dur });
            }
        }
    }
    // 閉じなかった on は windowEnd まで鳴っていたとみなす
    for (auto& kv : pending)
        for (double onPpq : kv.second)
            notes.push_back ({ kv.first, onPpq - windowStart,
                               std::max (0.25, windowEnd - onPpq) });

    std::sort (notes.begin(), notes.end(),
               [] (const RawNote& a, const RawNote& b) { return a.start < b.start; });
    return notes;
}

// ノート内容（16分スロット＋ピッチ）の繰り返しで最小ループ周期を検出する。
// コード進行だけの detectLoopBars と違い、コードが一定でもメロディ/リズムの
// 2 小節ループ等を拾える。候補は {1,2,4,8,16}。該当なしなら clipBars。
inline int detectNoteLoopBars (const std::vector<RawNote>& notes, int clipBars)
{
    if (clipBars <= 1) return std::max (1, clipBars);

    std::vector<std::vector<std::pair<int, int>>> fp (static_cast<size_t> (clipBars));
    for (const auto& n : notes)
    {
        const int b = static_cast<int> (std::floor (n.start / 4.0 + 1e-9));
        if (b < 0 || b >= clipBars) continue;
        const int slot = static_cast<int> (std::lround ((n.start - b * 4.0) / 0.25));
        fp[static_cast<size_t> (b)].push_back ({ slot, n.pitch });
    }
    for (auto& v : fp)
        std::sort (v.begin(), v.end());

    for (int p : { 1, 2, 4, 8, 16 })
    {
        if (p >= clipBars) continue;
        bool match = true;
        for (int b = p; b < clipBars && match; ++b)
            if (fp[static_cast<size_t> (b)] != fp[static_cast<size_t> (b - p)])
                match = false;
        if (match) return p;
    }
    return clipBars;
}

// アライン済みノート列（start は 0 起点）を解析する。
inline AnalysisResult analyzeNotes (const std::vector<RawNote>& notes)
{
    AnalysisResult r;
    if (notes.empty())
        return r;   // hasInput=false（R8）

    r.hasInput = true;

    // pitch-class ヒストグラム（ノート数カウント）＋ 最終エンド
    std::array<double, 12> hist {};
    double maxEnd = 0.0;
    for (const auto& n : notes)
    {
        hist[static_cast<size_t> (((n.pitch % 12) + 12) % 12)] += 1.0;
        maxEnd = std::max (maxEnd, n.start + n.duration);
    }

    const auto key = detectKey (hist);
    r.keyRoot = key.root;
    r.keyMode = key.mode;

    const double clipLenBeats = std::max (4.0, std::ceil (maxEnd / 4.0) * 4.0);
    r.clipBars = std::max (1, static_cast<int> (std::llround (clipLenBeats / 4.0)));

    r.progBar     = detectProgression (notes, clipLenBeats, 4, r.keyRoot, r.keyMode);
    r.progHalfBar = detectProgression (notes, clipLenBeats, 2, r.keyRoot, r.keyMode);
    // ループ長はノート内容ベース（2 小節ループ等も検出）。コード進行が短周期なら
    // その周期も尊重して小さい方を採る。
    r.loopBars    = std::min (detectNoteLoopBars (notes, r.clipBars),
                              detectLoopBars (r.progBar));

    // 16 分オンセットヒストグラム（1 小節に畳み込み、max で正規化）
    std::array<double, 16> h {};
    double hmax = 0.0;
    for (const auto& n : notes)
    {
        int sl = static_cast<int> (std::lround (std::fmod (n.start, 4.0) / 0.25)) % 16;
        if (sl < 0) sl += 16;
        h[static_cast<size_t> (sl)] += 1.0;
    }
    for (double v : h) hmax = std::max (hmax, v);
    if (hmax > 0.0)
    {
        for (auto& v : h) v /= hmax;
        r.onsetHist = h;
        r.hasOnset  = true;
    }

    r.notesPerBar = static_cast<double> (notes.size()) / std::max (1, r.clipBars);
    r.suggestedComplexity = std::clamp (
        static_cast<int> (std::llround ((r.notesPerBar - 1.5) / 6.5 * 100.0)), 0, 100);

    return r;
}

} // namespace kemuri::core
