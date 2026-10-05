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

// ── kemuriBass v2.2 生成エンジン（ブームバップ系スタイル）──────────────────
// 90 年代ヒップホップのベースの基本で組む。v2.0 の「プロデューサーの手癖で音を動かす」
// （ルート→♭7→5 の歩き・サンプルのベースを 8 分でなぞる等）は、1 小節 4〜5 音で音程が跳ね、
// ヒップホップのベースに聞こえなかったため廃止した（ユーザーの実機確認, 2026-10-04）。
//  1. 土台はキック。ベースとキックの関係は 4 通り（v2.2 で 2〜4 を追加）:
//       (1) キックと一緒に鳴る
//       (2) キックの上で伸ばしたまま（弾き直さない）
//       (3) キックとキックの間の裏拍に入る（ネタのアクセントがある所を選ぶ）
//       (4) 和音の変化を 8 分（Dilla は 16 分）食って先に移り、変化点のキックの上は伸ばす
//  2. 音はその時のネタの和音のルート（食う音は次の和音のルート）
//  3. 1 小節 4 音まで。1 ループ分を決めたら完全に繰り返し、変化は 4 小節ごとのフレーズの最後だけ
//  4. 音域は E1..D#2 の 1 オクターブに固定（ルートごとに置き場所が決まる → 跳ねない・高い音が出ない）
// プロデューサーの違いは「音の長さ」「弾き直しの数」「キックから外す量」「フレーズの最後」:
//   Premier : ほぼキックと一緒、短く切る（Mass Appeal 型）か短めに伸ばす / 食いは 16 分のピックアップ
//             （1 拍目は打ち直す）/ 最後は抜いて空ける
//   Pete    : レガート / 和音の変化をよく食う / 最後は次のルートへ下から 1 音
//   9th     : 1 拍目と和音の変化だけで長く伸ばす（キックの上は伸ばしたまま）/ 4 小節ごとに 2 音で歩く
//   Dilla   : キックの間に 16 分で入る・16 分食う / 後ろノリ（毎ループ同じズレ）/ 長短交互
// Complexity: 0 = すべてキックの上、30（既定）= プロデューサーなりにキックから外す、上げるほど外す。
// 35 を超えると同じ和音の弾き直しで下の 5 度・♭7 へも動く。
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
    int complexity = 30;   // 0-100: キックから外す量（0 = すべてキックの上）、35 超で 5 度・♭7 の動き
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
    bool        usedSampleBass = false;   // v2.1 以降は常に false（ルートのみ）
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

    // キーの音階（メジャー / ナチュラル・マイナー）で target から steps 段下の音（音域を外れるなら上）
    inline int scaleStep (int target, int keyRoot, bool minor, int steps)
    {
        static constexpr std::array<int, 7> maj { 0, 2, 4, 5, 7, 9, 11 }, mnr { 0, 2, 3, 5, 7, 8, 10 };
        const auto& sc = minor ? mnr : maj;
        auto inScale = [&] (int m)
        {
            const int d = ((m - keyRoot) % 12 + 12) % 12;
            return std::find (sc.begin(), sc.end(), d) != sc.end();
        };
        int q = target, n = 0;
        while (n < steps && q > kLow) { --q; if (inScale (q)) ++n; }
        if (n == steps) return q;
        q = target; n = 0;
        while (n < steps && q < kHigh) { ++q; if (inScale (q)) ++n; }
        return q;
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

    // 打点の種類（多すぎる時に削る順: キックの弾き直し → キックから外した音。必須は削らない）
    enum class Kind { Must, Off, Kick };
    inline int priority (Kind k) { return k == Kind::Must ? 2 : (k == Kind::Off ? 1 : 0); }
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
    // ネタのアクセント（16 分ごとのオンセットの強さ 0..1）
    auto netaAccent = [&] (double pos) -> double
    {
        if (! hasH || ! cfg.harmony->hasOnset) return 0.0;
        const int st = std::clamp (static_cast<int> (std::lround (pos * 4.0)), 0, 15);
        return cfg.harmony->onsetHist[static_cast<size_t> (st)];
    };

    // ── 生成ごとの選択（音の長さ・キックから外す量・ドラム無し時の定番キック）
    const auto fbKicks = fallbackKicks (P);
    const auto& fbKick = fbKicks[static_cast<size_t> (rng.next() * fbKicks.size()) % fbKicks.size()];

    enum class Len { Short, Medium, Legato, Sustain, LongShort };
    Len len = Len::Legato;
    double pAnt = 0.0, pGap = 0.0;   // 和音の変化を食う / キックの間に入る（Complexity 30 での確率）
    switch (P)
    {
        case Producer::Premier:
            len = rng.next() < 0.65 ? Len::Short : Len::Medium;
            pAnt = 0.15; pGap = 0.1;
            res.approach = len == Len::Short ? "キックに乗せる / ルート / 短く切る" : "キックに乗せる / ルート / 短めに伸ばす";
            break;
        case Producer::Pete:
            len = Len::Legato;
            pAnt = 0.6; pGap = 0.35;
            res.approach = "キックに乗せる / ルート / レガート";
            break;
        case Producer::Ninth:
            len = Len::Sustain;
            pAnt = 0.5; pGap = 0.0;
            res.approach = "1拍目と和音の変化だけ / 長く伸ばす";
            break;
        case Producer::Dilla:
            len = Len::LongShort;
            pAnt = 0.5; pGap = 0.6;
            res.approach = "キックに乗せる / ルート / 後ろノリ / 長短交互";
            break;
    }
    const double offScale = std::min (1.6, c / 0.3);   // Complexity 0 = すべてキックの上
    pAnt = std::min (1.0, pAnt * offScale);
    pGap = std::min (1.0, pGap * offScale);
    // 食った音を次の拍頭まで伸ばす（Premier は 16 分のピックアップで、拍頭は打ち直す）
    const bool tieAnticipation = (P != Producer::Premier);
    const double motionProb = std::max (0.0, c - 0.35) * 1.2;   // Complexity 35 以下は 0
    const int  scaleRoot  = hasH ? cfg.harmony->keyRoot : cfg.keyRoot;   // 経過音はキーの音階で歩く
    const bool scaleMinor = (hasH ? cfg.harmony->keyMode : cfg.keyMode) == 1;
    bool usedAnt = false, usedGap = false;

    // Dilla の後ろノリ: ループ内の打点ごとに固定のズレ（毎ループ同じ, 〜32 分 1 つ弱）
    std::array<double, 64> dillaLate {};
    for (auto& v : dillaLate) v = (P == Producer::Dilla) ? 0.03 + rng.next() * 0.05 : 0.0;

    // ── 1 ループ分（Lloop 小節）を決定
    // anticip = 次の和音へ食った音（小節末なら次の小節の 1 拍目とつながる）
    // held = 食った音（小節の途中・小節末とも）。キックの上で伸ばす
    struct Hit { double pos; int pitch; Kind kind; bool approach = false; bool anticip = false; bool held = false; };
    std::vector<std::vector<Hit>> loopHits (static_cast<size_t> (Lloop));
    for (int lb = 0; lb < Lloop; ++lb)
    {
        std::vector<double> kicks = hasK ? cfg.kick->bars[static_cast<size_t> (lb % Lk)] : fbKick;
        std::sort (kicks.begin(), kicks.end());
        auto kickNear = [&kicks] (double p, double tol)
        {
            return std::any_of (kicks.begin(), kicks.end(), [p, tol] (double k) { return std::abs (k - p) <= tol; });
        };

        const int  r0 = rootAt (lb, 0.0), r2 = rootAt (lb, 2.0), rNext = rootAt (lb + 1, 0.0);
        const bool midChange = (r0 != r2);
        const bool barChange = (r2 != rNext);   // 次の小節頭で和音が変わる

        struct Cand { double pos; Kind kind; bool change; bool anticip; };
        std::vector<Cand> cand { { 0.0, Kind::Must, false, false } };

        // 小節途中の和音の変化: 新しいルートへはキックで移る（2 拍目の裏の 16 分〜4 拍目頭の
        // キックのうち 3 拍目に最も近いもの、無ければ 3 拍目頭）。確率で 8 分（Dilla 16 分）食う。
        double changeAt = 4.0, antMidAt = -1.0;
        if (midChange)
        {
            changeAt = (P == Producer::Dilla) ? 1.75 : 2.0;
            double bestD = 1e9;
            for (double k : kicks)
                if (k >= 1.74 && k <= 3.0 && std::abs (k - 2.0) < bestD) { bestD = std::abs (k - 2.0); changeAt = k; }
            const double a = (P == Producer::Dilla || P == Producer::Premier) ? 1.75 : 1.5;
            if (a < changeAt - 0.2 && ! kickNear (a, 0.2) && rng.next() < pAnt)
            {
                changeAt = a;
                antMidAt = a;
                usedAnt  = true;
            }
            cand.push_back ({ changeAt, Kind::Must, true, antMidAt >= 0.0 });
        }
        const auto rootFor = [&] (double p) { return (midChange && p >= changeAt - 1e-9) ? r2 : r0; };

        if (P == Producer::Ninth)
        {
            // 9th: 和音が変わらない小節だけ、後半最初のキックで 1 回弾き直す
            if (! midChange)
                for (double k : kicks) if (k >= 2.0 && k <= 3.5) { cand.push_back ({ k, Kind::Kick, false, false }); break; }
        }
        else
        {
            for (double k : kicks) if (k > 0.2) cand.push_back ({ k, Kind::Kick, false, false });
        }

        // 次の小節頭の和音の変化を食う（4 拍目の裏 = 3.5。Dilla / Premier は 16 分 = 3.75）
        if (barChange && rng.next() < pAnt)
        {
            const double a = (P == Producer::Dilla || P == Producer::Premier) ? 3.75 : 3.5;
            if (! kickNear (a, 0.2))
            {
                cand.push_back ({ a, Kind::Off, false, true });
                usedAnt = true;
            }
        }
        std::stable_sort (cand.begin(), cand.end(), [] (const Cand& x, const Cand& y) { return x.pos < y.pos; });

        // 同じ位置は 1 つに、8 分以内に続く打点も 1 つに（必須 > キックから外した音 > キック）
        std::vector<Cand> kept;
        for (const auto& x : cand)
        {
            if (! kept.empty() && std::abs (x.pos - kept.back().pos) < 1e-6)
            {
                if (priority (x.kind) > priority (kept.back().kind)) kept.back().kind = x.kind;
                kept.back().change  = kept.back().change  || x.change;
                kept.back().anticip = kept.back().anticip || x.anticip;
                continue;
            }
            if (! kept.empty() && x.pos - kept.back().pos < 0.55)
            {
                if (priority (x.kind) > priority (kept.back().kind) && kept.back().kind != Kind::Must) kept.back() = x;
                continue;
            }
            kept.push_back (x);
        }

        // 食った音はキックの上で伸ばす: 食った位置から 1 拍強のキックは弾き直さない（レガート系）
        if (tieAnticipation && antMidAt >= 0.0)
            kept.erase (std::remove_if (kept.begin(), kept.end(), [antMidAt] (const Cand& x)
                        { return x.kind == Kind::Kick && x.pos > antMidAt && x.pos <= antMidAt + 1.1; }), kept.end());

        // 1 小節 4 音まで（キックの弾き直しから、後ろの打点ほど先に削る）
        auto trimTo = [&kept] (size_t n)
        {
            for (int pr = 0; pr <= 1 && kept.size() > n; ++pr)
                for (size_t i = kept.size(); i-- > 0 && kept.size() > n;)
                    if (priority (kept[i].kind) == pr) kept.erase (kept.begin() + static_cast<long> (i));
        };
        trimTo (static_cast<size_t> (kMaxPerBar));

        // キックとキックの間（1.5 拍以上空いた所）の裏拍に 1 音。ネタのアクセントがある所を選ぶ
        if (kept.size() < static_cast<size_t> (kMaxPerBar) && rng.next() < pGap)
        {
            const double minGap = (P == Producer::Dilla) ? 1.25 : 1.5;
            double bestPos = -1.0, bestScore = -1e9;
            for (size_t i = 0; i < kept.size(); ++i)
            {
                const double from = kept[i].pos;
                const double to   = (i + 1 < kept.size()) ? kept[i + 1].pos : 4.0;
                if (to - from < minGap) continue;
                for (double p = 0.25; p < 4.0; p += 0.25)
                {
                    const bool eighthOff = std::abs (std::fmod (p, 1.0) - 0.5) < 1e-6;
                    const bool sixteenth = std::abs (std::fmod (p, 0.5) - 0.25) < 1e-6;
                    if (! eighthOff && ! (sixteenth && P == Producer::Dilla)) continue;   // 裏拍だけ（Dilla は 16 分も）
                    if (p - from < 0.5 || to - p < 0.5 || kickNear (p, 0.2)) continue;
                    // 和音が変わる直前（1 拍以内）に古いルートを打ち直さない（そこは食う場所）
                    if ((barChange && p >= 3.0) || (midChange && p < changeAt && p >= changeAt - 1.0)) continue;
                    const double mid   = (from + to) * 0.5;
                    const double score = netaAccent (p) + (eighthOff ? 0.1 : 0.0) - 0.05 * std::abs (p - mid);
                    if (score > bestScore) { bestScore = score; bestPos = p; }
                }
            }
            if (bestPos >= 0.0)
            {
                kept.push_back ({ bestPos, Kind::Off, false, false });
                std::stable_sort (kept.begin(), kept.end(), [] (const Cand& x, const Cand& y) { return x.pos < y.pos; });
                usedGap = true;
            }
        }

        // 音高: ルート（食った音は次の和音のルート）。Complexity 35 超では同じ和音の弾き直しで
        // 下の 5 度・♭7 にも動く
        auto& hits = loopHits[static_cast<size_t> (lb)];
        for (const auto& x : kept)
        {
            const bool endAnticip = x.anticip && x.pos >= 3.0;
            const int  root  = endAnticip ? rNext : (x.change ? r2 : rootFor (x.pos));
            int        pitch = rootPitch (root);
            if (x.kind != Kind::Must && ! x.anticip && motionProb > 0.0 && rng.next() < motionProb)
                pitch = below (pitch, rng.next() < 0.6 ? 5 : 2);
            hits.push_back ({ x.pos, pitch, x.kind, false, endAnticip, x.anticip });
        }
    }

    // ── タイル + フレーズの最後（4 小節ごと・最終小節）だけ変化
    struct Ev { double at; int pitch; bool approach; bool anticip; bool held; int idx; };
    std::vector<Ev> evs;
    for (int bar = 0; bar < res.bars; ++bar)
    {
        std::vector<Hit> hits = loopHits[static_cast<size_t> (bar % Lloop)];
        const bool phraseEnd = (bar % 4 == 3) || (bar == res.bars - 1);

        auto dropFrom = [&hits] (double from)
        {
            hits.erase (std::remove_if (hits.begin(), hits.end(),
                                        [from] (const Hit& h) { return h.pos >= from && h.pos > 0.0; }),
                        hits.end());
        };
        auto keepAtMost = [&hits] (size_t n)
        {
            for (int pr = 0; pr <= 1 && hits.size() > n; ++pr)
                for (size_t i = hits.size(); i-- > 0 && hits.size() > n;)
                    if (priority (hits[i].kind) == pr) hits.erase (hits.begin() + static_cast<long> (i));
        };

        if (phraseEnd)
        {
            const int nextRootP = rootPitch (rootAt (bar + 1, 0.0));
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
                        const int a = rng.next() < 0.5 ? approachTo (nextRootP, 1)                                  // 半音下から
                                                        : scaleStep (nextRootP, scaleRoot, scaleMinor, 1);          // 音階で 1 つ下から
                        hits.push_back ({ 3.5, a, Kind::Off, true });
                    }
                    break;
                case Producer::Ninth:
                    if (rng.next() < 0.4 + f * 0.6)
                    {
                        // 2 音で歩いて次のルートへ（キーの音階で 2 つ下 → 1 つ下 → ルート）
                        dropFrom (2.9);
                        const int a2 = scaleStep (nextRootP, scaleRoot, scaleMinor, 1);
                        const int a1 = scaleStep (nextRootP, scaleRoot, scaleMinor, 2);
                        hits.push_back ({ 3.0, a1, Kind::Off, true });
                        hits.push_back ({ 3.5, a2, Kind::Off, true });
                    }
                    break;
                case Producer::Dilla:
                    if (rng.next() < 0.15 + f * 0.6)
                    {
                        dropFrom (3.25);
                        keepAtMost (static_cast<size_t> (kMaxPerBar - 1));
                        hits.push_back ({ 3.5, approachTo (nextRootP, 1), Kind::Off, true });   // 半音下から
                    }
                    break;
            }
        }
        // クリップの最後の小節では食わない（ループの頭に戻ると 1 拍目を打ち直すため）
        if (bar == res.bars - 1)
            hits.erase (std::remove_if (hits.begin(), hits.end(), [] (const Hit& h) { return h.anticip; }), hits.end());

        for (size_t i = 0; i < hits.size(); ++i)
        {
            // 前の小節の終わりで食った音と同じ音の 1 拍目は打ち直さない（キックの上で伸ばす）
            if (tieAnticipation && hits[i].pos == 0.0 && ! evs.empty() && evs.back().anticip
                && evs.back().pitch == hits[i].pitch && evs.back().at >= bar * 4.0 - 0.6)
                continue;

            double pos = hits[i].pos;
            if (P == Producer::Dilla && pos > 0.01)
                pos += dillaLate[static_cast<size_t> ((bar % Lloop) * 8 + static_cast<int> (i)) % dillaLate.size()];
            evs.push_back ({ bar * 4.0 + pos, hits[i].pitch, hits[i].approach, hits[i].anticip, hits[i].held, static_cast<int> (i) });
        }
    }

    // ── ノート化（長さは次の打点まで。食った音は小節線をまたいで伸びる）
    const double clipEnd = res.bars * 4.0;
    for (size_t i = 0; i < evs.size(); ++i)
    {
        const double next = (i + 1 < evs.size()) ? evs[i + 1].at : clipEnd;
        const double gap  = std::max (0.1, next - evs[i].at);

        double dur;
        if (evs[i].approach) dur = std::min (0.45, gap * 0.9);
        else switch (len)
        {
            case Len::Short:     dur = std::min (0.4, gap * 0.8);  break;   // 短いミュート
            case Len::Medium:    dur = std::min (1.0, gap * 0.7);  break;   // 短めに伸ばす
            case Len::Legato:    dur = gap * 0.95;                  break;   // つなげる
            case Len::Sustain:   dur = gap * 0.98;                  break;   // 伸ばし切る
            case Len::LongShort: dur = (evs[i].idx % 2 == 0 || evs[i].held) ? gap * 0.9 : std::min (0.3, gap * 0.8); break;
        }
        dur = std::max (0.08, std::min (dur, gap - 0.02));
        res.notes.push_back ({ evs[i].pitch, evs[i].at, dur, 127 });
    }

    if (usedAnt) res.approach += " / 和音の変化を食う";
    if (usedGap) res.approach += " / キックの間にも入る";
    if (motionProb > 0.0) res.approach += " / 5度・♭7へ動く";
    return res;
}

} // namespace kemuri::core
