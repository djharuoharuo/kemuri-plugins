#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <map>
#include <vector>

#include "ChordDetector.h"
#include "Dsp.h"
#include "KeyDetector.h"
#include "Types.h"

// v2: うわネタ（サンプルのチョップ）をオーディオから解析する（R5 改）。
// サイドチェインで受けたネタの音を 16 分単位の特徴量にし、
//  - 和音（2 拍 / 1 小節, Viterbi）とキー（Temperley プロファイル）
//  - サンプル自身のベースライン（低域を抜いて YIN で音程推定
//    = 90 年代の「サンプルにフィルターをかけてベースを抜く」手法の再現）
//  - ループ長と、ソング小節基準に位相を揃えた 1 ループ分の結果
// を求める。JUCE 非依存（テスト可能）。
namespace kemuri::core
{

// サイドチェインから取り込んだ連続区間（一定テンポ前提）
struct CapturedAudio
{
    std::vector<float> samples;       // モノラル（デシメート済み）
    double sampleRate     = 11025.0;
    double ppq0           = 0.0;      // samples[0] のソング位置（beats）
    double beatsPerSample = 0.0;
};

struct StepFeature
{
    std::array<double, 12> chroma {};
    double energy   = 0.0;
    double onset    = 0.0;
    int    bassMidi = -1;     // サンプル自身のベース音（-1 = 無声）
    double bassConf = 0.0;
};

struct AudioFeatures
{
    long firstBar = 0;                  // steps[0] のソング小節
    int  numBars  = 0;
    std::vector<StepFeature> steps;     // numBars * 16
};

// ── 16 分ステップ単位の特徴量 ─────────────────────────────────────
inline AudioFeatures computeAudioFeatures (const CapturedAudio& a, int maxBars = 16)
{
    AudioFeatures out;
    const size_t n = a.samples.size();
    constexpr int N = 4096, hop = 256;
    if (n < static_cast<size_t> (N) || a.beatsPerSample <= 0.0 || a.sampleRate <= 0.0)
        return out;

    const double ppqEnd = a.ppq0 + static_cast<double> (n) * a.beatsPerSample;
    long firstBar = static_cast<long> (std::ceil (a.ppq0 / 4.0 - 1e-9));
    const long endBar = static_cast<long> (std::floor (ppqEnd / 4.0 + 1e-9));   // exclusive
    if (endBar - firstBar > maxBars) firstBar = endBar - maxBars;
    if (endBar <= firstBar) return out;

    out.firstBar = firstBar;
    out.numBars  = static_cast<int> (endBar - firstBar);
    out.steps.resize (static_cast<size_t> (out.numBars) * 16);

    const double sr = a.sampleRate;
    auto sampleAtPpq = [&a] (double ppq) { return (ppq - a.ppq0) / a.beatsPerSample; };

    // bin → pitch class（60 Hz〜2.1 kHz、半音中心からの距離でガウス重み）
    std::vector<int>    binPc (N / 2, -1);
    std::vector<double> binW  (N / 2, 0.0);
    for (int k = 1; k < N / 2; ++k)
    {
        const double f = k * sr / N;
        if (f < 60.0 || f > 2100.0) continue;
        const double m = dsp::hzToMidi (f);
        const double nearest = std::round (m);
        const double dist = std::abs (m - nearest);
        binPc[static_cast<size_t> (k)] = ((static_cast<int> (nearest) % 12) + 12) % 12;
        binW[static_cast<size_t> (k)]  = std::exp (-dist * dist / (2.0 * 0.15 * 0.15));
    }

    std::vector<double> hann (N);
    for (int i = 0; i < N; ++i)
        hann[static_cast<size_t> (i)] = 0.5 - 0.5 * std::cos (2.0 * dsp::kPi * i / (N - 1));

    std::vector<std::complex<double>> buf (N);
    std::vector<double> mag (N / 2, 0.0), prevMag (N / 2, 0.0);
    std::vector<int> frameCount (out.steps.size(), 0);

    const double startSample = std::max (sampleAtPpq (firstBar * 4.0), N / 2.0);
    const double endSample   = sampleAtPpq (endBar * 4.0);
    for (double c = startSample; c + N / 2.0 < static_cast<double> (n) && c < endSample; c += hop)
    {
        const size_t s0 = static_cast<size_t> (c - N / 2.0);
        for (int i = 0; i < N; ++i)
            buf[static_cast<size_t> (i)] = { a.samples[s0 + static_cast<size_t> (i)] * hann[static_cast<size_t> (i)], 0.0 };
        dsp::fft (buf);

        std::array<double, 12> ch {};
        double e = 0.0, flux = 0.0;
        for (int k = 1; k < N / 2; ++k)
        {
            const size_t ku = static_cast<size_t> (k);
            mag[ku] = std::abs (buf[ku]);
            if (binPc[ku] >= 0) ch[static_cast<size_t> (binPc[ku])] += mag[ku] * binW[ku];
            const double f = k * sr / N;
            if (f >= 150.0 && f <= 4000.0)
            {
                const double d = mag[ku] - prevMag[ku];
                if (d > 0.0) flux += d;
            }
            e += mag[ku] * mag[ku];
        }
        std::swap (mag, prevMag);

        const double ppq  = a.ppq0 + c * a.beatsPerSample;
        const long   step = static_cast<long> (std::floor (ppq * 4.0)) - firstBar * 16;
        if (step < 0 || step >= static_cast<long> (out.steps.size())) continue;
        auto& sf = out.steps[static_cast<size_t> (step)];
        for (size_t p = 0; p < 12; ++p) sf.chroma[p] += ch[p];
        sf.energy += e;
        sf.onset = std::max (sf.onset, flux);
        frameCount[static_cast<size_t> (step)]++;
    }
    for (size_t i = 0; i < out.steps.size(); ++i)
        if (frameCount[i] > 0)
        {
            for (auto& v : out.steps[i].chroma) v /= frameCount[i];
            out.steps[i].energy /= frameCount[i];
        }

    // ── サンプル自身のベースライン: 280 Hz 4 次 LPF → ~2.7 kHz へ間引き → YIN
    const int D = std::max (1, static_cast<int> (std::lround (sr / 2756.25)));
    const double srB = sr / D;
    std::vector<float> low;
    low.reserve (n / static_cast<size_t> (D) + 1);
    auto lp1 = dsp::Biquad::lowpass (sr, 280.0);
    auto lp2 = dsp::Biquad::lowpass (sr, 280.0);
    for (size_t i = 0; i < n; ++i)
    {
        const float y = lp2.process (lp1.process (a.samples[i]));
        if (i % static_cast<size_t> (D) == 0) low.push_back (y);
    }

    const size_t W = static_cast<size_t> (std::lround (srB * 0.19));   // 約 190 ms
    std::vector<double> rms (out.steps.size(), 0.0);
    std::vector<long>   win (out.steps.size(), -1);
    double maxRms = 0.0;
    for (size_t st = 0; st < out.steps.size(); ++st)
    {
        const double ppqC = (firstBar * 16 + static_cast<long> (st) + 0.5) * 0.25;
        const long s0 = static_cast<long> (sampleAtPpq (ppqC) / D - W / 2.0);
        if (s0 < 0 || static_cast<size_t> (s0) + W >= low.size()) continue;
        double r = 0.0;
        for (size_t j = 0; j < W; ++j) r += static_cast<double> (low[static_cast<size_t> (s0) + j]) * low[static_cast<size_t> (s0) + j];
        rms[st] = std::sqrt (r / W);
        win[st] = s0;
        maxRms = std::max (maxRms, rms[st]);
    }
    for (size_t st = 0; st < out.steps.size(); ++st)
    {
        if (win[st] < 0 || maxRms <= 0.0 || rms[st] < 0.12 * maxRms) continue;
        double conf = 0.0;
        const double f0 = dsp::yinF0 (&low[static_cast<size_t> (win[st])], W, srB, 30.0, 260.0, 0.2, conf);
        if (f0 > 0.0)
        {
            out.steps[st].bassMidi = static_cast<int> (std::lround (dsp::hzToMidi (f0)));
            out.steps[st].bassConf = conf;
        }
    }
    return out;
}

// ── 解析結果（ソング小節基準に位相を揃えた 1 ループ分）─────────────
struct HarmonyAnalysis
{
    bool ok       = false;
    int  keyRoot  = 0;
    int  keyMode  = 0;
    int  loopBars = 0;                    // L（1/2/4/8/16）。index 0 = ソング小節 ≡ 0 (mod L)
    std::vector<ChordSeg> halfBar;        // 2L 個（2 拍ごと）
    std::vector<ChordSeg> perBar;         // L 個
    std::vector<int>      bassMidi;       // 16L 個（-1 = 無声）
    std::array<double, 16> onsetHist {};
    bool hasOnset = false;
};

namespace detail
{
    // 小節 b の特徴ベクトル（16 ステップ × 正規化クロマ + ベース音の pc）
    inline std::vector<double> barVector (const AudioFeatures& f, int b)
    {
        std::vector<double> v (16 * 24, 0.0);
        for (int st = 0; st < 16; ++st)
        {
            const auto& sf = f.steps[static_cast<size_t> (b * 16 + st)];
            double norm = 0.0;
            for (double c : sf.chroma) norm += c * c;
            norm = std::sqrt (norm);
            if (norm > 0.0)
                for (int p = 0; p < 12; ++p)
                    v[static_cast<size_t> (st * 24 + p)] = sf.chroma[static_cast<size_t> (p)] / norm;
            if (sf.bassMidi >= 0)
                v[static_cast<size_t> (st * 24 + 12 + ((sf.bassMidi % 12) + 12) % 12)] = 1.0;
        }
        return v;
    }

    inline double cosine (const std::vector<double>& a, const std::vector<double>& b)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (size_t i = 0; i < a.size(); ++i) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; }
        return (aa > 0.0 && bb > 0.0) ? ab / std::sqrt (aa * bb) : 0.0;
    }

    // ソング小節 s に対する「ループ内位置」= s mod L（負にも対応）
    inline int loopPos (long songBar, int L) { return static_cast<int> (((songBar % L) + L) % L); }
} // namespace detail

inline HarmonyAnalysis analyzeHarmony (const AudioFeatures& f)
{
    HarmonyAnalysis r;
    if (f.numBars <= 0) return r;

    double totalE = 0.0, maxStepE = 0.0;
    for (const auto& sf : f.steps) { totalE += sf.energy; maxStepE = std::max (maxStepE, sf.energy); }
    if (totalE <= 1e-9) return r;   // 無音 → R8 相当

    // キー（Temperley プロファイル、クロマの総和）
    std::array<double, 12> hist {};
    for (const auto& sf : f.steps)
        for (size_t p = 0; p < 12; ++p) hist[p] += sf.chroma[p];
    const auto key = detectKeyTemperley (hist);
    r.keyRoot = key.root;
    r.keyMode = key.mode;

    // 和音（2 拍・1 小節）。サンプル自身のベース音（YIN）をルートの証拠として渡す。
    auto chordsAt = [&f, &r, maxStepE] (int stepsPerSeg, double segBeats)
    {
        const int numSeg = f.numBars * 16 / stepsPerSeg;
        std::vector<std::array<double, 12>> segPch (static_cast<size_t> (numSeg));
        std::vector<bool> segHas (static_cast<size_t> (numSeg), false);
        std::vector<int>  segBass (static_cast<size_t> (numSeg), -1);
        for (int s = 0; s < numSeg; ++s)
        {
            double segE = 0.0;
            std::array<double, 12> bassW {};
            double bassTotal = 0.0;
            for (int st = 0; st < stepsPerSeg; ++st)
            {
                const auto& sf = f.steps[static_cast<size_t> (s * stepsPerSeg + st)];
                for (size_t p = 0; p < 12; ++p) segPch[static_cast<size_t> (s)][p] += sf.chroma[p];
                segE = std::max (segE, sf.energy);
                if (sf.bassMidi >= 0)
                {
                    bassW[static_cast<size_t> (((sf.bassMidi % 12) + 12) % 12)] += sf.bassConf;
                    bassTotal += sf.bassConf;
                }
            }
            segHas[static_cast<size_t> (s)] = segE > 0.02 * maxStepE;
            // 有声ステップの過半を占めるベース音のみ採用（あいまいなら証拠にしない）
            int bp = -1; double bw = 0.0;
            for (int p = 0; p < 12; ++p) if (bassW[static_cast<size_t> (p)] > bw) { bw = bassW[static_cast<size_t> (p)]; bp = p; }
            if (bp >= 0 && bassTotal > 0.0 && bw / bassTotal >= 0.5) segBass[static_cast<size_t> (s)] = bp;
        }
        return viterbiChordsFromPch (segPch, segHas, segBeats, r.keyRoot, r.keyMode, segBass);
    };
    const auto half = chordsAt (8, 2.0);
    const auto bar  = chordsAt (16, 4.0);

    // ループ長（小節ベクトルのコサイン類似, 候補 1/2/4/8/16）
    std::vector<std::vector<double>> bv;
    for (int b = 0; b < f.numBars; ++b) bv.push_back (detail::barVector (f, b));
    int L = 0;
    double bestAvg = -1.0;
    int bestP = 0;
    for (int p : { 1, 2, 4, 8, 16 })
    {
        if (p >= f.numBars) continue;
        double sum = 0.0; int cnt = 0;
        for (int b = p; b < f.numBars; ++b) { sum += detail::cosine (bv[static_cast<size_t> (b)], bv[static_cast<size_t> (b - p)]); ++cnt; }
        const double avg = cnt > 0 ? sum / cnt : 0.0;
        if (avg >= 0.88) { L = p; break; }
        if (avg > bestAvg) { bestAvg = avg; bestP = p; }
    }
    if (L == 0)
    {
        if (bestAvg >= 0.75) L = bestP;
        else { L = 1; while (L * 2 <= std::min (f.numBars, 16)) L *= 2; }
    }
    r.loopBars = L;

    // 位相合わせ: ループ内位置 i（= ソング小節 mod L）ごとに全出現を多数決
    r.halfBar.assign (static_cast<size_t> (2 * L), ChordSeg { 0.0, 2.0, r.keyRoot, r.keyMode == 0 ? "maj" : "min" });
    r.perBar.assign  (static_cast<size_t> (L),     ChordSeg { 0.0, 4.0, r.keyRoot, r.keyMode == 0 ? "maj" : "min" });
    r.bassMidi.assign (static_cast<size_t> (16 * L), -1);

    for (int i = 0; i < L; ++i)
    {
        std::array<std::map<int, int>, 2> halfVotes;
        std::map<int, int> barVotes;
        std::array<std::map<int, int>, 16> bassVotes;
        for (int b = 0; b < f.numBars; ++b)
        {
            if (detail::loopPos (f.firstBar + b, L) != i) continue;
            for (int h = 0; h < 2; ++h)
            {
                const auto& c = half[static_cast<size_t> (b * 2 + h)];
                halfVotes[static_cast<size_t> (h)][c.root * 2 + (c.quality == "min" ? 1 : 0)]++;
            }
            const auto& cb = bar[static_cast<size_t> (b)];
            barVotes[cb.root * 2 + (cb.quality == "min" ? 1 : 0)]++;
            for (int st = 0; st < 16; ++st)
            {
                const int m = f.steps[static_cast<size_t> (b * 16 + st)].bassMidi;
                if (m >= 0) bassVotes[static_cast<size_t> (st)][m]++;
            }
        }
        auto argmax = [] (const std::map<int, int>& m, int fallback)
        {
            int best = fallback, bc = -1;
            for (const auto& kv : m) if (kv.second > bc) { bc = kv.second; best = kv.first; }
            return best;
        };
        for (int h = 0; h < 2; ++h)
        {
            const int st = argmax (halfVotes[static_cast<size_t> (h)], -1);
            if (st >= 0)
                r.halfBar[static_cast<size_t> (i * 2 + h)] = { (i * 2 + h) * 2.0, 2.0, st / 2, st % 2 ? "min" : "maj" };
        }
        const int sb = argmax (barVotes, -1);
        if (sb >= 0) r.perBar[static_cast<size_t> (i)] = { i * 4.0, 4.0, sb / 2, sb % 2 ? "min" : "maj" };
        for (int st = 0; st < 16; ++st)
            r.bassMidi[static_cast<size_t> (i * 16 + st)] = argmax (bassVotes[static_cast<size_t> (st)], -1);
    }

    // うわネタのオンセット密度（コール&レスポンス用、1 小節 16 スロットに畳む）
    std::array<double, 16> oh {};
    for (size_t i = 0; i < f.steps.size(); ++i) oh[i % 16] += f.steps[i].onset;
    double mx = 0.0;
    for (double v : oh) mx = std::max (mx, v);
    if (mx > 0.0)
    {
        for (auto& v : oh) v /= mx;
        r.onsetHist = oh;
        r.hasOnset  = true;
    }

    r.ok = true;
    return r;
}

} // namespace kemuri::core
