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
};

// パターンへ micro-variation（オクターブ入替・ゴースト・ドロップ・タイミング）を適用し、
// フレーズ位置に応じたターンアラウンド/フィル/クライマックスを付与する。
// 正本 JS: _applyVariations。groove があれば学習タイミングを優先。
inline std::vector<OutNote> applyVariations (const Pattern& pat, const BarParams& p, Rng& rng,
                                             const LearnedGroove* groove = nullptr)
{
    struct TmpNote { double pos; double dur; int pitch; int vel; };
    std::vector<TmpNote> notes;

    const double c  = p.compFactor;
    const auto&  ctx  = p.ctx;
    const auto&  nctx = p.nextCtx;

    for (const auto& orig : pat.notes)
    {
        if (orig.pos != 0.0 && rng.next() < (0.18 - c * 0.12)) continue;   // drop off-beats

        int pitch = resolvePitch (orig.pitch, ctx, nctx);

        if (rng.next() < (0.04 + c * 0.18))                                // octave swap
            pitch = clampBass (pitch + (rng.next() < 0.5 ? 12 : -12));

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
        if (! applied && pat.swing != 0)
        {
            const double frac = pos - std::floor (pos / 0.5) * 0.5;
            if (std::abs (frac - 0.25) < 0.01)
                pos += (pat.swing - 50) / 100.0 * 0.5;
        }
        if (! applied && pat.jitter != 0.0)
            pos += (rng.next() - 0.5) * 2.0 * pat.jitter;

        pos = std::clamp (pos, 0.0, 3.9);
        notes.push_back ({ pos, orig.dur, pitch, vel });
    }

    // Ghost / extra note (complexity-driven)
    if (rng.next() < c * 0.45)
    {
        static constexpr std::array<double, 5> ghostSlots { 0.75, 1.25, 1.75, 2.25, 2.75 };
        const double gp = ghostSlots[static_cast<size_t> (rng.next() * ghostSlots.size())];
        bool clash = false;
        for (const auto& n : notes)
            if (std::abs (n.pos - gp) < 0.2) { clash = true; break; }
        if (! clash)
        {
            const int ghostPitch = (rng.next() < 0.75)
                                       ? clampBass (ctx.lowAnchor)
                                       : clampBass (ctx.lowAnchor + 12);
            notes.push_back ({ gp, 0.25, ghostPitch, 0 });   // vel unset → 0
        }
    }

    // Turnaround / fill / climax
    const int nRoot = clampBass (nctx.lowAnchor);
    if (p.isFinalClimax)
    {
        std::vector<TmpNote> kept;
        for (const auto& n : notes) if (n.pos < 2.75) kept.push_back (n);
        notes = std::move (kept);
        const auto& pent = ctx.penta;
        notes.push_back ({ 3.0,  0.25, clampBass (ctx.lowAnchor + pent[1]), 127 });
        notes.push_back ({ 3.25, 0.25, clampBass (ctx.lowAnchor + pent[2]), 127 });
        notes.push_back ({ 3.5,  0.25, clampBass (nRoot - 2), 127 });
        notes.push_back ({ 3.75, 0.25, clampBass (nRoot - 1), 127 });
    }
    else if (p.isLastOfPhrase)
    {
        std::vector<TmpNote> kept;
        for (const auto& n : notes) if (n.pos < 3.4) kept.push_back (n);
        notes = std::move (kept);
        const int dir = (rng.next() < 0.5) ? -1 : 1;
        notes.push_back ({ 3.5, 0.5, clampBass (nRoot + dir), 127 });
    }
    else if (p.isFill)
    {
        const int pIdx = 1 + static_cast<int> (rng.next() * 3);
        notes.push_back ({ 3.0, 0.25, clampBass (ctx.lowAnchor + ctx.penta[static_cast<size_t> (pIdx)]), 127 });
    }

    std::sort (notes.begin(), notes.end(),
               [] (const TmpNote& a, const TmpNote& b) { return a.pos < b.pos; });

    std::vector<OutNote> out;
    out.reserve (notes.size());
    for (const auto& n : notes)
        out.push_back ({ n.pitch, n.pos, n.dur, n.vel });
    return out;
}

} // namespace kemuri::core
