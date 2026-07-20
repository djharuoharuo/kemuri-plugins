#pragma once

#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "InteractionScorer.h"
#include "Pattern.h"
#include "Rng.h"

namespace kemuri::core
{

// 学習済みパターン遷移表（patterns.json, M4）。from → {to: weight}。
using Transitions = std::map<std::string, std::map<std::string, double>>;

// パターン選択器。クリップ毎に reset() で Markov 連鎖状態をリセットする。
// 正本 JS: _sampleOne / _pickPattern（g_lastPatName はメンバに保持）。
class MarkovSelector
{
public:
    void reset() { lastPatName.clear(); }

    // 学習遷移表があり前小節のパターンが分かれば 70% で Markov 追従。
    // それ以外は密度重み付きサンプリング（v1.6: Complexity が選ぶのは
    // 「どの密度のパターンか」— 既に密なパターンへ装飾を積まない）。
    // targetNpb < 0 で従来の一様ランダム。
    const Pattern& sampleOne (const std::vector<Pattern>& lib,
                              const Transitions* trans, Rng& rng,
                              double targetNpb = -1.0, double capNpb = 1e9) const
    {
        if (trans != nullptr && ! lastPatName.empty() && rng.next() < 0.7)
        {
            const auto it = trans->find (lastPatName);
            if (it != trans->end())
            {
                double total = 0.0;
                for (const auto& kv : it->second) total += kv.second;
                if (total > 0.0)
                {
                    double r = rng.next() * total;
                    for (const auto& kv : it->second)
                    {
                        r -= kv.second;
                        if (r <= 0.0)
                        {
                            for (const auto& p : lib)
                                if (p.name == kv.first) return p;
                            break;
                        }
                    }
                }
            }
        }

        if (targetNpb > 0.0)
        {
            double total = 0.0;
            std::vector<double> w (lib.size());
            for (size_t i = 0; i < lib.size(); ++i)
            {
                const double npb = static_cast<double> (lib[i].notes.size());
                w[i] = (npb > capNpb) ? 0.0 : std::exp (-std::abs (npb - targetNpb) / 0.75);
                total += w[i];
            }
            if (total > 0.0)
            {
                double r = rng.next() * total;
                for (size_t i = 0; i < lib.size(); ++i)
                {
                    r -= w[i];
                    if (r <= 0.0) return lib[i];
                }
            }
        }
        return lib[static_cast<size_t> (rng.next() * static_cast<double> (lib.size()))];
    }

    // トップライン解析済みなら複数候補から最も「応答」するものを選ぶ。
    const Pattern& pickPattern (const std::vector<Pattern>& lib,
                                const Transitions* trans, Rng& rng,
                                const std::optional<std::array<double, 16>>& onsetHist,
                                double targetNpb = -1.0, double capNpb = 1e9)
    {
        const int nCand = onsetHist.has_value() ? 3 : 1;
        const Pattern* best = nullptr;
        double bestScore = -1e300;
        for (int c = 0; c < nCand; ++c)
        {
            const Pattern& cand = sampleOne (lib, trans, rng, targetNpb, capNpb);
            const double sc = onsetHist.has_value()
                                  ? interactionScore (cand, *onsetHist)
                                  : 0.0;
            if (sc > bestScore) { bestScore = sc; best = &cand; }
        }
        lastPatName = best->name;
        return *best;
    }

private:
    std::string lastPatName;
};

} // namespace kemuri::core
