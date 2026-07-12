#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// 4x オーバーサンプリング True Peak（dBTP）。
//
// M4L 版はサンプルピークのみだったが、これはインターサンプルピークを捉える。
// オフラインの analyzer.py は FFT sinc 補間、こちらは windowed-sinc polyphase FIR
// によるリアルタイム版。適度な帯域の信号では両者は ±0.3 dB で一致する（G3）。
//
// 1 チャンネルぶんのディレイラインを持つ。prepare 後は確保しない（R12）。
namespace kemuri::dsp
{

class TruePeakMeter
{
public:
    static constexpr int kOversample   = 4;
    static constexpr int kTapsPerPhase = 32;
    static constexpr double kKaiserBeta = 9.0;   // 通過帯域を平坦に（overshoot 抑制）

    TruePeakMeter() { buildCoeffs(); }

    void prepare (int numChannelsIn)
    {
        numChannels = std::max (1, numChannelsIn);
        delay.assign (static_cast<size_t> (numChannels), std::vector<float> (kTapsPerPhase, 0.0f));
        reset();
    }

    void reset()
    {
        for (auto& d : delay)
            std::fill (d.begin(), d.end(), 0.0f);
        writePos = 0;
        peak     = 0.0f;
    }

    // channelData[ch][i]。全チャンネルの最大インターサンプルピークを追う。
    void process (const float* const* channelData, int numCh, int numSamples) noexcept
    {
        const int nc = std::min (numCh, numChannels);
        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < nc; ++ch)
                pushSample (ch, channelData[ch][i]);
            writePos = (writePos + 1) % kTapsPerPhase;
        }
    }

    // 直近の最大 True Peak（線形振幅）。dBTP は getPeakDb()。
    float getPeakLinear() const noexcept { return peak; }

    double getPeakDb() const noexcept
    {
        if (peak <= 0.0f) return -200.0;
        return 20.0 * std::log10 (static_cast<double> (peak));
    }

    // ── オフライン測定（テスト・スナップショット用）─────────────────
    // 1 本のモノ信号の定常状態 True Peak を dBTP で返す。
    //
    // オフライン参照（analyzer._measure_true_peak_db）は FFT で信号を周期的に扱うため
    // オンセット不連続が無い。ストリーミング FIR をこれと公平に比較するため、
    // 1 回流してディレイラインを充填・定常化させてから（ピークを捨てて）もう一度流し、
    // 定常状態のピークを返す（テスト信号は整数周期なので継ぎ目なく連続する）。
    // プラグイン実運用では音声は連続で流れ続けるため、この定常状態が実際の挙動。
    static double measureMonoDb (const float* mono, int numSamples)
    {
        TruePeakMeter m;
        m.prepare (1);
        const float* ch[1] = { mono };
        m.process (ch, 1, numSamples);   // ウォームアップ（充填 + 過渡を通過）
        m.peak = 0.0f;                    // 過渡のピークを破棄
        m.process (ch, 1, numSamples);    // 定常状態を測定
        return m.getPeakDb();
    }

private:
    static double besselI0 (double x)
    {
        // I0(x) = Σ ((x/2)^k / k!)^2
        double sum = 1.0, term = 1.0;
        const double halfSq = (x * 0.5) * (x * 0.5);
        for (int k = 1; k < 64; ++k)
        {
            term *= halfSq / (static_cast<double> (k) * static_cast<double> (k));
            sum  += term;
            if (term < 1e-14 * sum) break;
        }
        return sum;
    }

    void buildCoeffs()
    {
        // windowed-sinc プロトタイプ（カットオフ = 元 Nyquist）。位相ごとに分解。
        const int  L      = kOversample;
        const int  taps   = kTapsPerPhase;
        const int  proto  = taps * L;              // プロトタイプ長
        const double center = (proto - 1) / 2.0;
        const double i0beta = besselI0 (kKaiserBeta);

        std::vector<double> h (static_cast<size_t> (proto));
        for (int n = 0; n < proto; ++n)
        {
            const double x = (n - center) / static_cast<double> (L);   // sinc 引数
            const double s = (std::abs (x) < 1e-9) ? 1.0
                                                   : std::sin (kPiTp * x) / (kPiTp * x);
            // Kaiser 窓（beta 大 → 通過帯域が平坦で overshoot を抑える）
            const double r = 2.0 * n / (proto - 1) - 1.0;              // -1..+1
            const double arg = 1.0 - r * r;
            const double w = besselI0 (kKaiserBeta * std::sqrt (arg > 0.0 ? arg : 0.0)) / i0beta;
            h[static_cast<size_t> (n)] = s * w;
        }

        // 位相 p の係数: proto[k*L + p]。各位相の DC ゲインを 1 に正規化。
        for (int p = 0; p < L; ++p)
        {
            double sum = 0.0;
            for (int k = 0; k < taps; ++k)
                sum += h[static_cast<size_t> (k * L + p)];
            const double norm = (std::abs (sum) < 1e-12) ? 1.0 : 1.0 / sum;
            for (int k = 0; k < taps; ++k)
                phase[static_cast<size_t> (p)][static_cast<size_t> (k)]
                    = static_cast<float> (h[static_cast<size_t> (k * L + p)] * norm);
        }
    }

    inline void pushSample (int ch, float x) noexcept
    {
        auto& line = delay[static_cast<size_t> (ch)];
        line[static_cast<size_t> (writePos)] = x;

        // L 個のサブサンプルを各位相 FIR で生成し、最大絶対値を追う
        for (int p = 0; p < kOversample; ++p)
        {
            double acc = 0.0;
            int idx = writePos;
            const auto& ph = phase[static_cast<size_t> (p)];
            for (int k = 0; k < kTapsPerPhase; ++k)
            {
                acc += static_cast<double> (ph[static_cast<size_t> (k)])
                     * static_cast<double> (line[static_cast<size_t> (idx)]);
                idx = (idx - 1 + kTapsPerPhase) % kTapsPerPhase;
            }
            const float a = std::abs (static_cast<float> (acc));
            if (a > peak) peak = a;
        }
    }

    static constexpr double kPiTp = 3.14159265358979323846;

    int numChannels = 2;
    int writePos    = 0;
    float peak      = 0.0f;

    std::vector<std::vector<float>> delay;                 // [ch][tap]
    std::array<std::array<float, kTapsPerPhase>, kOversample> phase { {} };
};

} // namespace kemuri::dsp
