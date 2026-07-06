#pragma once

#include <string>
#include <vector>

#include "PitchToken.h"

namespace kemuri::core
{

// パターン定義の 1 ノート（pos は小節先頭からの beats 0-4）
struct PatternNote
{
    double     pos;
    double     dur;
    PitchToken pitch;
};

// 1 小節ぶんのパターン。swing / jitter は任意の feel パラメータ。
struct Pattern
{
    std::string              name;
    int                      swing  = 0;   // MPC 16分スイング % (0=無効)
    double                   jitter = 0.0; // ±jitter beats のドランクタイミング
    std::vector<PatternNote> notes;
};

} // namespace kemuri::core
