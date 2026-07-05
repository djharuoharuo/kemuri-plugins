#pragma once

#include <string>
#include <vector>

namespace kemuri::core
{

// MIDI 入力解析で受けた生ノート（beats 単位）
struct RawNote
{
    int    pitch;
    double start;
    double duration;
};

// 生成結果の 1 ノート（start はクリップ先頭からの beats、vel は 0-127）
struct OutNote
{
    int    pitch;
    double start;
    double dur;
    int    vel;
};

// コード検出の 1 セグメント
struct ChordSeg
{
    double      startBeat;
    double      durationBeats;
    int         root;      // 0-11
    std::string quality;   // "maj" / "min"
};

} // namespace kemuri::core
