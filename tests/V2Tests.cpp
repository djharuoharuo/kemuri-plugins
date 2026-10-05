// v2 解析・生成のテスト（JUCE 非依存）。
//  [解析] 合成オーディオ → 和音（2拍/1小節）・キー・サンプル自身のベース・ループ長・
//         ソング小節基準の位相（途中から取り込んでも同じ結果）・ノイズ耐性、ドラム MIDI → キック/スイング
//  [生成] キック + ネタ → ベース（和音一致・1拍目=ルート・キック同期・音域・ループ反復・密度・多様性）
#include <kemuri_core/AudioAnalyzer.h>
#include <kemuri_core/BassEngineV2.h>
#include <kemuri_core/DrumAnalyzer.h>

#include <cstdio>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <string>

using namespace kemuri::core;

namespace analyzer_tests {



static const char* NN[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
static double m2hz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }

struct Scene
{
    double sr = 11025.0, bpm = 90.0;
    // chordAt(ppq) -> 3 MIDI notes, bassAt(ppq) -> MIDI or -1
};

// ネタのオーディオを合成（和音 = 倍音付き持続音、ベース = 4 分で弾き直す低音）
static CapturedAudio synth (double ppqStart, int bars,
                            const std::function<std::array<int, 3> (double)>& chordAt,
                            const std::function<int (double)>& bassAt,
                            double noise, bool drums, std::uint32_t seed)
{
    CapturedAudio a;
    a.sampleRate = 11025.0;
    const double bpm = 90.0;
    a.beatsPerSample = (bpm / 60.0) / a.sampleRate;
    a.ppq0 = ppqStart;
    const size_t n = static_cast<size_t> (bars * 4.0 / a.beatsPerSample);
    a.samples.resize (n);
    std::mt19937 rng (seed);
    std::normal_distribution<double> g (0.0, 1.0);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = i / a.sampleRate;
        const double ppq = ppqStart + i * a.beatsPerSample;
        double s = 0.0;
        const auto ch = chordAt (ppq);
        for (int m : ch)
            for (int h = 1; h <= 4; ++h)
                s += 0.12 / h * std::sin (2.0 * dsp::kPi * m2hz (m) * h * t);
        const int b = bassAt (ppq);
        if (b >= 0)
        {
            const double beatPos = std::fmod (ppq, 1.0);
            const double env = std::exp (-beatPos * 2.0);
            s += env * (0.5 * std::sin (2.0 * dsp::kPi * m2hz (b) * t) + 0.2 * std::sin (2.0 * dsp::kPi * m2hz (b) * 2 * t));
        }
        if (drums)
        {
            const double p = std::fmod (ppq, 1.0);
            if (p < 0.05) s += 0.6 * g (rng) * (1.0 - p / 0.05);   // ノイズのアタック（ドラム混入）
        }
        s += noise * g (rng);
        a.samples[i] = static_cast<float> (s);
    }
    return a;
}

static int failures = 0;
static void expect (bool c, const char* label) { std::printf ("%s %s\n", c ? "  ok  " : "  FAIL", label); if (! c) ++failures; }

static void printH (const HarmonyAnalysis& h)
{
    std::printf ("  key=%s%s loop=%d bars | perBar:", NN[h.keyRoot], h.keyMode ? "m" : "", h.loopBars);
    for (const auto& c : h.perBar) std::printf (" %s%s", NN[c.root], c.quality == "min" ? "m" : "");
    std::printf (" | halfBar:");
    for (const auto& c : h.halfBar) std::printf (" %s%s", NN[c.root], c.quality == "min" ? "m" : "");
    std::printf ("\n  bass(step0 of each loop bar):");
    for (int i = 0; i < h.loopBars; ++i) { const int m = h.bassMidi[static_cast<size_t> (i * 16)]; std::printf (" %s", m >= 0 ? NN[m % 12] : "-"); }
    std::printf ("\n");
}

int run()
{
    // 2 小節ループ: 偶数小節 Am / 奇数小節 F、ベース A1 / F1
    auto chord2 = [] (double ppq) -> std::array<int, 3>
    { const long bar = static_cast<long> (std::floor (ppq / 4.0)); return (bar % 2 == 0) ? std::array<int, 3> { 57, 60, 64 } : std::array<int, 3> { 53, 57, 60 }; };
    auto bass2 = [] (double ppq) -> int
    { const long bar = static_cast<long> (std::floor (ppq / 4.0)); return (bar % 2 == 0) ? 33 : 29; };

    std::printf ("[A] 2小節ループ Am|F、ソング0小節目から取り込み\n");
    {
        const auto a = synth (0.0, 8, chord2, bass2, 0.0, false, 1);
        const auto h = analyzeHarmony (computeAudioFeatures (a));
        printH (h);
        expect (h.ok && h.loopBars == 2, "loop = 2");
        expect (h.perBar.size() == 2 && h.perBar[0].root == 9 && h.perBar[0].quality == "min", "loop bar0 = Am");
        expect (h.perBar.size() == 2 && h.perBar[1].root == 5 && h.perBar[1].quality == "maj", "loop bar1 = F");
        expect (h.bassMidi.size() == 32 && h.bassMidi[0] % 12 == 9 && h.bassMidi[16] % 12 == 5, "sample bass A / F");
    }

    std::printf ("[B] 同じネタを ソング5小節目の途中(ppq 21.3)から取り込み → 位相はソング基準で同じ結果のはず\n");
    {
        const auto a = synth (21.3, 9, chord2, bass2, 0.0, false, 2);
        const auto h = analyzeHarmony (computeAudioFeatures (a));
        printH (h);
        expect (h.loopBars == 2 && h.perBar[0].root == 9 && h.perBar[1].root == 5, "phase anchored to song bar 0 (Am first)");
    }

    std::printf ("[C] 小節内で 2拍ごとに Am→F（1小節ループ）\n");
    {
        auto chordHalf = [] (double ppq) -> std::array<int, 3>
        { return (std::fmod (ppq, 4.0) < 2.0) ? std::array<int, 3> { 57, 60, 64 } : std::array<int, 3> { 53, 57, 60 }; };
        auto bassHalf = [] (double ppq) -> int { return (std::fmod (ppq, 4.0) < 2.0) ? 33 : 29; };
        const auto a = synth (0.0, 6, chordHalf, bassHalf, 0.0, false, 3);
        const auto h = analyzeHarmony (computeAudioFeatures (a));
        printH (h);
        expect (h.loopBars == 1, "loop = 1");
        expect (h.halfBar.size() == 2 && h.halfBar[0].root == 9 && h.halfBar[1].root == 5, "half-bar Am | F");
        expect (h.bassMidi.size() == 16 && h.bassMidi[0] % 12 == 9 && h.bassMidi[8] % 12 == 5, "sample bass follows mid-bar change");
    }

    std::printf ("[D] [A] + ノイズ + ドラムのアタック混入\n");
    {
        const auto a = synth (0.0, 8, chord2, bass2, 0.05, true, 4);
        const auto h = analyzeHarmony (computeAudioFeatures (a));
        printH (h);
        expect (h.loopBars == 2 && h.perBar[0].root == 9 && h.perBar[1].root == 5, "robust to noise + drum bleed");
    }

    std::printf ("[E] ドラム MIDI からキック検出（キック 36 @ 0/1.75/2.5、スネア 38、16分ハット swing 58%%）\n");
    {
        std::vector<RawEvent> ev;
        for (int bar = 3; bar < 11; ++bar)
        {
            const double b0 = bar * 4.0;
            for (double k : { 0.0, 1.75, 2.5 }) { ev.push_back ({ b0 + k, 36, true }); ev.push_back ({ b0 + k + 0.1, 36, false }); }
            for (double sn : { 1.0, 3.0 }) { ev.push_back ({ b0 + sn, 38, true }); ev.push_back ({ b0 + sn + 0.1, 38, false }); }
            for (int s = 0; s < 16; ++s)
            {
                const double pos = s * 0.25 + ((s % 2) ? 0.04 : 0.0);   // 0.29 = swing 58%
                ev.push_back ({ b0 + pos, 42, true }); ev.push_back ({ b0 + pos + 0.05, 42, false });
            }
        }
        std::sort (ev.begin(), ev.end(), [] (const RawEvent& x, const RawEvent& y) { return x.ppq < y.ppq; });
        const auto k = analyzeKick (ev, 3, 11);
        std::printf ("  kickNote=%d loop=%d swing=%d%% bar0:", k.kickNote, k.loopBars, k.swingPercent);
        if (! k.bars.empty()) for (double p : k.bars[0]) std::printf (" %.2f", p);
        std::printf ("\n");
        expect (k.ok && k.kickNote == 36, "kick note = 36");
        expect (k.loopBars == 1 && k.bars[0].size() == 3, "kick loop 1 bar, 3 hits");
        expect (k.hasSwing && std::abs (k.swingPercent - 58) <= 1, "swing ~58%");
    }

    // 実機（2026-10-04）: 解析区間 16 小節のうちドラムが届いたのは後半だけ、しかも最後の小節は
    // Analyze を押した時点で途中まで。2 小節パターン A = 0/1.5/2.6、B = 0/0.5/1.5/2.6/3.4。
    std::printf ("[F] ドラムが途中から届いた + 最後の小節が途中まで\n");
    {
        std::vector<RawEvent> ev;
        for (int bar = 9; bar <= 16; ++bar)
        {
            const double b0 = bar * 4.0;
            const auto& pat = (bar % 2 == 0) ? std::vector<double> { 0.0, 1.5, 2.6 } : std::vector<double> { 0.0, 0.5, 1.5, 2.6, 3.4 };
            for (double k : pat)
            {
                if (bar == 16 && k > 0.2) break;   // 最後の小節は 1 拍目だけ届いた
                ev.push_back ({ b0 + k, 36, true }); ev.push_back ({ b0 + k + 0.1, 36, false });
            }
            if (bar < 16) for (double sn : { 1.0, 3.0 }) { ev.push_back ({ b0 + sn, 38, true }); ev.push_back ({ b0 + sn + 0.1, 38, false }); }
        }
        std::sort (ev.begin(), ev.end(), [] (const RawEvent& x, const RawEvent& y) { return x.ppq < y.ppq; });
        const auto k = analyzeKick (ev, 1, 17);
        std::printf ("  loop=%d", k.loopBars);
        for (size_t i = 0; i < k.bars.size(); ++i) { std::printf (" | slot%zu:", i); for (double p : k.bars[i]) std::printf (" %.2f", p); }
        std::printf ("\n");
        bool full = k.ok && k.loopBars == 2 && k.bars.size() == 2;
        if (full) full = k.bars[0].size() == 3 && k.bars[1].size() == 5;   // ソング小節偶数 = A, 奇数 = B
        expect (full, "loop 2 bars, both slots = the full patterns (no empty / partial bars)");
    }

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
} // namespace analyzer_tests

namespace generator_tests {



static const char* NN[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
static int failures = 0;
static void expect (bool c, const std::string& label) { std::printf ("%s %s\n", c ? "  ok  " : "  FAIL", label.c_str()); if (! c) ++failures; }

// 合成解析結果: 2 小節ループ Am | F、サンプルのベース A1 / F1（4 分で鳴る）
static HarmonyAnalysis makeHarmony (bool withBass)
{
    HarmonyAnalysis h;
    h.ok = true; h.keyRoot = 9; h.keyMode = 1; h.loopBars = 2;
    h.halfBar = { { 0, 2, 9, "min" }, { 2, 2, 9, "min" }, { 4, 2, 5, "maj" }, { 6, 2, 5, "maj" } };
    h.perBar  = { { 0, 4, 9, "min" }, { 4, 4, 5, "maj" } };
    h.bassMidi.assign (32, -1);
    if (withBass)
        for (int st = 0; st < 32; ++st) h.bassMidi[static_cast<size_t> (st)] = (st < 16) ? 33 : 29;
    return h;
}
static KickAnalysis makeKick()
{
    KickAnalysis k; k.ok = true; k.kickNote = 36; k.loopBars = 1;
    k.bars = { { 0.0, 1.75, 2.5 } };
    return k;
}

static void dump (const V2Result& r, int barsToShow)
{
    std::printf ("  [%s] %s  bars=%d\n", producerName (r.producer), r.approach.c_str(), r.bars);
    for (int b = 0; b < barsToShow; ++b)
    {
        std::printf ("   bar%d:", b);
        for (const auto& n : r.notes)
            if (n.start >= b * 4 && n.start < b * 4 + 4)
                std::printf (" %s%d@%.2f(%.2f)", NN[n.pitch % 12], n.pitch / 12 - 1, n.start - b * 4, n.dur);
        std::printf ("\n");
    }
}

int run()
{
    const auto H  = makeHarmony (true);
    const auto Hn = makeHarmony (false);
    const auto K  = makeKick();

    // Am | F の 2 小節ループ。小節 b の和音のルート、次の小節のルート
    auto rootOf = [] (int b) { return (b % 2 == 0) ? 9 : 5; };
    // 音の役割: その時の和音の音 / 次の小節へ食った音（小節の 3 拍目以降で次のルート）
    auto isRootOrAnticip = [&rootOf] (const OutNote& n)
    {
        const int bar = static_cast<int> (std::floor (n.start / 4.0 + 1e-9));
        const double inBar = n.start - bar * 4.0;
        return n.pitch % 12 == rootOf (bar) || (inBar >= 3.0 && n.pitch % 12 == rootOf (bar + 1));
    };
    auto onAKick = [] (const OutNote& n)
    {
        const double inBar = n.start - std::floor (n.start / 4.0 + 1e-9) * 4.0;
        for (double kp : { 0.0, 1.75, 2.5, 4.0 }) if (std::abs (inBar - kp) < 0.13) return true;
        return false;
    };

    for (int style : { 1, 4, 3, 2 })
    {
        std::printf ("\n=== style %d ===\n", style);
        Rng rng (1234u + style);
        V2Config cfg; cfg.style = style; cfg.bars = 8; cfg.complexity = 30; cfg.fill = 20;
        cfg.harmony = &H; cfg.kick = &K;
        const auto r = buildBassV2 (cfg, rng);
        dump (r, 8);

        // 1) 和音に合っている: その時の和音の構成音(+♭7) か、次の和音へ食った音
        int inChord = 0, total = 0;
        for (const auto& n : r.notes)
        {
            const int bar = static_cast<int> (n.start / 4.0);
            const std::set<int> tones = (bar % 2 == 0) ? std::set<int> { 9, 0, 4, 7 } : std::set<int> { 5, 9, 0, 3 };   // + ♭7
            ++total; if (tones.count (n.pitch % 12) || isRootOrAnticip (n)) ++inChord;
        }
        expect (total > 0 && inChord * 100 / total >= 75, "音の75%以上が和音構成音(+♭7)か食った音  " + std::to_string (inChord) + "/" + std::to_string (total));

        // 2) 各小節の 1 拍目で鳴っている音 = その小節の和音のルート（食ってつないだ音も含む）
        bool rootsOk = true;
        for (int b = 0; b < r.bars; ++b)
        {
            const OutNote* sounding = nullptr;
            for (const auto& n : r.notes)
                if (n.start <= b * 4 + 0.15 && n.start + n.dur > b * 4 + 0.01 && (! sounding || n.start > sounding->start)) sounding = &n;
            if (! sounding || sounding->pitch % 12 != rootOf (b)) rootsOk = false;
        }
        expect (rootsOk, "各小節の1拍目に鳴っている音 = その小節の和音のルート（位相も正しい）");

        // 3) キックとの関係: キック位置（0/1.75/2.5）にベースが始まる割合
        //    （伸ばしたまま・食ってつなぐ分は重ならない。9th は特に少ない）
        int onKick = 0, kicks = 0;
        for (int b = 0; b < 2; ++b)
            for (double kp : { 0.0, 1.75, 2.5 })
            {
                ++kicks;
                for (const auto& n : r.notes) if (std::abs (n.start - (b * 4 + kp)) < 0.13) { ++onKick; break; }
            }
        const int needPct = (style == 3) ? 30 : (style == 1 ? 80 : 60);
        expect (onKick * 100 / kicks >= needPct, "キック位置にベース " + std::to_string (onKick) + "/" + std::to_string (kicks));

        // 4) 音域
        bool reg = true; for (const auto& n : r.notes) if (n.pitch < 28 || n.pitch > 40) reg = false;
        expect (reg, "音域 E1..E2（高い音なし）");

        // 5) ループが毎回同じ（フレーズの最後でない bar1 と bar5）
        auto barSig = [&r] (int b) { std::string s; for (const auto& n : r.notes) if (n.start >= b * 4 && n.start < b * 4 + 4) s += std::to_string (n.pitch) + "@" + std::to_string (std::lround ((n.start - b * 4) * 100)) + " "; return s; };
        expect (barSig (1) == barSig (5), "bar1 == bar5（ループの完全反復）");

        // 6) 密度
        const double npb = static_cast<double> (r.notes.size()) / r.bars;
        expect (npb >= 1.0 && npb <= 4.0, "密度 " + std::to_string (npb).substr (0, 4) + " 音/小節（ブームバップ 2〜4）");
        bool perBarOk = true;
        for (int b = 0; b < r.bars; ++b)
        {
            int cnt = 0;
            for (const auto& n : r.notes) if (n.start >= b * 4 && n.start < b * 4 + 4) ++cnt;
            if (cnt > 4 + ((b % 4 == 3) ? 1 : 0)) perBarOk = false;   // フレーズの最後だけ経過音 +1 まで
        }
        expect (perBarOk, "どの小節も 4 音まで（フレーズの最後は +1）");

        // 7) Complexity 30 では、フレーズの最後以外はルートか食った次のルートだけ
        bool allRoots = true;
        for (const auto& n : r.notes)
            if (static_cast<int> (n.start / 4.0) % 4 != 3 && ! isRootOrAnticip (n)) allRoots = false;
        expect (allRoots, "フレーズの最後以外はルートだけ（音程が跳ねない）");

        // 8) Complexity 0 では全部キックの上（フレーズの最後の経過音を除く）
        {
            Rng rng0 (99u + style);
            V2Config c0 = cfg; c0.complexity = 0;
            const auto r0 = buildBassV2 (c0, rng0);
            bool allOnKick = true;
            for (const auto& n : r0.notes)
            {
                const int bar = static_cast<int> (n.start / 4.0);
                if (bar % 4 == 3 && n.start - bar * 4.0 >= 2.9) continue;
                if (! onAKick (n)) allOnKick = false;
            }
            expect (allOnKick, "Complexity 0 = 全部キックの上");
        }
    }

    // 9) キックと重ならない音: 既定（Complexity 30）で 40 回生成し、フレーズの最後以外で
    //    キックから外れた音（キックの間・食い）が出る割合
    std::printf ("\n=== キックと重ならない音（Complexity 30, 40 回）===\n");
    for (int style : { 1, 4, 3, 2 })
    {
        int withOff = 0;
        for (int sd = 0; sd < 40; ++sd)
        {
            Rng rng (static_cast<std::uint32_t> (sd) * 7919u + 11u);
            V2Config cfg; cfg.style = style; cfg.bars = 8; cfg.complexity = 30; cfg.fill = 20;
            cfg.harmony = &H; cfg.kick = &K;
            const auto r = buildBassV2 (cfg, rng);
            for (const auto& n : r.notes)
                if (static_cast<int> (n.start / 4.0) % 4 != 3 && ! onAKick (n)) { ++withOff; break; }
        }
        std::printf ("  style %d: %d/40\n", style, withOff);
        if (style == 4 || style == 2) expect (withOff >= 20, "Pete / Dilla はキックから外れる音がふつうに出る");
        if (style == 1) expect (withOff <= 24, "Premier はほぼキックの上");
    }

    // 10) サンプルにベースが無いネタでも同じ（ルートだけ）
    std::printf ("\n=== サンプルにベースが無い場合 ===\n");
    {
        Rng rng (77u);
        V2Config cfg; cfg.style = 1; cfg.harmony = &Hn; cfg.kick = &K; cfg.complexity = 30;
        const auto r = buildBassV2 (cfg, rng);
        dump (r, 2);
        bool ok = ! r.notes.empty();
        for (const auto& n : r.notes)
            if (static_cast<int> (n.start / 4.0) < 3 && ! isRootOrAnticip (n)) ok = false;
        expect (ok, "ベース無しのネタでも和音のルート");
    }

    // 9) 実機のケース（2026-10-04 の Live）: G#m/D# | G#m/A#m、キック 1・2 の裏・3 の裏 / 1・1 の裏・2 の裏・3 の裏・4 の裏
    std::printf ("\n=== 実機ケース G#m/D# | G#m/A#m ===\n");
    {
        HarmonyAnalysis h;
        h.ok = true; h.keyRoot = 8; h.keyMode = 1; h.loopBars = 2;
        h.halfBar = { { 0, 2, 8, "min" }, { 2, 2, 3, "maj" }, { 4, 2, 8, "min" }, { 6, 2, 10, "min" } };
        h.perBar  = { { 0, 4, 8, "min" }, { 4, 4, 8, "min" } };
        h.bassMidi.assign (32, -1);
        KickAnalysis k; k.ok = true; k.kickNote = 36; k.loopBars = 2;
        k.bars = { { 0.0, 1.5, 2.6 }, { 0.0, 0.5, 1.5, 2.6, 3.4 } };
        for (int style : { 4, 1, 3, 2 })
        {
            Rng rng (2026u + style);
            V2Config cfg; cfg.style = style; cfg.bars = 8; cfg.complexity = 30; cfg.fill = 20;
            cfg.harmony = &h; cfg.kick = &k;
            const auto r = buildBassV2 (cfg, rng);
            dump (r, 4);
            const double npb = static_cast<double> (r.notes.size()) / r.bars;
            int distinctMax = 0;
            for (int b = 0; b < r.bars; ++b)
            {
                if (b % 4 == 3) continue;
                std::set<int> ps;
                for (const auto& n : r.notes) if (n.start >= b * 4 && n.start < b * 4 + 4) ps.insert (n.pitch);
                distinctMax = std::max (distinctMax, static_cast<int> (ps.size()));
            }
            expect (npb <= 4.0, std::string (producerName (r.producer)) + ": " + std::to_string (npb).substr (0, 4) + " 音/小節");
            expect (distinctMax <= 2, std::string (producerName (r.producer)) + ": 1 小節の音程は和音の数（2）まで");
        }
    }

    // 8) 多様性: 同じネタ・同じキックで 50 回生成 → アプローチ/音形の種類
    std::printf ("\n=== 多様性（同じネタとキック、50回）===\n");
    for (int style : { 0, 1, 2, 3, 4 })
    {
        std::set<std::string> outs, approaches;
        for (int s = 0; s < 50; ++s)
        {
            Rng rng (static_cast<std::uint32_t> (s) * 2654435761u + 5u);
            V2Config cfg; cfg.style = style; cfg.bars = 4; cfg.complexity = 40; cfg.fill = 40;
            cfg.harmony = &H; cfg.kick = &K;
            const auto r = buildBassV2 (cfg, rng);
            std::string sig;
            for (const auto& n : r.notes) sig += std::to_string (n.pitch) + "@" + std::to_string (std::lround (n.start * 100)) + ":" + std::to_string (std::lround (n.dur * 100)) + " ";
            outs.insert (sig);
            approaches.insert (std::string (producerName (r.producer)) + "/" + r.approach);
        }
        std::printf ("  style %d: 出力の種類 %zu/50, アプローチ %zu 種\n", style, outs.size(), approaches.size());
    }

    // 9) ドラム入力なし → 定番キック
    std::printf ("\n=== ドラム入力なし（定番キック）===\n");
    {
        Rng rng (5u);
        V2Config cfg; cfg.style = 1; cfg.harmony = &H; cfg.kick = nullptr;
        const auto r = buildBassV2 (cfg, rng);
        dump (r, 2);
        expect (! r.usedUserKick && ! r.notes.empty(), "定番キックで生成");
    }

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
} // namespace generator_tests

int main()
{
    const int a = analyzer_tests::run();
    const int g = generator_tests::run();
    return (a == 0 && g == 0) ? 0 : 1;
}
