#pragma once

#include <vector>

#include "GrooveEngine.h"
#include "MarkovSelector.h"
#include "Pattern.h"
#include "PatternLibrary.h"

// プロデューサ別のパターン + 学習遷移 + 学習グルーヴをまとめた束（M4）。
// ハードコード既定に patterns.json（R6）をマージして使う。処理側（JUCE）が
// JSON を解釈してこの構造を埋める。kemuri_core は JUCE 非依存のまま。
namespace kemuri::core
{

struct ProducerLib
{
    std::vector<Pattern> patterns;      // ハードコード + 学習パターン
    Transitions          transitions;   // 学習 Markov 遷移（空なら未学習）
    LearnedGroove        groove;        // 学習タイミング
    bool                 hasGroove = false;
};

struct PatternBank
{
    ProducerLib premier;
    ProducerLib dilla;
    ProducerLib ninth;
    ProducerLib pete;
    ProducerLib pool;   // どのプロデューサにも属さない学習パターン（Boom-Bap 回転に追加）
};

// ハードコードのみで初期化した束。
inline PatternBank makeDefaultBank()
{
    PatternBank b;
    b.premier.patterns = premierPatterns();
    b.dilla.patterns   = dillaPatterns();
    b.ninth.patterns   = ninthPatterns();
    b.pete.patterns    = peteRockPatterns();
    return b;
}

} // namespace kemuri::core
