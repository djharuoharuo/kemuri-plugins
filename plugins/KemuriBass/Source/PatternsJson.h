#pragma once

#include <juce_core/juce_core.h>

#include <kemuri_core/PatternBank.h>

// patterns.json (R6) を解釈して PatternBank へマージする（JUCE 依存）。
// スキーマ（docs/PORTING.md 準拠、tools/learning の出力と同一）:
//   { "version": 1,
//     "libraries": {
//       "premier": {
//         "patterns": [ { "name","swing","jitter","notes":[{"pos","dur","pitch"}] } ],
//         "transitions": { "from": { "to": weight } },
//         "groove": { "timing": [ [mean,std] | null, ... x16 ] } },
//       "dilla":{}, "ninth":{}, "pete":{}, "pool":{} } }
// pitch は文字列統一（数値も "0" / "7"）。
namespace kemuri
{

struct PatternsJsonResult
{
    bool present = false;   // ファイルが存在したか
    bool ok      = false;   // パース成功したか（R9: false なら警告）
    int  added   = 0;       // マージした学習パターン数
};

namespace detail
{
    inline core::PitchToken parsePitch (const juce::var& v)
    {
        if (v.isString())
        {
            const juce::String s = v.toString();
            // 数値文字列は数値トークン、それ以外は名前付きトークン
            if (s.containsOnly ("-0123456789") && s.isNotEmpty())
                return { core::Tok::Number, s.getIntValue() };
            return core::pitchTokenFromString (s.toStdString());
        }
        if (v.isInt() || v.isDouble())
            return { core::Tok::Number, static_cast<int> (v) };
        return { core::Tok::Number, 0 };
    }

    inline bool parseProducer (const juce::var& libVar, core::ProducerLib& out)
    {
        if (! libVar.isObject())
            return true;   // 空 or 未指定は許容

        // patterns
        if (const juce::var pats = libVar.getProperty ("patterns", {}); pats.isArray())
        {
            for (const auto& pv : *pats.getArray())
            {
                core::Pattern pat;
                pat.name   = pv.getProperty ("name", "learned").toString().toStdString();
                pat.swing  = static_cast<int> (pv.getProperty ("swing", 0));
                pat.jitter = static_cast<double> (pv.getProperty ("jitter", 0.0));
                const juce::var notes = pv.getProperty ("notes", {});
                if (! notes.isArray()) return false;
                for (const auto& nv : *notes.getArray())
                {
                    core::PatternNote n;
                    n.pos   = static_cast<double> (nv.getProperty ("pos", 0.0));
                    n.dur   = static_cast<double> (nv.getProperty ("dur", 0.25));
                    n.pitch = parsePitch (nv.getProperty ("pitch", "0"));
                    pat.notes.push_back (n);
                }
                out.patterns.push_back (std::move (pat));
            }
        }

        // transitions
        if (const juce::var tr = libVar.getProperty ("transitions", {}); tr.isObject())
        {
            if (auto* obj = tr.getDynamicObject())
                for (const auto& from : obj->getProperties())
                    if (from.value.isObject())
                        if (auto* row = from.value.getDynamicObject())
                            for (const auto& to : row->getProperties())
                                out.transitions[from.name.toString().toStdString()]
                                               [to.name.toString().toStdString()]
                                    = static_cast<double> (to.value);
        }

        // groove.timing (16 slots of [mean,std] or null)
        if (const juce::var gr = libVar.getProperty ("groove", {}); gr.isObject())
        {
            const juce::var timing = gr.getProperty ("timing", {});
            if (timing.isArray())
            {
                const auto& arr = *timing.getArray();
                for (int i = 0; i < arr.size() && i < 16; ++i)
                {
                    if (arr[i].isArray() && arr[i].getArray()->size() >= 2)
                    {
                        const auto& pair = *arr[i].getArray();
                        out.groove.timing[static_cast<size_t> (i)]
                            = std::make_pair (static_cast<double> (pair[0]),
                                              static_cast<double> (pair[1]));
                    }
                }
                out.hasGroove = out.groove.hasAny();
            }
        }
        return true;
    }
} // namespace detail

// bank にマージする。ファイルが無ければ present=false（正常）。
inline PatternsJsonResult mergePatternsJson (core::PatternBank& bank, const juce::File& file)
{
    PatternsJsonResult r;
    if (! file.existsAsFile())
        return r;
    r.present = true;

    const juce::var root = juce::JSON::parse (file.loadFileAsString());
    if (! root.isObject())
        return r;   // R9: ok=false

    const juce::var libs = root.getProperty ("libraries", {});
    if (! libs.isObject())
        return r;

    const auto before = bank.premier.patterns.size() + bank.dilla.patterns.size()
                      + bank.ninth.patterns.size() + bank.pete.patterns.size()
                      + bank.pool.patterns.size();

    bool ok = true;
    ok &= detail::parseProducer (libs.getProperty ("premier", {}), bank.premier);
    ok &= detail::parseProducer (libs.getProperty ("dilla",   {}), bank.dilla);
    ok &= detail::parseProducer (libs.getProperty ("ninth",   {}), bank.ninth);
    ok &= detail::parseProducer (libs.getProperty ("pete",    {}), bank.pete);
    ok &= detail::parseProducer (libs.getProperty ("pool",    {}), bank.pool);

    const auto after = bank.premier.patterns.size() + bank.dilla.patterns.size()
                     + bank.ninth.patterns.size() + bank.pete.patterns.size()
                     + bank.pool.patterns.size();

    r.ok    = ok;
    r.added = static_cast<int> (after - before);
    return r;
}

// %APPDATA%/KemuriBeat/patterns.json のパス。
inline juce::File patternsJsonFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("KemuriBeat")
        .getChildFile ("patterns.json");
}

} // namespace kemuri
