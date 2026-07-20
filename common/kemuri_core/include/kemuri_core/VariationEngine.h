#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "GrooveEngine.h"
#include "KeyContext.h"
#include "MusicTheory.h"
#include "Pattern.h"
#include "PitchToken.h"
#include "Rng.h"
#include "Types.h"

namespace kemuri::core
{

// 1 小節生成に渡す文脈。正本 JS: generateBar の引数 `p`。
struct BarParams
{
    KeyContext ctx;
    KeyContext ctxMid;                 // half-bar chord (Soul-Jazz only)
    bool       hasMid        = false;
    KeyContext nextCtx;
    bool       useHalfBar    = false;
    int        barIndex      = 0;
    int        barInPhrase   = 0;
    bool       isLastOfPhrase = false;
    bool       isDevelopment = false;  // isDevelopment || midDevelopment
    bool       isFinalClimax = false;  // 最終小節のクライマックス
    bool       isFill        = false;
    double     compFactor    = 0.0;
    int        swingOverride = 0;      // v1.5: うわネタ検出スイング%（0=パターン既定）
    double     varIntensity  = 1.0;    // v1.6: スタイル別変異強度（Premier 0.15 …）
};

// v1.6: ソフト音域上限。メインの音は G#2 (44) まで、短い音(≤16分)のみ +2 許す。
// 超過は 1 オクターブ下へ反射（B2 で鳴り続ける「変に高い音」の根治）。
// 検証済み研究: メインパターンは E1..G#2、C3+ は短いフィルのみ。
// ジャズ系（Funk/SoulJazz）は mainCeil=45 (A2) で呼ぶ。
inline int reflectCeiling (int pitch, double dur, int mainCeil = 44)
{
    const int ceiling = (dur <= 0.25) ? mainCeil + 2 : mainCeil;
    while (pitch > ceiling) pitch -= 12;
    if (pitch < kBassMin) pitch += 12;
    return pitch;
}

// パターンへ micro-variation（オクターブ入替・ゴースト・ドロップ・タイミング）を適用し、
// フレーズ位置に応じたターンアラウンド/フィル/クライマックスを付与する。
// 正本 JS: _applyVariations。groove があれば学習タイミングを優先。
inline std::vector<OutNote> applyVariations (const Pattern& pat, const BarParams& p, Rng& rng,
                                             const LearnedGroove* groove = nullptr)
{
    struct TmpNote { double pos; double dur; int pitch; int vel; };
    std::vector<TmpNote> notes;

    const double c   = p.compFactor;
    const double vi  = p.varIntensity;   // v1.6: スタイル別変異強度
    const auto&  ctx  = p.ctx;
    const auto&  nctx = p.nextCtx;

    // v1.6 (研究準拠): 低 Complexity ほど間引く（隙間 = boom-bap の基本）。
    // 変異はスタイル強度でスケール（Premier ≈ verbatim、Funk/SoulJazz は現状維持）。
    const double dropProb  = vi * std::max (0.15, 0.35 - c * 0.20);
    const double swapProb  = vi * (c * 0.06);        // 旧 0.04+c*0.18 は跳躍しすぎ
    const double ghostProb = vi * (c * 0.20);        // 旧 c*0.45 は音数過多

    for (const auto& orig : pat.notes)
    {
        if (orig.pos != 0.0 && rng.next() < dropProb) continue;   // drop off-beats

        int pitch = resolvePitch (orig.pitch, ctx, nctx);

        if (rng.next() < swapProb)                                // octave swap（70% 下方向）
        {
            const bool down = rng.next() < 0.7;
            const int  swapped = pitch + (down ? -12 : 12);
            if (down || swapped <= 44)                            // 上方向は G#2 まで
                pitch = clampBass (swapped);
        }

        double pos = orig.pos;
        const int vel = 127;

        int slot = static_cast<int> (std::lround (pos / 0.25)) % 16;
        if (slot < 0) slot = 0;
        bool applied = false;

        if (groove != nullptr)
        {
            const auto& t = groove->timing[static_cast<size_t> (slot)];
            if (t.has_value())
            {
                double dev = t->first + rng.gauss() * t->second;
                dev = std::clamp (dev, -0.12, 0.12);
                pos += dev;
                applied = true;
            }
        }
        // うわネタから検出したスイングがあれば優先（同じポケットに乗せる）。
        const int effSwing = (p.swingOverride > 0) ? p.swingOverride : pat.swing;
        if (! applied && effSwing != 0)
        {
            const double frac = pos - std::floor (pos / 0.5) * 0.5;
            if (std::abs (frac - 0.25) < 0.01)
                pos += (effSwing - 50) / 100.0 * 0.5;
        }
        if (! applied && pat.jitter != 0.0)
            pos += (rng.next() - 0.5) * 2.0 * pat.jitter;

        pos = std::clamp (pos, 0.0, 3.9);
        notes.push_back ({ pos, orig.dur, pitch, vel });
    }

    // Ghost note（v1.6: 最大 1/小節・常に低域ルート。+12 ゴーストは「変な高音」の元凶）
    if (rng.next() < ghostProb)
    {
        static constexpr std::array<double, 5> ghostSlots { 0.75, 1.25, 1.75, 2.25, 2.75 };
        const double gp = ghostSlots[static_cast<size_t> (rng.next() * ghostSlots.size())];
        bool clash = false;
        for (const auto& n : notes)
            if (std::abs (n.pos - gp) < 0.2) { clash = true; break; }
        if (! clash)
            notes.push_back ({ gp, 0.25, clampBass (ctx.lowAnchor), 0 });   // vel unset → 0
    }

    // Turnaround / fill / climax（v1.6: 研究準拠 — フィルは最大 2 音・低域、
    // クライマックスの 50% は「足す」代わりに「引く」= 末尾を空けて締める）
    const int nRoot = clampBass (nctx.lowAnchor);
    if (p.isFinalClimax)
    {
        std::vector<TmpNote> kept;
        for (const auto& n : notes) if (n.pos < 3.0) kept.push_back (n);
        notes = std::move (kept);
        if (rng.next() >= 0.5)
        {
            // additive: 低域 2 音のショートフィル（旧: 16 分ペンタ 4 連は busy すぎ）
            notes.push_back ({ 3.0, 0.4, clampBass (ctx.lowAnchor), 127 });
            notes.push_back ({ 3.5, 0.4, clampBass (nRoot - (rng.next() < 0.5 ? 1 : 2)), 127 });
        }
        // subtractive: 何も足さない（空白がフックになる）
    }
    else if (p.isLastOfPhrase)
    {
        // ターンアラウンドは確率制（毎フレーズ必ずは入れない）
        if (rng.next() < 0.5 + c * 0.5)
        {
            std::vector<TmpNote> kept;
            for (const auto& n : notes) if (n.pos < 3.4) kept.push_back (n);
            notes = std::move (kept);
            const int dir = (rng.next() < 0.5) ? -1 : 1;
            notes.push_back ({ 3.5, 0.5, clampBass (nRoot + dir), 127 });
        }
    }
    else if (p.isFill)
    {
        // v1.6.1: フィルの音は root か 5th のみ（ペンタ中域の色音はベースでは
        // 「音色が変わった」ように聴こえる — 検証済み研究のフィル語彙に整合）
        const int fillPitch = (rng.next() < 0.5) ? ctx.lowAnchor : ctx.lowAnchor + 7;
        notes.push_back ({ 3.0, 0.25, clampBass (fillPitch), 127 });
    }

    std::sort (notes.begin(), notes.end(),
               [] (const TmpNote& a, const TmpNote& b) { return a.pos < b.pos; });

    std::vector<OutNote> out;
    out.reserve (notes.size());
    for (const auto& n : notes)
        out.push_back ({ reflectCeiling (n.pitch, n.dur), n.pos, n.dur, n.vel });
    return out;
}

} // namespace kemuri::core
