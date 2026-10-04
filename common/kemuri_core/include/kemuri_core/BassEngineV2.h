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

// ── kemuriBass v2 生成エンジン（ブームバップ系スタイル）──────────────────
// 90 年代の作り方そのものをモデル化する（検証済みリサーチ準拠）:
//  - リズム = あなたのキック（Drum Rack MIDI）。1 拍目は必ず、ベースはキックと一緒に鳴る。
//    ネタの和音が変わる位置には必ず打点を置く。ドラム入力が無いときは実曲由来の定番キック。
//  - 音高 = その瞬間のネタの和音（2 拍単位）と、ネタ自身のベース音（低域を抜いた音程）。
//  - プロデューサーの手癖:
//      Premier : サンプルのベースを verbatim でなぞる / キックのみ / 短いミュート（Mass Appeal）
//      Pete    : サンプルのベースの動きを 8 分でなぞる / レガート / ルート→♭7→6 の歩き（T.R.O.Y.）
//      9th     : サンプルの低域を捨てて自前のルート追従 / 伸ばす / 4 小節ごとに 2 音の経過音
//      Dilla   : 1 拍目 + シンコペーション / 順次進行 / 長短を交互 / 後ろノリ（毎ループ同じズレ）
//  - ループは毎回完全に同じ（1 ループ分の決定を固定してタイル）。変化はフレーズ端だけ。
//  - 音域 E1..G2（MIDI 28..43）、直前の音に最も近いオクターブを選ぶ（跳躍しない）。
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
    int complexity = 30;   // 0-100: キック以外の打点・経過音の量
    int fill       = 20;   // 0-100: フレーズ端の展開の量
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
    std::string approach;            // UI 表示用（例: "サンプルのベースをなぞる / 短く"）
    bool        usedUserKick   = false;
    bool        usedSampleBass = false;
};

namespace v2detail
{
    inline constexpr int kLow = 28, kHigh = 43, kCenter = 33;   // E1..G2、中心 A1

    // ピッチクラスを音域内で直前の音に最も近いオクターブへ
    inline int place (int pc, int prev)
    {
        int best = -1, bestDist = 1000;
        for (int m = kLow; m <= kHigh; ++m)
        {
            if (((m % 12) + 12) % 12 != pc) continue;
            const int d = std::abs (m - (prev > 0 ? prev : kCenter));
            if (d < bestDist) { bestDist = d; best = m; }
        }
        return best;
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

    // ── 和音・サンプルベースの参照（ループ内位置で引く）
    auto chordAt = [&] (int bar, double beat) -> std::pair<int, bool>
    {
        if (! hasH) return { cfg.keyRoot, cfg.keyMode == 1 };
        const auto& h = cfg.harmony->halfBar[static_cast<size_t> ((bar % Lh) * 2 + (beat >= 2.0 ? 1 : 0))];
        return { h.root, h.quality == "min" };
    };
    auto sampleBassAt = [&] (int bar, double beat) -> int
    {
        if (! hasH) return -1;
        const int st = std::clamp (static_cast<int> (std::floor (beat * 4.0 + 1e-6)), 0, 15);
        return cfg.harmony->bassMidi[static_cast<size_t> ((bar % Lh) * 16 + st)];
    };

    // サンプルのベースがどれだけ鳴っているか（なぞる手法が使えるか）
    double voiced = 0.0;
    if (hasH)
    {
        for (int m : cfg.harmony->bassMidi) if (m >= 0) voiced += 1.0;
        voiced /= static_cast<double> (cfg.harmony->bassMidi.size());
    }
    const bool sampleBassUsable = voiced >= 0.25;

    // ── 生成ごとのアプローチ（多様性はここから。ランダムな飾りではない）
    std::vector<std::vector<double>> fbKicks = fallbackKicks (P);
    const auto& fbKick = fbKicks[static_cast<size_t> (rng.next() * fbKicks.size()) % fbKicks.size()];

    bool followSample = false, staccato = false, legato = false, fullClipMotion = false, alternateLen = false;
    bool ninthBounce = false, ninthApproach = false;
    const double roll = rng.next();
    switch (P)
    {
        case Producer::Premier:
            // サンプルの低域が十分 → 50% verbatim / 25% ルートのペダル / 25% Full Clip 型。
            // 低域が無い → ルートのペダル / Full Clip 型（ルート+低い 5 度+下の ♭7）を半々。
            if (sampleBassUsable) { followSample = roll < 0.5; fullClipMotion = roll >= 0.75; }
            else                  { fullClipMotion = roll < 0.5; }
            staccato     = rng.next() < 0.7;                     // Mass Appeal: 全部短いミュート
            res.approach = followSample ? "サンプルのベースをそのまま" : (fullClipMotion ? "ルート+低い5度/下の♭7" : "ルートのペダル");
            res.approach += staccato ? " / 短く切る" : " / 短めに伸ばす";
            break;
        case Producer::Pete:
            followSample = sampleBassUsable && roll < 0.7;
            legato       = true;
            res.approach = followSample ? "サンプルのベースの動きを8分でなぞる / レガート" : "ルート→♭7→5の歩き / レガート";
            break;
        case Producer::Ninth:
            followSample  = false;                               // サンプルの低域は捨てて自前のベース
            legato        = true;
            ninthBounce   = roll >= 0.5 && roll < 0.8;           // ルートと 5 度のバウンス
            ninthApproach = roll >= 0.8;                         // 和音変化の前に ♭7 から入る
            res.approach  = ninthBounce ? "ルートと5度 / 伸ばす / 4小節ごとに経過音"
                          : ninthApproach ? "ルート+変化前の♭7 / 伸ばす / 4小節ごとに経過音"
                                          : "和音のルートを追う / 伸ばす / 4小節ごとに経過音";
            break;
        case Producer::Dilla:
            followSample = sampleBassUsable && roll < 0.6;
            alternateLen = true;
            res.approach = std::string (followSample ? "サンプルのベース+" : "") + "順次進行 / 長短交互 / 後ろノリ";
            break;
    }
    res.usedSampleBass = followSample;

    // Dilla の後ろノリ: ループ内の打点ごとに固定のズレ（毎ループ同じ）
    std::array<double, 64> dillaLate {};
    for (auto& v : dillaLate) v = (P == Producer::Dilla) ? 0.03 + rng.next() * 0.07 : 0.0;   // 〜32 分 1 つ弱

    // ── 1 ループ分（Lloop 小節）を決定
    // towardPc >= 0 の音は「次の音（towardPc）へ順次で解決する経過音」として、
    // 目標音の近くのオクターブに置く。
    struct Hit { double pos; int pc; bool chordChange; int towardPc = -1; };
    std::vector<std::vector<Hit>> loopHits (static_cast<size_t> (Lloop));
    for (int lb = 0; lb < Lloop; ++lb)
    {
        std::vector<double> on = hasK ? cfg.kick->bars[static_cast<size_t> (lb % Lk)] : fbKick;
        std::sort (on.begin(), on.end());

        // 1 拍目は必ず（わずかな前後ズレのキックはそのまま使う）
        if (on.empty() || on.front() > 0.2) on.insert (on.begin(), 0.0);

        // 2 拍目の和音変化には必ず打点（Dilla は 16 分先取り）
        const auto c0 = chordAt (lb, 0.0), c2 = chordAt (lb, 2.0);
        const bool midChange = (c0 != c2);
        if (midChange)
        {
            bool near = false;
            for (double p : on) if (std::abs (p - 2.0) <= 0.26) near = true;
            if (! near) on.push_back (P == Producer::Dilla ? 1.75 : 2.0);
        }

        // プロデューサー別の追加打点（既存の打点から 16 分以上離れた所だけ）
        auto addIfFree = [&on] (double p)
        {
            for (double q : on) if (std::abs (q - p) < 0.3) return;
            on.push_back (p);
        };
        if (P == Producer::Pete)
        {
            // サンプルのベースが 8 分で動く位置をなぞる（＋ Complexity で裏拍）
            for (int e = 0; e < 8; ++e)
            {
                const double p = e * 0.5;
                const int cur = sampleBassAt (lb, p), prv = (e > 0) ? sampleBassAt (lb, p - 0.5) : -1;
                if (followSample && cur >= 0 && cur != prv) addIfFree (p);
                else if (e % 2 == 1 && rng.next() < c * 0.6) addIfFree (p);
            }
        }
        else if (P == Producer::Dilla)
        {
            for (double p : { 0.75, 2.25, 3.25 }) if (rng.next() < 0.15 + c * 0.5) addIfFree (p);
        }
        else if (P == Producer::Ninth)
        {
            if (ninthBounce || rng.next() < c * 0.4) addIfFree (3.0);
            if (ninthApproach && midChange) addIfFree (1.5);   // 和音変化の前に ♭7
        }
        else // Premier: キックのみ。Complexity でまれに次小節頭への 16 分ピックアップ
        {
            if (rng.next() < c * 0.3) addIfFree (3.75);
        }
        std::sort (on.begin(), on.end());

        // 近すぎる打点（ダブルキック等）は 1 つに
        std::vector<double> clean;
        for (double p : on) if (clean.empty() || p - clean.back() >= 0.2) clean.push_back (p);

        // 密度上限（研究: Premier/9th ≈2-4、Dilla 3-7、Pete 中央値 6）
        const size_t cap = (P == Producer::Pete) ? 8 : (P == Producer::Dilla ? 7 : 5);
        if (clean.size() > cap) clean.resize (cap);

        // 音高（ピッチクラス）
        auto& hits = loopHits[static_cast<size_t> (lb)];
        int prevPc = -1;
        for (size_t i = 0; i < clean.size(); ++i)
        {
            const double p = clean[i];
            const auto ch = chordAt (lb, p);
            const int root = ch.first;
            const bool minor = ch.second;
            const int sb = sampleBassAt (lb, p);
            int pc = root;
            const bool atChange = (p == 0.0) || (midChange && std::abs (p - 2.0) <= 0.26);

            if (followSample && sb >= 0)
                pc = ((sb % 12) + 12) % 12;
            else if (! atChange && i > 0)
            {
                // 和音内の動き（拍頭/和音変化点以外）
                if (P == Producer::Premier && fullClipMotion)
                    pc = (root + ((i % 2 == 1) ? 7 : 10)) % 12;                    // 低い 5 度 / 下の ♭7
                else if (P == Producer::Pete)
                {
                    // ルート→♭7→5→♭7（root-7th-5th のジャジーな定番。T.R.O.Y. の Dorian 6 度は
                    // ネタが Aeolian だと濁るため、和音内の音だけで歩く）
                    static constexpr std::array<int, 4> walk { 0, 10, 7, 10 };
                    pc = (root + walk[i % walk.size()]) % 12;
                }
                else if (P == Producer::Dilla)
                {
                    const std::array<int, 4> step { 10, 7, minor ? 3 : 4, 5 };   // ♭7, 5, 3, 4 の順次
                    pc = (root + step[static_cast<size_t> (rng.next() * 4) % 4]) % 12;
                }
                else if (P == Producer::Ninth && p >= 2.9 && p <= 3.1)
                    pc = (root + 7) % 12;                                           // ルート-5 度のバウンス
                else if (P == Producer::Ninth && ninthApproach && p >= 1.4 && p <= 1.6)
                    pc = (chordAt (lb, 2.0).first + 10) % 12;                       // 次の和音の ♭7 から入る
            }
            hits.push_back ({ p, pc, atChange });
            prevPc = pc;
        }
        (void) prevPc;
    }

    // ── タイル + フレーズ端の展開 → ノート化
    int prevPitch = -1;
    for (int bar = 0; bar < res.bars; ++bar)
    {
        std::vector<Hit> hits = loopHits[static_cast<size_t> (bar % Lloop)];
        const bool phraseEnd = (bar % 4 == 3) || (bar == res.bars - 1);

        if (phraseEnd)
        {
            const auto nextCh = chordAt (bar + 1, 0.0);
            const int nextRoot = nextCh.first;
            double devProb = (P == Producer::Premier) ? f * 0.5 : (P == Producer::Ninth ? 0.6 + f * 0.4 : f);
            if (bar == res.bars - 1) devProb = std::min (1.0, devProb + 0.2);
            if (rng.next() < devProb)
            {
                const bool subtractive = (P == Producer::Premier || P == Producer::Dilla) && rng.next() < 0.5;
                if (subtractive)
                {
                    // 3 拍目以降を抜いて空白で締める（ドロップアウト）
                    hits.erase (std::remove_if (hits.begin(), hits.end(), [] (const Hit& h) { return h.pos >= 2.9; }), hits.end());
                }
                else
                {
                    // 次の小節のルートへ 2 音でつなぐ（9th の 2 音トランジション / Pete の歩き）
                    hits.erase (std::remove_if (hits.begin(), hits.end(), [] (const Hit& h) { return h.pos >= 2.9; }), hits.end());
                    const int stepIn = (rng.next() < 0.5) ? 2 : 1;     // 全音下 or 半音下から
                    hits.push_back ({ 3.0, (nextRoot + 7) % 12, false, nextRoot });
                    hits.push_back ({ 3.5, ((nextRoot - stepIn) % 12 + 12) % 12, false, nextRoot });
                }
            }
        }

        for (size_t i = 0; i < hits.size(); ++i)
        {
            const double start = hits[i].pos;
            const double next  = (i + 1 < hits.size()) ? hits[i + 1].pos : 4.0 + (loopHits[static_cast<size_t> ((bar + 1) % Lloop)].empty() ? 0.0 : loopHits[static_cast<size_t> ((bar + 1) % Lloop)].front().pos);
            const double gap   = std::max (0.1, next - start);

            double dur;
            if (staccato)          dur = std::min (0.35, gap * 0.85);                    // 短いミュート
            else if (legato)       dur = gap * 0.92;                                     // つなげる
            else if (alternateLen) dur = (i % 2 == 0) ? gap * 0.9 : std::min (0.22, gap * 0.8);
            else                   dur = std::min (1.5, gap * 0.7);                      // 短めに伸ばす
            dur = std::max (0.08, std::min (dur, gap - 0.02));

            double pos = start;
            if (P == Producer::Dilla && pos > 0.01) pos += dillaLate[static_cast<size_t> ((bar % Lloop) * 8 + i) % dillaLate.size()];

            int pitch;
            if (hits[i].towardPc >= 0)
            {
                // 経過音: 目標（次の小節頭）の置き場所を先に決め、その近くに置く
                const int target = place (hits[i].towardPc, prevPitch);
                pitch = place (hits[i].pc, target);
                // 目標の下から入る（approach-from-below）を優先
                if (pitch > target && pitch - 12 >= kLow) pitch -= 12;
            }
            else
                pitch = place (hits[i].pc, prevPitch);
            if (pitch < 0) continue;
            prevPitch = pitch;
            res.notes.push_back ({ pitch, bar * 4.0 + pos, dur, 127 });
        }
    }
    return res;
}

} // namespace kemuri::core
