#pragma once

#include <random>

namespace kemuri::core
{

// 生成の確率的部分に注入する乱数源。テストは固定シードで決定的に回す。
// 正本 JS は Math.random() 直呼びでシード不可のため、C++ 版は完全一致を求めず
// 分布検査で検証する（AGENTS.md G3）。
class Rng
{
public:
    explicit Rng (std::uint32_t seed = std::random_device {}()) : engine (seed) {}

    // [0,1) の一様乱数（JS Math.random 相当）
    double next()
    {
        return dist (engine);
    }

    // Box-Muller 標準正規サンプル（JS _gauss 相当）
    double gauss()
    {
        const double u = next() > 0.0 ? next() : 1e-9;
        const double v = next();
        return std::sqrt (-2.0 * std::log (u <= 0.0 ? 1e-9 : u)) * std::cos (2.0 * 3.14159265358979323846 * v);
    }

    std::mt19937& raw() { return engine; }

private:
    std::mt19937 engine;
    std::uniform_real_distribution<double> dist { 0.0, 1.0 };
};

} // namespace kemuri::core
