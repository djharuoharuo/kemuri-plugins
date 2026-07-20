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
    bool                  hasSwing            = false;   // v1.5: スイング検出
    int                   swingPercent        = 50;      // 50=ストレート, 58=Dilla 級
};

// 小節グリッド整列: start をどれだけ引けば小節頭に揃うか（4 beats = 1 小節）。
// 解析窓や .mid の先頭が小節境界にないと全小節判定がズレるため、丸ごと小節ぶんだけ
// 引いて拍内位置（アウフタクト含む）を保存する。
inline double barAlignOffset (double startBeats)
{
    return std::floor (startBeats / 4.0) * 4.0;
}

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
// v1.2: 完全一致でなく Dice 類似度（自己類似行列の対角線検出に相当）。
// 多少の変奏（ゴースト1音の追加等）があっても周期を拾う。候補は {1,2,4,8,16}。
inline int detectNoteLoopBars (const std::vector<RawNote>& notes, int clipBars,
                               double threshold = 0.72)
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
    {
        std::sort (v.begin(), v.end());
        v.erase (std::unique (v.begin(), v.end()), v.end());
    }

    // Dice 係数（ソート済み集合の共通要素数から）
    auto dice = [] (const std::vector<std::pair<int, int>>& a,
                    const std::vector<std::pair<int, int>>& b) -> double
    {
        if (a.empty() && b.empty()) return 1.0;
        if (a.empty() || b.empty()) return 0.0;
        size_t i = 0, j = 0, common = 0;
        while (i < a.size() && j < b.size())
        {
            if (a[i] == b[j])      { ++common; ++i; ++j; }
            else if (a[i] < b[j])  ++i;
            else                   ++j;
        }
        return 2.0 * static_cast<double> (common)
               / static_cast<double> (a.size() + b.size());
    };

    for (int p : { 1, 2, 4, 8, 16 })
    {
        if (p >= clipBars) continue;
        double sum = 0.0;
        int    cnt = 0;
        for (int b = p; b < clipBars; ++b)
        {
            sum += dice (fp[static_cast<size_t> (b)], fp[static_cast<size_t> (b - p)]);
            ++cnt;
        }
        if (cnt > 0 && sum / cnt >= threshold)
            return p;
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

    // pitch-class ヒストグラム（v1.5: 音価重み — TKP 系プロファイルの標準）＋
    // 最終オンセット小節。クリップ長はオンセット基準: サスティンが次の小節へ
    // 食み出しても小節数を膨らませない（「8小節ループが9小節」誤検出の根治）。
    std::array<double, 12> hist {};
    int lastOnsetBar = 0;
    for (const auto& n : notes)
    {
        hist[static_cast<size_t> (((n.pitch % 12) + 12) % 12)] += std::max (0.1, n.duration);
        lastOnsetBar = std::max (lastOnsetBar,
                                 static_cast<int> (std::floor (n.start / 4.0 + 1e-9)));
    }

    // v1.2: Temperley-Kostka-Payne プロファイル（K-S より高精度）
    const auto key = detectKeyTemperley (hist);
    r.keyRoot = key.root;
    r.keyMode = key.mode;

    r.clipBars = lastOnsetBar + 1;
    const double clipLenBeats = r.clipBars * 4.0;

    // v1.2: Viterbi 平滑化（フラッピング抑制 + キーのダイアトニック事前分布）
    r.progBar     = detectProgressionViterbi (notes, clipLenBeats, 4, r.keyRoot, r.keyMode);
    r.progHalfBar = detectProgressionViterbi (notes, clipLenBeats, 2, r.keyRoot, r.keyMode);
    // ループ長はノート内容ベース（Dice 類似度）とコード周期の小さい方。
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

    // ── スイング推定（v1.5）────────────────────────────────────────
    // MPC 系 16 分スイングは奇数 16 分（拍内 0.25 / 0.75）を遅らせる。
    // 各オンセットの半拍内位置 frac をとり、オフビート 16 分域 [0.15, 0.42] の
    // サンプルから swing% = frac / 0.5 * 100 の中央値を推定する。
    // クオンタイズ済み（frac=0.25）は 50% → スイングなし扱い。
    {
        std::vector<double> samples;
        for (const auto& n : notes)
        {
            double frac = std::fmod (n.start, 0.5);
            if (frac < 0.0) frac += 0.5;
            if (frac >= 0.15 && frac <= 0.42)
                samples.push_back (frac / 0.5 * 100.0);
        }
        if (samples.size() >= 4)
        {
            std::nth_element (samples.begin(),
                              samples.begin() + static_cast<long> (samples.size() / 2),
                              samples.end());
            const double median = samples[samples.size() / 2];
            if (median >= 52.0)   // 2% 未満の遅れはノイズ扱い
            {
                r.swingPercent = std::clamp (static_cast<int> (std::lround (median)), 50, 66);
                r.hasSwing     = true;
            }
        }
    }

    return r;
}

} // namespace kemuri::core
