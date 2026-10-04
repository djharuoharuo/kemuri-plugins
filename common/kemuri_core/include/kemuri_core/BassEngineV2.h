#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "AudioAnalyzer.h"
#include "DrumAnalyzer.h"
#include "MusicTheory.h"
#include "Rng.h"
#include "Types.h"

// ── kemuriBass v2.1 生成エンジン（ブームバップ系スタイル）──────────────────
// 90 年代ヒップホップのベースの基本だけで組む。v2.0 の「プロデューサーの手癖で音を動かす」
// （ルート→♭7→5 の歩き・サンプルのベースを 8 分でなぞる等）は、1 小節 4〜5 音で音程が跳ね、
// ヒップホップのベースに聞こえなかったため廃止した（ユーザーの実機確認, 2026-10-04）。
//  1. ベースはキックと一緒に鳴る（あなたのキック。8 分以内に続くキックは 1 音にまとめる）
//  2. 音はその時のネタの和音のルート。和音が変わる所では必ず弾き直す
//  3. 1 小節 4 音まで。1 ループ分を決めたら完全に繰り返し、変化は 4 小節ごとのフレーズの最後だけ
//  4. 音域は E1..D#2 の 1 オクターブに固定（ルートごとに置き場所が決まる → 跳ねない・高い音が出ない）
// プロデューサーの違いは「音の長さ」「弾き直しの数」「フレーズの最後」だけ:
//   Premier : キックごとに短く切る（Mass Appeal 型）か短めに伸ばす / 最後は抜いて空ける
//   Pete    : レガートでつなぐ / 最後は次のルートへ下から 1 音で入る
//   9th     : 1 拍目と和音の変化だけで長く伸ばす / 4 小節ごとに 2 音で歩いてつなぐ
//   Dilla   : キックに後ろノリ（毎ループ同じズレ）/ 長短交互 / 最後は半音下から 1 音
// Complexity を上げた時だけ、同じ和音の弾き直しで下の 5 度・♭7 へ動く（35 以下は動かない）。
namespace kemuri::core
{

enum class Producer { Premier, Pete, Dilla, Ninth };

inline const char* producerName (Producer p)
{
    switch (p)
    {
        case Producer::Premier: return "Premier";
        case Producer::Pete:    return "Pete Rock";
        case Producer::Dilla:   return "J Dilla";
        case Producer::Ninth:   return "9th Wonder";
    }
    return "";
}

struct V2Config
{
    int style      = 0;    // 0 Mix / 1 Premier / 2 Dilla / 3 9th / 4 Pete
    int bars       = 4;    // 要求小節数（4/8/16）
    int complexity = 30;   // 0-100: 同じ和音の弾き直しで 5 度・♭7 へ動く量（35 以下は動かない）
    int fill       = 20;   // 0-100: フレーズの最後の変化の量
    int keyRoot    = 0;    // ネタ未解析時のキー
    int keyMode    = 1;
    const HarmonyAnalysis* harmony = nullptr;   // ok なら使う
    const KickAnalysis*    kick    = nullptr;   // ok なら使う
};

struct V2Result
{
    std::vector<OutNote> notes;
    int         bars = 4;            // 実際の小節数（ループ長に合わせて自動延長することがある）
    Producer    producer = Producer::Premier;
    std::string approach;            // UI 表示用（例: "キックに乗せる / ルート / 短く切る"）
    bool        usedUserKick   = false;
    bool        usedSampleBass = false;   // v2.1 では常に false（ルートのみ）
};

namespace v2detail
{
    inline constexpr int kLow = 28, kHigh = 40;   // E1..E2
    inline constexpr int kMaxPerBar = 4;

    // ルートの置き場所: E1..D#2 の 1 オクターブに固定
    inline int rootPitch (int pc) { return kLow + ((pc - 4) % 12 + 12) % 12; }

    // ルートから semis 下（下の 5 度 = 5、下の ♭7 = 2）。音域を外れるならオクターブ上
    inline int below (int rootP, int semis)
    {
        int p = rootP - semis;
        if (p < kLow) p += 12;
        return std::min (p, kHigh);
    }

    // 目標音へ semis 下から入る（音域を外れるなら上から）
    inline int approachTo (int target, int semis)
    {
        return (target - semis >= kLow) ? target - semis : std::min (kHigh, target + semis);
    }

    // 定番キック（ドラム入力が無いとき）。出典は各コメント。
    inline std::vector<std::vector<double>> fallbackKicks (Producer p)
    {
        switch (p)
        {
            case Producer::Premier: return { { 0.0, 2.5 },            // N.Y. State of Mind: 1 + 3 拍目の裏
                                             { 0.0, 1.5, 2.5 },       // Mass Appeal: 1 + 2・3 拍目の裏
                                             { 0.0, 1.5, 2.0 } };     // 定番: 1, 2 の裏, 3
            case Producer::Pete:    return { { 0.0, 1.5, 2.5 }, { 0.0, 0.5, 2.5 } };
            case Producer::Dilla:   return { { 0.0, 1.75, 2.5 }, { 0.0, 0.75, 2.5 } };   // 付点 8 分系
            case Producer::Ninth:   return { { 0.0, 1.5, 2.5 }, { 0.0, 2.5 } };
        }
        return { { 0.0, 2.0 } };
    }
} // namespace v2detail

inline V2Result buildBassV2 (const V2Config& cfg, Rng& rng)
{
    using namespace v2detail;
    V2Result res;

    // プロデューサー（Mix は生成ごとに 1 人）
    switch (cfg.style)
    {
        case 1: res.producer = Producer::Premier; break;
        case 2: res.producer = Producer::Dilla;   break;
        case 3: res.producer = Producer::Ninth;   break;
        case 4: res.producer = Producer::Pete;    break;
        default:
        {
            static constexpr std::array<Producer, 4> all { Producer::Premier, Producer::Pete, Producer::Dilla, Producer::Ninth };
            res.producer = all[static_cast<size_t> (rng.next() * 4.0) % 4];
        }
    }
    const Producer P = res.producer;
    const double c = std::clamp (cfg.complexity / 100.0, 0.0, 1.0);
    const double f = std::clamp (cfg.fill / 100.0, 0.0, 1.0);

    const bool hasH = cfg.harmony != nullptr && cfg.harmony->ok && cfg.harmony->loopBars > 0;
    const bool hasK = cfg.kick != nullptr && cfg.kick->ok && cfg.kick->loopBars > 0;
    const int  Lh = hasH ? cfg.harmony->loopBars : 1;
    const int  Lk = hasK ? cfg.kick->loopBars : 1;
    const int  Lloop = std::max (Lh, Lk);
    res.bars = std::min (16, std::max (cfg.bars, Lloop));   // ループ長の倍数（すべて 2 の冪）
    res.usedUserKick = hasK;

    // その時点のネタの和音のルート（ループ内位置で引く。2 拍単位）
    auto rootAt = [&] (int bar, double beat) -> int
    {
        if (! hasH) return cfg.keyRoot;
        const int lb = ((bar % Lh) + Lh) % Lh;
        return cfg.harmony->halfBar[static_cast<size_t> (lb * 2 + (beat >= 2.0 ? 1 : 0))].root;
    };

    // ── 生成ごとの選択（音の長さ・ドラム無し時の定番キック）
    const auto fbKicks = fallbackKicks (P);
    const auto& fbKick = fbKicks[static_cast<size_t> (rng.next() * fbKicks.size()) % fbKicks.size()];

    enum class Len { Short, Medium, Legato, Sustain, LongShort };
    Len len = Len::Legato;
    switch (P)
    {
        case Producer::Premier:
            len = rng.next() < 0.65 ? Len::Short : Len::Medium;
            res.approach = len == Len::Short ? "キックに乗せる / ルート / 短く切る" : "キックに乗せる / ルート / 短めに伸ばす";
            break;
        case Producer::Pete:
            len = Len::Legato;
            res.approach = "キックに乗せる / ルート / レガート";
            break;
        case Producer::Ninth:
            len = Len::Sustain;
            res.approach = "1拍目と和音の変化だけ / 長く伸ばす";
            break;
        case Producer::Dilla:
            len = Len::LongShort;
            res.approach = "キックに乗せる / ルート / 後ろノリ / 長短交互";
            break;
    }
    const double motionProb = std::max (0.0, c - 0.35) * 1.2;   // Complexity 35 以下は 0
    if (motionProb > 0.0) res.approach += " / 5度・♭7へ動く";

    // Dilla の後ろノリ: ループ内の打点ごとに固定のズレ（毎ループ同じ, 〜32 分 1 つ弱）
    std::array<double, 64> dillaLate {};
    for (auto& v : dillaLate) v = (P == Producer::Dilla) ? 0.03 + rng.next() * 0.05 : 0.0;

    // ── 1 ループ分（Lloop 小節）を決定
    struct Hit { double pos; int pitch; bool must; bool approach = false; };
    std::vector<std::vector<Hit>> loopHits (static_cast<size_t> (Lloop));
    for (int lb = 0; lb < Lloop; ++lb)
    {
        std::vector<double> kicks = hasK ? cfg.kick->bars[static_cast<size_t> (lb % Lk)] : fbKick;
        std::sort (kicks.begin(), kicks.end());

        const int  r0 = rootAt (lb, 0.0), r2 = rootAt (lb, 2.0);
        const bool midChange = (r0 != r2);

        // 候補: 1 拍目（必須）+ 和音の変化点（必須）+ キック
        struct Cand { double pos; bool must; bool change; };
        std::vector<Cand> cand { { 0.0, true, false } };
        if (midChange)
        {
            // 新しいルートへはキックで移る: 2 拍目の裏の 16 分（食い）〜 4 拍目頭までのキックのうち
            // 3 拍目に最も近いもの。キックが無ければ 3 拍目頭（Dilla は 16 分前に食う）
            double at = (P == Producer::Dilla) ? 1.75 : 2.0, bestD = 1e9;
            for (double k : kicks)
                if (k >= 1.74 && k <= 3.0 && std::abs (k - 2.0) < bestD) { bestD = std::abs (k - 2.0); at = k; }
            cand.push_back ({ at, true, true });
        }
        if (P == Producer::Ninth)
        {
            // 9th: 和音が変わらない小節だけ、後半最初のキックで 1 回弾き直す
            if (! midChange)
                for (double k : kicks) if (k >= 2.0 && k <= 3.5) { cand.push_back ({ k, false, false }); break; }
        }
        else
        {
            for (double k : kicks) if (k > 0.2) cand.push_back ({ k, false, false });
        }
        std::stable_sort (cand.begin(), cand.end(), [] (const Cand& a, const Cand& b) { return a.pos < b.pos; });

        // 同じ位置（和音の変化点に選んだキック）は 1 つに、8 分以内に続く打点も 1 つに（和音の変化点を優先）
        std::vector<Cand> kept;
        for (const auto& x : cand)
        {
            if (! kept.empty() && std::abs (x.pos - kept.back().pos) < 1e-6)
            {
                kept.back().must   = kept.back().must   || x.must;
                kept.back().change = kept.back().change || x.change;
                continue;
            }
            if (! kept.empty() && x.pos - kept.back().pos < 0.55)
            {
                if (x.must && ! kept.back().must) kept.back() = x;
                continue;
            }
            kept.push_back (x);
        }

        // 1 小節 4 音まで（必須を残し、後ろの打点から削る）
        while (kept.size() > static_cast<size_t> (kMaxPerBar))
        {
            auto it = std::find_if (kept.rbegin(), kept.rend(), [] (const Cand& x) { return ! x.must; });
            if (it == kept.rend()) break;
            kept.erase (std::next (it).base());
        }

        // 音高: ルート。Complexity が高い時だけ、同じ和音の弾き直しで下の 5 度・♭7
        auto& hits = loopHits[static_cast<size_t> (lb)];
        for (const auto& x : kept)
        {
            const int root = x.change ? r2 : rootAt (lb, x.pos);
            int pitch = rootPitch (root);
            if (! x.must && motionProb > 0.0 && rng.next() < motionProb)
                pitch = below (pitch, rng.next() < 0.6 ? 5 : 2);
            hits.push_back ({ x.pos, pitch, x.must });
        }
    }

    // ── タイル + フレーズの最後（4 小節ごと・最終小節）だけ変化 → ノート化
    for (int bar = 0; bar < res.bars; ++bar)
    {
        std::vector<Hit> hits = loopHits[static_cast<size_t> (bar % Lloop)];
        const bool phraseEnd = (bar % 4 == 3) || (bar == res.bars - 1);

        if (phraseEnd)
        {
            const int nextRootP = rootPitch (rootAt (bar + 1, 0.0));
            auto dropFrom = [&hits] (double from)
            {
                hits.erase (std::remove_if (hits.begin(), hits.end(),
                                            [from] (const Hit& h) { return h.pos >= from && h.pos > 0.0; }),
                            hits.end());
            };
            auto keepAtMost = [&hits] (size_t n)
            {
                while (hits.size() > n)
                {
                    auto it = std::find_if (hits.rbegin(), hits.rend(), [] (const Hit& h) { return ! h.must; });
                    if (it == hits.rend()) break;
                    hits.erase (std::next (it).base());
                }
            };
            switch (P)
            {
                case Producer::Premier:
                    if (rng.next() < 0.15 + f * 0.6) dropFrom (2.9);              // 抜いて空ける
                    break;
                case Producer::Pete:
                    if (rng.next() < 0.25 + f * 0.6)
                    {
                        dropFrom (3.25);
                        keepAtMost (static_cast<size_t> (kMaxPerBar - 1));
                        hits.push_back ({ 3.5, approachTo (nextRootP, rng.next() < 0.5 ? 1 : 2), false, true });
                    }
                    break;
                case Producer::Ninth:
                    if (rng.next() < 0.4 + f * 0.6)
                    {
                        // 2 音で歩いて次のルートへ（3 半音下 → 1〜2 半音下 → ルート）
                        dropFrom (2.9);
                        const int a2 = approachTo (nextRootP, rng.next() < 0.5 ? 1 : 2);
                        const int a1 = approachTo (nextRootP, 3);
                        hits.push_back ({ 3.0, a1, false, true });
                        hits.push_back ({ 3.5, a2, false, true });
                    }
                    break;
                case Producer::Dilla:
                    if (rng.next() < 0.15 + f * 0.6)
                    {
                        dropFrom (3.25);
                        keepAtMost (static_cast<size_t> (kMaxPerBar - 1));
                        hits.push_back ({ 3.5, approachTo (nextRootP, 1), false, true });   // 半音下から
                    }
                    break;
            }
        }

        for (size_t i = 0; i < hits.size(); ++i)
        {
            const double start = hits[i].pos;
            const double next  = (i + 1 < hits.size()) ? hits[i + 1].pos : 4.0;   // 次の小節は必ず 1 拍目から
            const double gap   = std::max (0.1, next - start);

            double dur;
            if (hits[i].approach) dur = std::min (0.45, gap * 0.9);
            else switch (len)
            {
                case Len::Short:     dur = std::min (0.4, gap * 0.8);  break;   // 短いミュート
                case Len::Medium:    dur = std::min (1.0, gap * 0.7);  break;   // 短めに伸ばす
                case Len::Legato:    dur = gap * 0.95;                  break;   // つなげる
                case Len::Sustain:   dur = gap * 0.98;                  break;   // 伸ばし切る
                case Len::LongShort: dur = (i % 2 == 0) ? gap * 0.9 : std::min (0.3, gap * 0.8); break;
            }

            double pos = start;
            if (P == Producer::Dilla && pos > 0.01)
                pos += dillaLate[static_cast<size_t> ((bar % Lloop) * 8 + static_cast<int> (i)) % dillaLate.size()];
            dur = std::max (0.08, std::min (dur, start + gap - pos - 0.02));

            res.notes.push_back ({ hits[i].pitch, bar * 4.0 + pos, dur, 127 });
        }
    }
    return res;
}

} // namespace kemuri::core
