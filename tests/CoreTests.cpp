// kemuri_core 単体テスト。
// G3: 決定的サブコンポーネントが JS 正本から抽出した参照ベクトル
// (tests/reference/*.json) と一致することを検証する。
#include <juce_core/juce_core.h>

#include <kemuri_core/ChordDetector.h>
#include <kemuri_core/Generators.h>
#include <kemuri_core/InteractionScorer.h>
#include <kemuri_core/KeyContext.h>
#include <kemuri_core/KeyDetector.h>
#include <kemuri_core/LoopDetector.h>
#include <kemuri_core/MidiAnalyzer.h>
#include <kemuri_core/MusicTheory.h>
#include <kemuri_core/PatternLibrary.h>
#include <kemuri_core/PhrasePlanner.h>
#include <kemuri_core/PitchToken.h>
#include <kemuri_core/Rng.h>
#include <kemuri_core/Version.h>

#include <array>
#include <cstdio>
#include <map>
#include <string>

using namespace kemuri::core;

namespace
{
int failures = 0;
int checks   = 0;

void expect (bool cond, const juce::String& label)
{
    ++checks;
    if (! cond)
    {
        std::printf ("FAIL: %s\n", label.toRawUTF8());
        ++failures;
    }
}

juce::var loadRef (const juce::String& name)
{
    const juce::File dir (KEMURI_REFERENCE_DIR);
    const juce::File f = dir.getChildFile (name);
    const juce::var parsed = juce::JSON::parse (f.loadFileAsString());
    if (! parsed.isArray())
        std::printf ("FAIL: could not load/parse %s\n", name.toRawUTF8());
    return parsed;
}

const Pattern* findPattern (const juce::String& lib, const juce::String& name)
{
    const std::vector<Pattern>* v = nullptr;
    if (lib == "premier") v = &premierPatterns();
    else if (lib == "dilla") v = &dillaPatterns();
    else if (lib == "ninth") v = &ninthPatterns();
    else if (lib == "pete")  v = &peteRockPatterns();
    if (! v) return nullptr;
    for (const auto& p : *v)
        if (name == juce::String (p.name)) return &p;
    return nullptr;
}

PitchToken tokenFromVar (const juce::var& t)
{
    if (t.isString())
        return pitchTokenFromString (t.toString().toStdString());
    return PitchToken { Tok::Number, static_cast<int> (t) };
}

// ── Reference-vector tests ─────────────────────────────────────────

void testPitchTokens()
{
    const juce::var rows = loadRef ("pitch_tokens.json");
    for (int i = 0; i < rows.size(); ++i)
    {
        const juce::var& r = rows[i];
        const auto ctx  = makeKeyContext ((int) r["ctxRoot"],  (bool) r["ctxMinor"]);
        const auto nctx = makeKeyContext ((int) r["nextRoot"], (bool) r["nextMinor"]);
        const int got = resolvePitch (tokenFromVar (r["token"]), ctx, nctx);
        expect (got == (int) r["midi"],
                "pitch_tokens[" + juce::String (i) + "] token=" + r["token"].toString()
                    + " got=" + juce::String (got) + " want=" + r["midi"].toString());
    }
}

void testKeyContext()
{
    const juce::var rows = loadRef ("key_context.json");
    for (int i = 0; i < rows.size(); ++i)
    {
        const juce::var& r = rows[i];
        const auto c = makeKeyContext ((int) r["root"], (bool) r["minor"]);
        expect (c.lowAnchor == (int) r["lowAnchor"], "key_context lowAnchor row " + juce::String (i));
        expect (c.midAnchor == (int) r["midAnchor"], "key_context midAnchor row " + juce::String (i));
        const juce::var& sc = r["scale"];
        for (int j = 0; j < sc.size(); ++j)
            expect (c.scale[(size_t) j] == (int) sc[j], "key_context scale row " + juce::String (i));
        const juce::var& ch = r["chord"];
        for (int j = 0; j < ch.size(); ++j)
            expect (c.chord[(size_t) j] == (int) ch[j], "key_context chord row " + juce::String (i));
        const juce::var& pe = r["penta"];
        for (int j = 0; j < pe.size(); ++j)
            expect (c.penta[(size_t) j] == (int) pe[j], "key_context penta row " + juce::String (i));
    }
}

void testClampSnap()
{
    const juce::var rows = loadRef ("clamp_snap.json");
    for (int i = 0; i < rows.size(); ++i)
    {
        const juce::var& r = rows[i];
        const int in = (int) r["in"];
        const int want = (int) r["out"];
        const int got = (r["fn"].toString() == "clampBass")
                            ? clampBass (in)
                            : snapNear (in, (int) r["anchor"]);
        expect (got == want, "clamp_snap[" + juce::String (i) + "] got=" + juce::String (got)
                                 + " want=" + juce::String (want));
    }
}

void testPhraseFlags()
{
    const juce::var rows = loadRef ("phrase_flags.json");
    for (int i = 0; i < rows.size(); ++i)
    {
        const juce::var& r = rows[i];
        const auto f = computePhrase ((int) r["bars"], (int) r["fill"], (int) r["bar"]);
        const juce::String tag = "phrase_flags[" + juce::String (i) + "] ";
        expect (f.fillBars       == (int) r["fillBars"],           tag + "fillBars");
        expect (f.barInPhrase    == (int) r["barInPhrase"],        tag + "barInPhrase");
        expect (f.inDevHalf      == (bool) r["inDevHalf"],         tag + "inDevHalf");
        expect (f.isLastOfPhrase == (bool) r["isLastOfPhrase"],    tag + "isLastOfPhrase");
        expect (f.isDevelopment  == (bool) r["isDevelopment"],     tag + "isDevelopment");
        expect (f.midDevelopment == (bool) r["midDevelopment"],    tag + "midDevelopment");
        expect (f.isFill         == (bool) r["isFill"],            tag + "isFill");
    }
}

void testChordDetect()
{
    const juce::var rows = loadRef ("chord_detect.json");
    for (int i = 0; i < rows.size(); ++i)
    {
        const juce::var& r = rows[i];
        std::vector<RawNote> notes;
        const juce::var& nv = r["notes"];
        for (int j = 0; j < nv.size(); ++j)
        {
            const juce::var& n = nv[j];
            notes.push_back ({ (int) n[0], (double) n[1], (double) n[2] });
        }
        const auto prog = detectProgression (notes, (double) r["clipLen"], (double) r["segBeats"],
                                              (int) r["defaultRoot"], (int) r["defaultMode"]);
        const juce::var& want = r["prog"];
        expect ((int) prog.size() == want.size(),
                "chord_detect[" + juce::String (i) + "] segment count");
        for (int j = 0; j < want.size() && j < (int) prog.size(); ++j)
        {
            expect (prog[(size_t) j].root == (int) want[j]["root"],
                    "chord_detect[" + juce::String (i) + "] seg " + juce::String (j) + " root");
            expect (juce::String (prog[(size_t) j].quality) == want[j]["quality"].toString(),
                    "chord_detect[" + juce::String (i) + "] seg " + juce::String (j) + " quality");
        }
    }
}

void testKeyDetect()
{
    const juce::var rows = loadRef ("key_detect.json");
    for (int i = 0; i < rows.size(); ++i)
    {
        const juce::var& r = rows[i];
        std::array<double, 12> hist {};
        const juce::var& hv = r["hist"];
        for (int j = 0; j < 12 && j < hv.size(); ++j)
            hist[(size_t) j] = (double) hv[j];
        const auto res = detectKey (hist);
        expect (res.root == (int) r["bestRoot"], "key_detect[" + juce::String (i) + "] root");
        expect (res.mode == (int) r["bestMode"], "key_detect[" + juce::String (i) + "] mode");
    }
}

void testInteraction()
{
    const juce::var rows = loadRef ("interaction.json");
    for (int i = 0; i < rows.size(); ++i)
    {
        const juce::var& r = rows[i];
        const Pattern* pat = findPattern (r["lib"].toString(), r["pattern"].toString());
        expect (pat != nullptr, "interaction[" + juce::String (i) + "] pattern lookup");
        if (! pat) continue;
        std::array<double, 16> hist {};
        const juce::var& hv = r["hist"];
        for (int j = 0; j < 16 && j < hv.size(); ++j)
            hist[(size_t) j] = (double) hv[j];
        const double got = interactionScore (*pat, hist);
        expect (std::abs (got - (double) r["score"]) < 1e-9,
                "interaction[" + juce::String (i) + "] score got=" + juce::String (got, 6)
                    + " want=" + juce::String ((double) r["score"], 6));
    }
}

void testLoopDetect()
{
    const juce::var rows = loadRef ("loop_detect.json");
    for (int i = 0; i < rows.size(); ++i)
    {
        const juce::var& r = rows[i];
        std::vector<ChordSeg> prog;
        const juce::var& pv = r["prog"];
        for (int j = 0; j < pv.size(); ++j)
            prog.push_back ({ 0.0, 4.0, (int) pv[j][0], pv[j][1].toString().toStdString() });
        const int got = detectLoopBars (prog);
        expect (got == (int) r["loopBars"],
                "loop_detect[" + juce::String (i) + "] got=" + juce::String (got)
                    + " want=" + r["loopBars"].toString());
    }
}

// ── Stochastic generation: invariant / distribution checks (G3 後半) ──
// 乱数を含む全体出力は JS との完全一致を求めず、JS も満たす不変条件を検査する。
void testGeneration()
{
    for (int style = 0; style < 8; ++style)
    {
        for (int bars : { 4, 8, 16 })
        {
            int minCount = 1000000, maxCount = 0;
            for (int seed = 0; seed < 100; ++seed)
            {
                Rng rng (static_cast<std::uint32_t> (seed * 2654435761u + 1u));
                GenerateConfig cfg;
                cfg.style = style; cfg.bars = bars; cfg.complexity = 50; cfg.fill = 40;
                cfg.root = 4; cfg.mode = (style % 2);
                const auto notes = buildNotes (cfg, rng);

                const juce::String tag = "gen style=" + juce::String (style)
                                         + " bars=" + juce::String (bars)
                                         + " seed=" + juce::String (seed);
                expect (! notes.empty(), tag + " nonempty");

                const int count = static_cast<int> (notes.size());
                minCount = std::min (minCount, count);
                maxCount = std::max (maxCount, count);

                for (const auto& n : notes)
                {
                    expect (n.pitch >= kBassMin && n.pitch <= kBassMax, tag + " pitch in bass range");
                    expect (n.start >= 0.0 && n.start < bars * 4.0, tag + " start in clip");
                    expect (n.vel >= 1 && n.vel <= 127, tag + " velocity valid");
                    expect (n.dur > 0.0, tag + " positive duration");
                }
            }
            // ざっくりした密度帯: 1 小節あたり最低 0.5 ノート以上は出る
            expect (minCount >= bars / 2,
                    "gen style=" + juce::String (style) + " bars=" + juce::String (bars)
                        + " min note count " + juce::String (minCount));
        }
    }
}

// M3: MIDI 入力解析のオーケストレーション（R5 / R8）。
void testAnalyze()
{
    // 空入力 → hasInput=false（R8）
    expect (! analyzeNotes ({}).hasInput, "analyze empty → no input");

    // C major 全音符 → A minor 全音符
    std::vector<RawNote> notes;
    for (int p : { 48, 52, 55 }) notes.push_back ({ p, 0.0, 4.0 });
    for (int p : { 45, 48, 52 }) notes.push_back ({ p, 4.0, 4.0 });

    const auto res = analyzeNotes (notes);
    expect (res.hasInput, "analyze hasInput");
    expect (res.clipBars == 2, "analyze clipBars=2");
    expect (res.progBar.size() == 2, "analyze progBar size");
    if (res.progBar.size() == 2)
    {
        expect (res.progBar[0].root == 0 && res.progBar[0].quality == "maj", "analyze prog[0]=C maj");
        expect (res.progBar[1].root == 9 && res.progBar[1].quality == "min", "analyze prog[1]=A min");
    }

    // pairEvents: on/off ペアリングと windowStart オフセット
    std::vector<RawEvent> ev {
        { 100.0, 48, true }, { 100.0, 52, true },
        { 102.0, 48, false }, { 101.0, 52, false } };
    const auto paired = pairEvents (ev, 100.0, 104.0);
    expect (paired.size() == 2, "pairEvents count");
    // start は windowStart(100) 起点 → 0.0
    bool has48 = false, has52 = false;
    for (const auto& n : paired)
    {
        if (n.pitch == 48) { has48 = true; expect (std::abs (n.start - 0.0) < 1e-9 && std::abs (n.duration - 2.0) < 1e-9, "pairEvents 48 dur=2"); }
        if (n.pitch == 52) { has52 = true; expect (std::abs (n.duration - 1.0) < 1e-9, "pairEvents 52 dur=1"); }
    }
    expect (has48 && has52, "pairEvents pitches present");
}

// v1.1: ノート内容ベースのループ検出（2 小節ループ等）。
void testNoteLoop()
{
    // 4 小節で最初の 2 小節が繰り返す（bar0==bar2, bar1==bar3）
    std::vector<RawNote> notes;
    auto addBar = [&notes] (int bar, int p1, int p2)
    {
        notes.push_back ({ p1, bar * 4.0 + 0.0, 1.0 });
        notes.push_back ({ p2, bar * 4.0 + 2.0, 1.0 });
    };
    addBar (0, 40, 47); addBar (1, 45, 43);
    addBar (2, 40, 47); addBar (3, 45, 43);
    expect (detectNoteLoopBars (notes, 4) == 2, "note loop detects 2-bar");

    // 完全一致の 1 小節ループ
    std::vector<RawNote> uni;
    for (int b = 0; b < 4; ++b) uni.push_back ({ 40, b * 4.0, 1.0 });
    expect (detectNoteLoopBars (uni, 4) == 1, "note loop detects 1-bar");

    // 非周期
    std::vector<RawNote> aperi;
    for (int b = 0; b < 4; ++b) aperi.push_back ({ 40 + b, b * 4.0, 1.0 });
    expect (detectNoteLoopBars (aperi, 4) == 4, "note loop aperiodic → clipBars");
}

// v1.1: ループロック生成でループが実際に繰り返すこと（Soul-Jazz 以外）。
void testLoopLock()
{
    auto barSlice = [] (const std::vector<OutNote>& ns, int bar)
    {
        std::vector<std::pair<double, int>> out;
        for (const auto& n : ns)
            if (n.start >= bar * 4.0 - 1e-9 && n.start < (bar + 1) * 4.0 - 1e-9)
                out.push_back ({ n.start - bar * 4.0, n.pitch });
        std::sort (out.begin(), out.end());
        return out;
    };

    for (int style : { 0, 1, 4, 6, 7 })   // Soul-Jazz(5) は除外
    {
        Rng rng (static_cast<std::uint32_t> (style + 1) * 99991u);
        GenerateConfig cfg;
        cfg.style = style; cfg.bars = 4; cfg.complexity = 50; cfg.fill = 40; cfg.root = 0;
        const auto notes = buildNotes (cfg, rng);
        // L=2（既定）: bar0 と bar2 は同じ型（どちらも非展開）
        expect (barSlice (notes, 0) == barSlice (notes, 2),
                "loop-lock style " + juce::String (style) + " bar0==bar2");
    }

    // Soul-Jazz はロックしない（毎小節生成 → 通常は不一致、少なくともクラッシュしない）
    {
        Rng rng (12321u);
        GenerateConfig cfg; cfg.style = 5; cfg.bars = 4; cfg.complexity = 50;
        const auto notes = buildNotes (cfg, rng);
        expect (! notes.empty(), "soul-jazz still generates");
    }
}

// R10: 生成が 0 ノートになる状況でもフォールバックで空にならないことを確認。
void testFallback()
{
    // 空でない生成しか通常起きないが、フォールバック経路の健全性を型で担保。
    Rng rng (12345u);
    GenerateConfig cfg;
    cfg.style = 7; cfg.bars = 4; cfg.complexity = 0; cfg.fill = 0;
    const auto notes = buildNotes (cfg, rng);
    expect (! notes.empty(), "fallback/normal produces notes");
}
} // namespace

int main()
{
    expect (std::strcmp (versionString, "0.1.0") == 0, "versionString");

    testPitchTokens();
    testKeyContext();
    testClampSnap();
    testPhraseFlags();
    testChordDetect();
    testKeyDetect();
    testInteraction();
    testLoopDetect();
    testGeneration();
    testFallback();
    testAnalyze();
    testNoteLoop();
    testLoopLock();

    std::printf ("%d checks, %d failures\n", checks, failures);
    if (failures == 0) std::printf ("All tests passed.\n");
    return failures == 0 ? 0 : 1;
}
