// R3: SMF エクスポート経路の検証。
// 実際に Processor が使う writeSequenceToSmf を通し、書いて読み戻して
// format 0 / PPQ 480 / ノートペア数を確認する。
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <kemuri_core/Generators.h>

#include "SmfExport.h"

#include <cstdio>

using namespace kemuri;
using namespace kemuri::core;

namespace
{
int failures = 0;
void expect (bool c, const char* label)
{
    if (! c) { std::printf ("FAIL: %s\n", label); ++failures; }
}
} // namespace

int main()
{
    // 空シーケンスは書けない（false）
    {
        const juce::File tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getChildFile ("kemuri_smf_empty.mid");
        expect (! writeSequenceToSmf ({}, tmp), "empty sequence returns false");
    }

    // 複数スタイル/小節で往復
    for (int style : { 0, 1, 5, 6, 7 })
    {
        for (int bars : { 4, 8, 16 })
        {
            Rng rng (static_cast<std::uint32_t> (style * 31 + bars));
            GenerateConfig cfg;
            cfg.style = style; cfg.bars = bars; cfg.complexity = 55; cfg.fill = 35;
            cfg.root = 7; cfg.mode = style % 2;
            const auto notes = buildNotes (cfg, rng);

            const juce::File tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                       .getChildFile ("kemuri_smf_" + juce::String (style)
                                                      + "_" + juce::String (bars) + ".mid");
            const bool wrote = writeSequenceToSmf (notes, tmp);
            expect (wrote, "write succeeds");

            juce::MidiFile rd;
            juce::FileInputStream is (tmp);
            const bool ok = rd.readFrom (is);
            expect (ok, "read back succeeds");
            expect (rd.getTimeFormat() == kSmfPpq, "PPQ is 480");
            expect (rd.getNumTracks() == 1, "single track (format 0)");

            int noteOns = 0;
            if (rd.getNumTracks() > 0)
                for (auto* ev : *rd.getTrack (0))
                    if (ev->message.isNoteOn()) ++noteOns;
            expect (noteOns == static_cast<int> (notes.size()), "note-on count matches");

            tmp.deleteFile();
        }
    }

    std::printf (failures == 0 ? "SMF tests passed.\n" : "%d SMF failures\n", failures);
    return failures == 0 ? 0 : 1;
}
