#pragma once

#include <vector>

#include "Pattern.h"

// プロデューサ別ハードコードパターンライブラリ。
// 正本 JS: PREMIER_PATTERNS / DILLA_PATTERNS / NINTH_PATTERNS / PETEROCK_PATTERNS。
// 学習パターン (patterns.json, M4) は同一スキーマでこれらへマージする。
namespace kemuri::core
{

const std::vector<Pattern>& premierPatterns();
const std::vector<Pattern>& dillaPatterns();
const std::vector<Pattern>& ninthPatterns();
const std::vector<Pattern>& peteRockPatterns();

} // namespace kemuri::core
