#pragma once

#include <vector>

#include <kemuri_core/Types.h>

namespace kemuri
{

// 生成済みループの不変スナップショット。
// メッセージスレッドで構築し、オーディオスレッドへ atomic ポインタで受け渡す（R11）。
// 構築後は読み取り専用なので、オーディオスレッドは lock/alloc なしで参照できる。
struct MidiSequence
{
    std::vector<core::OutNote> notes;       // start でソート済み、beats 単位
    double                     lengthBeats = 0.0;
};

} // namespace kemuri
