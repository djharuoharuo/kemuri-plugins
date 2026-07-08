// R6/R9: patterns.json のロード・マージ検証。
#include <juce_core/juce_core.h>

#include <kemuri_core/PatternBank.h>

#include "PatternsJson.h"

#include <cstdio>

using namespace kemuri;

namespace
{
int failures = 0;
void expect (bool c, const char* label)
{
    if (! c) { std::printf ("FAIL: %s\n", label); ++failures; }
}

juce::File writeTemp (const juce::String& name, const juce::String& content)
{
    const juce::File f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile (name);
    f.replaceWithText (content);
    return f;
}

const char* kSample = R"JSON({
  "version": 1,
  "libraries": {
    "premier": {
      "patterns": [ { "name": "LRN_p", "swing": 0, "jitter": 0,
        "notes": [ { "pos": 0, "dur": 0.5, "pitch": "0" },
                   { "pos": 2, "dur": 0.5, "pitch": "b7" } ] } ],
      "transitions": { "LRN_p": { "PRM_mass_appeal": 2 } },
      "groove": { "timing": [ [0.0,0.01], null, [0.03,0.02], null,
                              null, null, null, null,
                              null, null, null, null,
                              null, null, null, null ] }
    },
    "pool": { "patterns": [ { "name": "LRN_pool", "notes": [ { "pos": 0, "dur": 1, "pitch": "octave" } ] } ] }
  }
})JSON";
} // namespace

int main()
{
    using namespace kemuri::core;

    // 存在しないファイル → present=false（正常, R6 の該当なし）
    {
        PatternBank bank = makeDefaultBank();
        const auto r = mergePatternsJson (bank, juce::File::getSpecialLocation (
            juce::File::tempDirectory).getChildFile ("does_not_exist_kemuri.json"));
        expect (! r.present, "missing file → not present");
    }

    // 正常な patterns.json → マージ成功（R6）
    {
        PatternBank bank = makeDefaultBank();
        const size_t premierBefore = bank.premier.patterns.size();
        const juce::File f = writeTemp ("kemuri_patterns_ok.json", kSample);
        const auto r = mergePatternsJson (bank, f);

        expect (r.present && r.ok, "valid json parses ok");
        expect (r.added == 2, "added 2 learned patterns");
        expect (bank.premier.patterns.size() == premierBefore + 1, "premier gained 1 pattern");
        expect (bank.premier.patterns.back().name == "LRN_p", "learned name preserved");
        expect (! bank.premier.transitions.empty(), "transitions parsed");
        expect (bank.premier.transitions["LRN_p"]["PRM_mass_appeal"] == 2.0, "transition weight");
        expect (bank.premier.hasGroove, "groove parsed");
        expect (bank.premier.groove.timing[0].has_value(), "groove slot 0 set");
        expect (! bank.pool.patterns.empty() && bank.pool.patterns[0].name == "LRN_pool",
                "pool pattern parsed");

        // 学習パターンのピッチトークンが正しく解決される
        expect (bank.premier.patterns.back().notes.size() == 2, "learned note count");
        expect (bank.premier.patterns.back().notes[1].pitch.kind == Tok::FlatSeven,
                "learned pitch token b7");

        f.deleteFile();
    }

    // 壊れた JSON → ok=false, ハードコードのみ（R9）
    {
        PatternBank bank = makeDefaultBank();
        const size_t premierBefore = bank.premier.patterns.size();
        const juce::File f = writeTemp ("kemuri_patterns_bad.json", "{ this is not valid json ][");
        const auto r = mergePatternsJson (bank, f);
        expect (r.present && ! r.ok, "malformed json → present but not ok (R9)");
        expect (bank.premier.patterns.size() == premierBefore, "malformed → no patterns added");
        f.deleteFile();
    }

    // コミット済みサンプル（docs/patterns.sample.json）が読めること
#ifdef KEMURI_SAMPLE_JSON
    {
        PatternBank bank = makeDefaultBank();
        const auto r = mergePatternsJson (bank, juce::File (KEMURI_SAMPLE_JSON));
        expect (r.present && r.ok, "committed sample parses ok");
        expect (r.added > 0, "committed sample adds patterns");
    }
#endif

    // Python 学習パイプライン出力との相互検証（環境変数で渡された場合のみ）
    {
        const juce::String extra =
            juce::SystemStats::getEnvironmentVariable ("KEMURI_EXTRA_PATTERNS", {});
        const juce::File f (extra);
        if (extra.isNotEmpty() && f.existsAsFile())
        {
            PatternBank bank = makeDefaultBank();
            const auto r = mergePatternsJson (bank, f);
            expect (r.present && r.ok, "python-generated patterns.json parses ok");
            expect (r.added > 0, "python-generated adds patterns");
            std::printf ("cross-check: loaded %d patterns from python output\n", r.added);
        }
    }

    std::printf (failures == 0 ? "Patterns tests passed.\n" : "%d patterns failures\n", failures);
    return failures == 0 ? 0 : 1;
}
