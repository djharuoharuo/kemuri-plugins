#pragma once

#include <array>
#include <optional>
#include <utility>

namespace kemuri::core
{

// 学習済みグルーヴ（16分スロットごとの micro-timing 統計 [mean, std]）。
// patterns.json (M4) から供給される。未学習スロットは nullopt。
// 正本 JS: USER_GROOVE* / _learnedGroove。velocity は常に 127 固定（学習しない）。
struct LearnedGroove
{
    std::array<std::optional<std::pair<double, double>>, 16> timing {};

    bool hasAny() const
    {
        for (const auto& t : timing)
            if (t.has_value()) return true;
        return false;
    }
};

} // namespace kemuri::core
