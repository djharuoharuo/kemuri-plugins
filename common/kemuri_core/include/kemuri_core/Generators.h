#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "GrooveEngine.h"
#include "KeyContext.h"
#include "MarkovSelector.h"
#include "MusicTheory.h"
#include "Pattern.h"
#include "PatternBank.h"
#include "PatternLibrary.h"
#include "PhrasePlanner.h"
#include "Rng.h"
#include "Types.h"
#include "VariationEngine.h"

namespace kemuri::core
{

// 生成 1 回ぶんのパラメータ。正本 JS: g_style / g_complexity / g_fill / g_bars /
// g_root / g_mode と解析由来の progression / onsetHist。
struct GenerateConfig
{
    int  style      = 0;   // 0-7
    int  bars       = 4;   // 4 / 8 / 16
    int  complexity = 30;  // 0-100
    int  fill       = 20;  // 0-100
    int  root       = 0;   // 0-11
    int  mode       = 0;   // 0=major, 1=minor

    bool                  useProgression = false;
    std::vector<ChordSeg> progBar;
    std::vector<ChordSeg> progHalfBar;
    int                   loopBars = 0;

    std::optional<std::array<double, 16>> onsetHist;   // topline call & response
    const PatternBank*                    bank = nullptr;   // 学習パターン束（null=既定）
    int                                   swingOverride = 0; // v1.5: 検出スイング%（0=なし）
};

// cfg.bank が null のときのハードコード既定束。
inline const PatternBank& defaultBank()
{
    static const PatternBank bank = makeDefaultBank();
    return bank;
}

// スタイルに対応するプロデューサ束を返す（Boom-Bap Mix はランダムに 1 つ）。
inline const ProducerLib& producerForStyle (const PatternBank& bank, int style, Rng& rng)
{
    switch (style)
    {
        case 1:  return bank.premier;
        case 2:  return bank.dilla;
        case 3:  return bank.ninth;
        case 4:  return bank.pete;
        default:
        {
            const std::array<const ProducerLib*, 5> libs {
                &bank.premier, &bank.dilla, &bank.ninth, &bank.pete, &bank.pool };
            const int n = bank.pool.patterns.empty() ? 4 : 5;
            return *libs[static_cast<size_t> (rng.next() * n)];
        }
    }
}

// 生成の可変状態（選択器・乱数・解析文脈）をまとめて渡す。
struct GenContext
{
    MarkovSelector&                              selector;
    Rng&                                         rng;
    const std::optional<std::array<double, 16>>& onsetHist;
    const LearnedGroove*                         groove;
};

// 60% コードトーン / 40% スケールトーン（comp が高いほどスケール寄り）。
// 正本 JS: chordOrScale。
inline int chordOrScale (int root, const KeyContext& ctx, double comp, Rng& rng)
{
    const double chordProb = 0.7 - comp * 0.3;
    if (rng.next() < chordProb)
    {
        const int idx = 1 + static_cast<int> (rng.next() * (ctx.chord.size() - 1));
        return snapNear (root + ctx.chord[static_cast<size_t> (idx)], ctx.midAnchor);
    }
    const int sIdx = 1 + static_cast<int> (rng.next() * (ctx.scale.size() - 1));
    return snapNear (root + ctx.scale[static_cast<size_t> (sIdx)], ctx.midAnchor);
}

// ── Pattern-based styles (Premier / Dilla / Ninth / Pete / Boom-Bap) ──
inline std::vector<OutNote> genFromLib (const std::vector<Pattern>& lib,
                                        const BarParams& p, GenContext& gc)
{
    const Pattern& pat = gc.selector.pickPattern (lib, nullptr, gc.rng, gc.onsetHist);
    return applyVariations (pat, p, gc.rng, gc.groove);
}

inline std::vector<OutNote> genBoomBap (const BarParams& p, GenContext& gc)
{
    const std::array<const std::vector<Pattern>*, 4> libs {
        &premierPatterns(), &dillaPatterns(), &ninthPatterns(), &peteRockPatterns() };
    const auto* lib = libs[static_cast<size_t> (gc.rng.next() * libs.size())];
    return genFromLib (*lib, p, gc);
}

// ── Style 5: Soul-Jazz (walking bass, half-bar harmony) ──────────────
inline std::vector<OutNote> genSoulJazz (const BarParams& p, Rng& rng)
{
    std::vector<OutNote> notes;
    const auto& ctxA = p.ctx;
    const auto& ctxB = p.hasMid ? p.ctxMid : p.ctx;
    const auto& nctx = p.nextCtx;
    const double comp = p.compFactor;

    const int rootA = snapNear (ctxA.midAnchor, ctxA.midAnchor);
    const int rootB = snapNear (ctxB.midAnchor, ctxA.midAnchor);
    const int rootN = snapNear (nctx.midAnchor, ctxA.midAnchor);

    const int beat1 = (rng.next() < 0.85) ? rootA : snapNear (rootA - 5, ctxA.midAnchor);
    notes.push_back ({ beat1, 0.0, 0.95, 0 });

    int beat2;
    if (ctxB.root != ctxA.root)
    {
        if (comp > 0.4 && rng.next() < 0.5)
            beat2 = clampBass (rootB + (rng.next() < 0.5 ? -1 : 1));
        else
            beat2 = snapNear (rootA + 7, ctxA.midAnchor);
    }
    else
    {
        beat2 = chordOrScale (rootA, ctxA, comp, rng);
    }
    notes.push_back ({ beat2, 1.0, 0.95, 0 });

    const int beat3 = (ctxB.root != ctxA.root)
                          ? rootB
                          : (rng.next() < 0.55 ? snapNear (rootA + 7, ctxA.midAnchor)
                                               : chordOrScale (rootA, ctxA, comp, rng));
    notes.push_back ({ beat3, 2.0, 0.95, 0 });

    int approach;
    if (comp > 0.3 && rng.next() < 0.6 + comp * 0.25)
    {
        approach = clampBass (rootN + (rng.next() < 0.5 ? -1 : 1));
    }
    else
    {
        const std::array<int, 4> pool { rootN - 2, rootN + 2, rootN - 5, rootN + 5 };
        approach = clampBass (pool[static_cast<size_t> (rng.next() * pool.size())]);
    }
    notes.push_back ({ approach, 3.0, 0.95, 0 });

    if (p.isFill || p.isDevelopment)
    {
        const int pickup = clampBass (rootN + (notes.back().pitch < rootN ? 1 : -1));
        notes.push_back ({ pickup, 3.5, 0.45, 0 });
        notes[notes.size() - 2].dur = 0.45;
    }
    if (p.isFinalClimax)
    {
        notes.back().dur = 0.3;
        notes.push_back ({ snapNear (rootA + 5, ctxA.midAnchor), 3.33, 0.3, 0 });
        notes.push_back ({ clampBass (rootN - 1),                3.66, 0.3, 0 });
    }
    return notes;
}

// ── Style 6: Funk (16th-note groove) ─────────────────────────────────
inline std::vector<OutNote> genFunk (const BarParams& p, Rng& rng)
{
    const auto& ctx = p.ctx;
    std::vector<OutNote> notes;
    const int root = snapNear (ctx.midAnchor, ctx.midAnchor);
    const int oct  = clampBass (root + 12);
    const double comp = p.compFactor;

    notes.push_back ({ root, 0.0, 0.25, 0 });
    if (rng.next() < 0.35 + comp * 0.4) notes.push_back ({ root, 0.25, 0.25, 0 });
    if (rng.next() < 0.45 + comp * 0.3) notes.push_back ({ root, 0.75, 0.25, 0 });
    if (rng.next() < 0.6)               notes.push_back ({ chordOrScale (root, ctx, comp, rng), 1.0, 0.25, 0 });
    if (rng.next() < 0.35 + comp * 0.3) notes.push_back ({ root, 1.5, 0.25, 0 });

    if (rng.next() < 0.5 + comp * 0.3)
    {
        notes.push_back ({ oct,  2.0,  0.25, 0 });
        notes.push_back ({ root, 2.25, 0.25, 0 });
    }
    else
    {
        notes.push_back ({ root, 2.0, 0.5, 0 });
    }

    if (rng.next() < 0.4 + comp * 0.4)
    {
        const int step = ctx.scale[static_cast<size_t> (2 + static_cast<int> (rng.next() * 2))];
        notes.push_back ({ snapNear (root + step, ctx.midAnchor), 2.75, 0.25, 0 });
    }

    if (p.isLastOfPhrase)
    {
        const int nRootF = snapNear (p.nextCtx.midAnchor, ctx.midAnchor);
        notes.push_back ({ clampBass (nRootF - 2), 3.0,  0.25, 0 });
        notes.push_back ({ clampBass (nRootF - 1), 3.5,  0.25, 0 });
        notes.push_back ({ clampBass (nRootF - 1), 3.75, 0.25, 0 });
    }
    else
    {
        notes.push_back ({ root, 3.0, 0.5, 0 });
        if (rng.next() < 0.4 + comp * 0.4)
            notes.push_back ({ chordOrScale (root, ctx, comp, rng), 3.5, 0.5, 0 });
    }

    if (p.isFill || p.isFinalClimax)
    {
        const auto& pent = ctx.penta;
        for (double s = 3.0; s < 4.0; s += 0.25)
        {
            const int pIdx = static_cast<int> (rng.next() * pent.size());
            notes.push_back ({ snapNear (root + pent[static_cast<size_t> (pIdx)], ctx.midAnchor), s, 0.25, 0 });
        }
    }
    return notes;
}

// ── Style 7: Lo-Fi (sparse, root & 5th, long durations) ──────────────
inline std::vector<OutNote> genLoFi (const BarParams& p, Rng& rng)
{
    const auto& ctx = p.ctx;
    std::vector<OutNote> notes;
    const int root  = snapNear (ctx.lowAnchor, ctx.lowAnchor);
    const int fifth = clampBass (root + 7);
    const double comp = p.compFactor;

    notes.push_back ({ root, 0.0, 1.75, 0 });
    const int beat3 = (rng.next() < 0.3 + comp * 0.4) ? fifth : root;
    notes.push_back ({ beat3, 2.0, 1.75, 0 });

    if (comp > 0.35 && rng.next() < comp * 0.7)
    {
        const auto& pent = ctx.penta;
        const int idx = 1 + static_cast<int> (rng.next() * 3);
        notes.push_back ({ snapNear (root + pent[static_cast<size_t> (idx)], ctx.lowAnchor), 3.5, 0.45, 0 });
        notes[1].dur = 1.45;
    }

    if (p.isLastOfPhrase)
    {
        const int nRoot = snapNear (p.nextCtx.lowAnchor, ctx.lowAnchor);
        notes.back() = { clampBass (nRoot - 1), 3.5, 0.45, 0 };
        notes[1].dur = 1.45;
    }
    if (p.isFinalClimax)
    {
        const int nRoot2 = snapNear (p.nextCtx.lowAnchor, ctx.lowAnchor);
        notes.push_back ({ fifth, 3.0, 0.45, 0 });
        notes.push_back ({ clampBass (nRoot2 - 1), 3.5, 0.45, 0 });
        notes[1].dur = 0.95;
    }
    return notes;
}

inline std::vector<OutNote> generateBar (int style, const BarParams& p, GenContext& gc)
{
    switch (style)
    {
        case 1: return genFromLib (premierPatterns(),  p, gc);
        case 2: return genFromLib (dillaPatterns(),    p, gc);
        case 3: return genFromLib (ninthPatterns(),    p, gc);
        case 4: return genFromLib (peteRockPatterns(), p, gc);
        case 5: return genSoulJazz (p, gc.rng);
        case 6: return genFunk (p, gc.rng);
        case 7: return genLoFi (p, gc.rng);
        case 0:
        default: return genBoomBap (p, gc);
    }
}

// 検出済み進行を小節位置のコードへ写す（ループ長で巻き戻す）。
// 正本 JS: _chordAtBar。
inline std::vector<std::pair<int, std::string>> chordAtBar (int barIdx, bool useHalfBar,
                                                            const GenerateConfig& cfg)
{
    const auto& prog = useHalfBar ? cfg.progHalfBar : cfg.progBar;
    if (! cfg.useProgression || prog.empty())
    {
        const std::pair<int, std::string> def { cfg.root, cfg.mode == 0 ? "maj" : "min" };
        if (useHalfBar) return { def, def };
        return { def };
    }
    const int segsPerBar = useHalfBar ? 2 : 1;
    const int totalBars  = static_cast<int> (prog.size()) / segsPerBar;
    const int loop       = cfg.loopBars > 0 ? std::min (cfg.loopBars, totalBars) : totalBars;
    const int loopBarIdx = barIdx % std::max (1, loop);
    const int startSeg   = loopBarIdx * segsPerBar;

    std::vector<std::pair<int, std::string>> out;
    for (int i = 0; i < segsPerBar; ++i)
    {
        const auto& s = prog[static_cast<size_t> ((startSeg + i) % static_cast<int> (prog.size()))];
        out.emplace_back (s.root, s.quality);
    }
    return out;
}

// クリップ全体のノート列を生成する（v1.2: モチーフロック生成）。
//
// ヒップホップのループ構造（Schloss『Making Beats』のループ美学、実務の 1/2/4 小節
// ループ）に基づく:
//  - Boom-Bap 系は 1 ループぶんの「モチーフ（パターン）」を選んで固定する。
//    音を固定するのではなく、コードが変わる小節では同じモチーフをそのコードで
//    再解決する（リフをコードに追従させる）。同一ハーモニーの繰り返しは完全に同一。
//  - 展開はフレーズ端のみ（4=bar3 turn / 8=bar7 climax / 16=bar11 mid + bar15 climax、
//    fill はループ後半のみ）。展開も固定モチーフを基に変形する。
//  - ループ長 L: 検出ループが 2 回以上繰り返せる（L*2 <= bars）ときだけ採用。
//    それ以外は既定 2 小節（検出不能・過大検出で毎小節ランダムに退化しない）。
//  - Soul-Jazz（歩くベース）はロックせず毎小節生成する。
//  - 0 ノートならルート全音符 × Bars をフォールバック（R10）。
inline std::vector<OutNote> buildNotes (const GenerateConfig& cfg, Rng& rng)
{
    std::vector<OutNote> notes;
    MarkovSelector selector;
    selector.reset();

    const bool useHalfBar   = (cfg.style == 5);
    const bool lockLoop     = (cfg.style != 5);
    const bool patternStyle = (cfg.style >= 0 && cfg.style <= 4);
    GenContext gc { selector, rng, cfg.onsetHist, nullptr };
    const PatternBank& bank = (cfg.bank != nullptr) ? *cfg.bank : defaultBank();

    int L = std::min (cfg.bars, 2);
    if (lockLoop && cfg.useProgression && cfg.loopBars >= 1 && cfg.loopBars * 2 <= cfg.bars)
        L = cfg.loopBars;
    if (L < 1) L = 1;

    double varIntensity = 1.0;   // スタイル別変異強度（モチーフ選択時に確定）

    auto makeParams = [&] (int bar, bool withDev) -> BarParams
    {
        const auto chordsThis = chordAtBar (bar,     useHalfBar, cfg);
        const auto chordsNext = chordAtBar (bar + 1, useHalfBar, cfg);
        BarParams p;
        p.ctx        = makeKeyContext (chordsThis[0].first, chordsThis[0].second);
        p.hasMid     = useHalfBar;
        p.ctxMid     = useHalfBar ? makeKeyContext (chordsThis[1].first, chordsThis[1].second) : p.ctx;
        p.nextCtx    = makeKeyContext (chordsNext[0].first, chordsNext[0].second);
        p.useHalfBar = useHalfBar;
        p.barIndex   = bar;
        p.compFactor = cfg.complexity / 100.0;
        p.swingOverride = cfg.swingOverride;
        p.varIntensity  = varIntensity;
        if (withDev)
        {
            const auto f = computePhrase (cfg.bars, cfg.fill, bar);
            p.barInPhrase    = f.barInPhrase;
            p.isLastOfPhrase = f.isLastOfPhrase;
            p.isDevelopment  = f.isDevelopment || f.midDevelopment;
            p.isFinalClimax  = f.isDevelopment;
            p.isFill         = f.isFill;
        }
        else
        {
            p.barInPhrase = bar % 4;   // 展開なしのプレーングルーヴ
        }
        return p;
    };

    // モチーフの固定（v1.6 研究準拠）:
    //  - プロデューサ束は「1 生成につき 1 つ」選ぶ（旧: セル毎シャッフルは
    //    実在しない through-composed な線を作り、個性を消していた）。
    //  - パターンは密度重み付きで選ぶ: target notes/bar = base + span * Complexity。
    //    Premier 2+2c（default 2.6/bar）… busy は Funk/SoulJazz レーンに限定。
    //  - 変異強度はプロデューサ別（Premier ≈ verbatim 0.15 … 研究の変異モデル）。
    struct Motif { const Pattern* pat = nullptr; const ProducerLib* lib = nullptr; };
    std::vector<Motif> cellMotifs (static_cast<size_t> (L));
    if (lockLoop && patternStyle)
    {
        const ProducerLib& pl = producerForStyle (bank, cfg.style, rng);   // 生成につき 1 回
        const Transitions* tr = pl.transitions.empty() ? nullptr : &pl.transitions;

        const double c01 = cfg.complexity / 100.0;
        double targetNpb = 3.0 + 2.0 * c01, capNpb = 5.0;
        if (&pl == &bank.premier) { targetNpb = 2.0 + 2.0 * c01; capNpb = 5.0; varIntensity = 0.15; }
        else if (&pl == &bank.dilla) { targetNpb = 3.0 + 2.0 * c01; capNpb = 6.0; varIntensity = 0.7; }
        else if (&pl == &bank.ninth) { varIntensity = 0.4; }
        else if (&pl == &bank.pete)  { varIntensity = 0.4; }
        else /* pool */              { targetNpb = 2.5 + 2.5 * c01; capNpb = 6.0; varIntensity = 0.5; }

        for (int c = 0; c < L; ++c)
            cellMotifs[static_cast<size_t> (c)] =
                { &selector.pickPattern (pl.patterns, tr, rng, cfg.onsetHist, targetNpb, capNpb), &pl };
    }

    // 同一（セル, ハーモニー文脈）の繰り返しは同一結果を再利用する
    std::map<std::string, std::vector<OutNote>> cache;
    auto harmonicKey = [&] (int bar) -> std::string
    {
        const auto t = chordAtBar (bar,     useHalfBar, cfg);
        const auto n = chordAtBar (bar + 1, useHalfBar, cfg);
        std::string k = std::to_string (bar % L);
        for (const auto& c : t) { k += "|"; k += std::to_string (c.first); k += c.second; }
        for (const auto& c : n) { k += ">"; k += std::to_string (c.first); k += c.second; }
        return k;
    };

    for (int bar = 0; bar < cfg.bars; ++bar)
    {
        const auto f = computePhrase (cfg.bars, cfg.fill, bar);
        const bool developed = f.isLastOfPhrase || f.isDevelopment || f.midDevelopment || f.isFill;

        auto motifBar = [&] (int b, bool withDev) -> std::vector<OutNote>
        {
            const Motif& m  = cellMotifs[static_cast<size_t> (b % L)];
            const auto   p  = makeParams (b, withDev);
            const LearnedGroove* gr = (m.lib != nullptr && m.lib->hasGroove) ? &m.lib->groove : nullptr;
            return applyVariations (*m.pat, p, rng, gr);
        };

        std::vector<OutNote> barNotes;
        if (lockLoop && ! developed)
        {
            const std::string key = harmonicKey (bar);
            const auto it = cache.find (key);
            if (it != cache.end())
            {
                barNotes = it->second;   // ループの繰り返し（完全同一）
            }
            else
            {
                barNotes = patternStyle ? motifBar (bar, false)
                                        : generateBar (cfg.style, makeParams (bar, false), gc);
                cache.emplace (key, barNotes);
            }
        }
        else
        {
            // 展開バー: 固定モチーフを基に変形（パターン系）/ dev フラグ付き生成（手続き系）
            barNotes = (lockLoop && patternStyle) ? motifBar (bar, true)
                                                  : generateBar (cfg.style, makeParams (bar, true), gc);
        }

        const double barOff = bar * 4.0;
        for (auto& n : barNotes)
        {
            n.start += barOff;
            if (n.vel == 0) n.vel = 127;
            // v1.6: 手続き系（Funk/SoulJazz/LoFi）にもソフト音域上限（ジャズ系は
            // A2=45 まで許容）。パターン系は applyVariations 内で 44 適用済み。
            if (! patternStyle)
                n.pitch = reflectCeiling (n.pitch, n.dur, 45);
            notes.push_back (n);
        }
    }

    if (notes.empty())   // R10 fallback
    {
        const auto ctx = makeKeyContext (cfg.root, cfg.mode == 1);
        for (int bar = 0; bar < cfg.bars; ++bar)
            notes.push_back ({ clampBass (ctx.lowAnchor), bar * 4.0, 4.0, 127 });
    }
    return notes;
}

} // namespace kemuri::core
