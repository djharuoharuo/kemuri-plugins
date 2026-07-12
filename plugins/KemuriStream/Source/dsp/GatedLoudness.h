#pragma once

#include <array>
#include <cmath>
#include <vector>

#include "KWeighting.h"

// ITU-R BS.1770-4 / EBU R128 ゲート付き integrated loudness。
//
// ブロック構造は pyloudnorm と同一（400ms ブロック・100ms ホップ = 75% オーバーラップ）。
// K-weighting → チャンネル別二乗平均 → チャンネル和（G=1.0）→ 二段ゲート:
//   絶対ゲート -70 LUFS / 相対ゲート（絶対ゲート後平均 -10 LU）。
// codec_filter.js の on_power / integrated_lufs と数値一致し、
// かつ pyloudnorm.Meter.integrated_loudness とも一致する（G3 で検証）。
//
// 無音は絶対ゲートで履歴に入らないため、無音中は integrated が凍結する（R15）。
//
// process() は音声スレッドから呼ばれる。prepare() 済みなら確保・ロックを行わない（R12）。
namespace kemuri::dsp
{

class GatedLoudness
{
public:
    static constexpr double kOffset      = -0.691;   // BS.1770 オフセット
    static constexpr double kSilenceGate = -70.0;    // 絶対ゲート (LUFS)
    static constexpr double kRelGate     = -10.0;    // 相対ゲート (LU)
    static constexpr int    kMaxChannels = 2;
    static constexpr int    kBlockSubs   = 4;        // 400ms = 4 × 100ms
    static constexpr int    kMaxBlocks   = 18000;    // ~30分 @ 10 blocks/s

    void prepare (double newSampleRate, int numChannelsIn)
    {
        sampleRate    = newSampleRate;
        numChannels   = std::min (numChannelsIn, kMaxChannels);
        subBlockLen   = std::max (1, static_cast<int> (std::lround (0.1 * sampleRate)));

        for (int ch = 0; ch < kMaxChannels; ++ch)
            kWeight[ch].prepare (sampleRate);

        blocks.assign (kMaxBlocks, 0.0);   // 確保はここだけ（音声スレッド外）
        reset();
    }

    void reset()
    {
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            kWeight[ch].reset();
            subBlockSum[ch] = 0.0;
            for (int s = 0; s < kBlockSubs; ++s)
                subRing[ch][s] = 0.0;
        }
        subBlockCounter = 0;
        subCount        = 0;
        subWrite        = 0;
        blockWrite      = 0;
        blockCount      = 0;
        momentary       = kSilenceGate - 10.0;   // 無効値
        integrated      = kSilenceGate - 10.0;
    }

    // channelData[ch][i]。音声スレッドから。確保なし。
    void process (const float* const* channelData, int numCh, int numSamples) noexcept
    {
        const int nc = std::min (numCh, numChannels);

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < nc; ++ch)
            {
                const float w = kWeight[ch].processSample (channelData[ch][i]);
                subBlockSum[ch] += static_cast<double> (w) * static_cast<double> (w);
            }

            if (++subBlockCounter >= subBlockLen)
            {
                closeSubBlock (nc);
                subBlockCounter = 0;
            }
        }
    }

    double getMomentaryLufs()  const noexcept { return momentary; }
    double getIntegratedLufs() const noexcept { return integrated; }
    bool   hasIntegrated()     const noexcept { return blockCount > 0; }

private:
    static double powerToLufs (double p) noexcept
    {
        if (p <= 1e-10) return -80.0;
        return kOffset + 10.0 * std::log10 (p);
    }

    void closeSubBlock (int nc) noexcept
    {
        // 直近 100ms の各チャンネル二乗和をリングへ格納
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            subRing[ch][subWrite] = subBlockSum[ch];
            subBlockSum[ch] = 0.0;
        }
        subWrite = (subWrite + 1) % kBlockSubs;
        if (subCount < kBlockSubs) ++subCount;

        // 400ms ぶん揃ったらブロックを確定
        if (subCount < kBlockSubs)
            return;

        const double denom = static_cast<double> (kBlockSubs * subBlockLen);
        double zBlock = 0.0;                        // チャンネル和（G=1.0）
        for (int ch = 0; ch < nc; ++ch)
        {
            double chSum = 0.0;
            for (int s = 0; s < kBlockSubs; ++s)
                chSum += subRing[ch][s];
            zBlock += chSum / denom;                // チャンネル二乗平均
        }

        momentary = powerToLufs (zBlock);

        // 絶対ゲート: 無音ブロックは履歴に入れない（→ integrated 凍結）
        if (momentary > kSilenceGate)
        {
            if (blockCount < kMaxBlocks)
            {
                blocks[static_cast<size_t> (blockWrite)] = zBlock;
                blockWrite = (blockWrite + 1) % kMaxBlocks;
                ++blockCount;
            }
            else
            {
                blocks[static_cast<size_t> (blockWrite)] = zBlock;   // 最古を上書き
                blockWrite = (blockWrite + 1) % kMaxBlocks;
            }
            recomputeIntegrated();
        }
    }

    void recomputeIntegrated() noexcept
    {
        const int n = blockCount;
        if (n == 0) return;

        // pass 1: 絶対ゲート後の平均
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
            sum += blocks[static_cast<size_t> (i)];
        const double l1  = powerToLufs (sum / n);
        const double thr = l1 + kRelGate;

        // pass 2: 相対ゲート
        double sum2 = 0.0;
        int    m    = 0;
        for (int i = 0; i < n; ++i)
        {
            if (powerToLufs (blocks[static_cast<size_t> (i)]) > thr)
            {
                sum2 += blocks[static_cast<size_t> (i)];
                ++m;
            }
        }
        integrated = (m == 0) ? l1 : powerToLufs (sum2 / m);
    }

    double sampleRate  = 44100.0;
    int    numChannels = 2;
    int    subBlockLen = 4410;

    std::array<KWeightingFilter, kMaxChannels> kWeight;
    std::array<double, kMaxChannels>           subBlockSum { {} };
    std::array<std::array<double, kBlockSubs>, kMaxChannels> subRing { {} };

    int subBlockCounter = 0;
    int subCount        = 0;   // 揃った 100ms サブブロック数（最大 kBlockSubs）
    int subWrite        = 0;

    std::vector<double> blocks;   // z_block 履歴（絶対ゲート済み）。prepare で確保
    int blockWrite = 0;
    int blockCount = 0;

    double momentary  = -80.0;
    double integrated = -80.0;
};

} // namespace kemuri::dsp
