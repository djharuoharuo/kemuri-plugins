#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PatternsJson.h"
#include "SmfExport.h"

#include <algorithm>
#include <cmath>

#include <kemuri_core/Generators.h>

namespace kemuri
{

namespace
{
    const juce::StringArray kStyleNames {
        "Boom-Bap Mix", "Premier", "J Dilla", "9th Wonder",
        "Pete Rock", "Soul-Jazz", "Funk", "Lo-Fi" };
    const juce::StringArray kBarChoices { "4", "8", "16" };
    const juce::StringArray kKeyNames {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const juce::StringArray kModeNames { "Major", "Minor" };

    constexpr int   kBarValues[3] { 4, 8, 16 };
    constexpr int   kMidiChannel  = 1;
    constexpr double kTargetDecimRate = 11025.0;

    juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

    // Ableton 表記のノート名（C3 = 60）
    juce::String abletonNoteName (int midi)
    {
        return kKeyNames[((midi % 12) + 12) % 12] + juce::String (midi / 12 - 2);
    }

    juce::String chordName (const kemuri::core::ChordSeg& c)
    {
        return kKeyNames[c.root] + (c.quality == "min" ? "m" : "");
    }
} // namespace

KemuriBassProcessor::KemuriBassProcessor()
    : AudioProcessor (BusesProperties()
                          // v2.0.2: ネタの音を受けるサイドチェインだけを入力に持つ（VST3 では kAux,
                          // getPluginHasMainInput() = false）。v2.0.1 までの「無効な主入力 + 副入力」
                          // 構成では Live でサイドチェインを選んでも届かなかった。
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createLayout())
{
    audioFifoBuf.assign (static_cast<size_t> (kAudioFifoCapacity), 0.0f);
    loadPatternBank();   // R6/R9: patterns.json をハードコードへマージ
    startTimerHz (25);   // キャプチャのドレイン（message thread）
}

void KemuriBassProcessor::loadPatternBank()
{
    bank = kemuri::core::makeDefaultBank();
    const auto res = mergePatternsJson (bank, patternsJsonFile());

    if (! res.present)
    {
        bankStatus  = "patterns: built-in only";
        bankWarning = false;
    }
    else if (! res.ok)
    {
        // R9: パース失敗 → ハードコードのみ（bank は既定に戻す）+ 警告
        bank        = kemuri::core::makeDefaultBank();
        bankStatus  = "patterns.json parse failed — using built-in only";
        bankWarning = true;
    }
    else
    {
        bankStatus  = "patterns.json: +" + juce::String (res.added) + " learned";
        bankWarning = false;
    }
}

KemuriBassProcessor::~KemuriBassProcessor()
{
    stopTimer();
}

juce::AudioProcessorValueTreeState::ParameterLayout KemuriBassProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { pid::style, 1 }, "Style", kStyleNames, 0));
    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { pid::complexity, 1 }, "Complexity", 0, 100, 30));
    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { pid::fill, 1 }, "Fill", 0, 100, 20));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { pid::bars, 1 }, "Bars", kBarChoices, 0));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { pid::key, 1 }, "Key", kKeyNames, 0));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { pid::mode, 1 }, "Mode", kModeNames, 0));

    return layout;
}

void KemuriBassProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate   = newSampleRate;
    internalPpq  = 0.0;
    lastRendered = nullptr;
    wasPlaying   = false;
    captureFifo.reset();

    // サイドチェインの間引き設定（44.1/48 kHz → 1/4、96 kHz → 1/9）
    decimFactor = std::max (1, static_cast<int> (std::lround (newSampleRate / kTargetDecimRate)));
    decimRate   = newSampleRate / decimFactor;
    const double cutoff = std::min (4500.0, decimRate * 0.4);
    aa1 = kemuri::core::dsp::Biquad::lowpass (newSampleRate, cutoff);
    aa2 = kemuri::core::dsp::Biquad::lowpass (newSampleRate, cutoff);
    audioCapturing = false;   // 次のブロックで区間マーカーを積み直す
}

void KemuriBassProcessor::releaseResources()
{
}

bool KemuriBassProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;

    // サイドチェインは 無効 / モノ / ステレオ を許す
    for (int i = 0; i < layouts.inputBuses.size(); ++i)
    {
        const auto& in = layouts.inputBuses.getReference (i);
        if (! in.isDisabled() && in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

// ── Generation (message thread) ─────────────────────────────────────
void KemuriBassProcessor::requestGenerate()
{
    using namespace kemuri::core;

    const int style      = static_cast<int> (apvts.getRawParameterValue (pid::style)->load());
    const int complexity = static_cast<int> (apvts.getRawParameterValue (pid::complexity)->load());
    const int fill       = static_cast<int> (apvts.getRawParameterValue (pid::fill)->load());
    const int bars       = kBarValues[std::clamp (static_cast<int> (apvts.getRawParameterValue (pid::bars)->load()), 0, 2)];
    const int keyRoot    = static_cast<int> (apvts.getRawParameterValue (pid::key)->load());
    const int keyMode    = static_cast<int> (apvts.getRawParameterValue (pid::mode)->load());

    auto seq = std::make_unique<MidiSequence>();
    previewKickBeats.clear();

    if (style <= 4)
    {
        // v2: キック同期 + ネタの和音/ベース（ブームバップ系）
        V2Config v;
        v.style = style; v.bars = bars; v.complexity = complexity; v.fill = fill;
        v.keyRoot = keyRoot; v.keyMode = keyMode;
        v.harmony = harmonyV2.ok ? &harmonyV2 : nullptr;
        v.kick    = kickV2.ok    ? &kickV2    : nullptr;
        const auto res = buildBassV2 (v, rng);

        seq->notes       = res.notes;
        seq->lengthBeats = res.bars * 4.0;

        generateSummary = u8 ("\xE7\x94\x9F\xE6\x88\x90: ") + producerName (res.producer) + " — " + u8 (res.approach.c_str())
                          + (res.usedUserKick ? u8 (" / \xE3\x81\x82\xE3\x81\xAA\xE3\x81\x9F\xE3\x81\xAE\xE3\x82\xAD\xE3\x83\x83\xE3\x82\xAF\xE3\x81\xAB\xE5\x90\x8C\xE6\x9C\x9F")
                                              : u8 (" / \xE5\xAE\x9A\xE7\x95\xAA\xE3\x82\xAD\xE3\x83\x83\xE3\x82\xAF"))
                          + (res.bars != bars ? u8 ("\xEF\xBC\x88") + juce::String (res.bars)
                                                    + u8 ("\xE5\xB0\x8F\xE7\xAF\x80: \xE3\x83\x8D\xE3\x82\xBF\xE3\x81\xAE\xE3\x83\xAB\xE3\x83\xBC\xE3\x83\x97\xE9\x95\xB7\xE3\x81\xAB\xE5\x90\x88\xE3\x82\x8F\xE3\x81\x9B\xE3\x81\x9F\xEF\xBC\x89")
                                              : juce::String());

        if (kickV2.ok)
            for (int b = 0; b < res.bars; ++b)
                for (double p : kickV2.bars[static_cast<size_t> (b % kickV2.loopBars)])
                    previewKickBeats.push_back (b * 4.0 + p);
    }
    else
    {
        // Soul-Jazz / Funk / Lo-Fi は手続き生成（ネタの和音はソング小節基準で渡す）
        GenerateConfig cfg;
        cfg.style = style; cfg.complexity = complexity; cfg.fill = fill;
        cfg.bars = bars; cfg.root = keyRoot; cfg.mode = keyMode; cfg.bank = &bank;
        if (harmonyV2.ok)
        {
            cfg.useProgression = true;
            cfg.progBar        = harmonyV2.perBar;
            cfg.progHalfBar    = harmonyV2.halfBar;
            cfg.loopBars       = harmonyV2.loopBars;
            cfg.bars           = std::min (16, std::max (bars, harmonyV2.loopBars));
            if (harmonyV2.hasOnset) cfg.onsetHist = harmonyV2.onsetHist;
        }
        if (kickV2.ok && kickV2.hasSwing) cfg.swingOverride = kickV2.swingPercent;
        seq->notes       = buildNotes (cfg, rng);
        seq->lengthBeats = cfg.bars * 4.0;
        generateSummary  = u8 ("\xE7\x94\x9F\xE6\x88\x90: ") + kStyleNames[style];
    }

    // ネタ未解析のまま生成した場合ははっきり警告する（黙って手動キーで作らない）
    if (! harmonyV2.ok)
        generateSummary = u8 ("\xE2\x9A\xA0 \xE3\x83\x8D\xE3\x82\xBF\xE6\x9C\xAA\xE8\xA7\xA3\xE6\x9E\x90 \xE2\x80\x94 \xE6\x89\x8B\xE5\x8B\x95\xE3\x81\xAE Key ")
                          + kKeyNames[keyRoot] + (keyMode == 0 ? " Major" : " Minor")
                          + u8 (" \xE3\x81\xA7\xE4\xBD\x9C\xE3\x81\xA3\xE3\x81\x9F\xE3\x81\x9F\xE3\x82\x81\xE3\x83\x8D\xE3\x82\xBF\xE3\x81\xA8\xE3\x81\xAF\xE5\x90\x88\xE3\x81\x84\xE3\x81\xBE\xE3\x81\x9B\xE3\x82\x93\n")
                          + generateSummary;

    // start でソート（processBlock の区間判定を安定させる）
    std::sort (seq->notes.begin(), seq->notes.end(),
               [] (const OutNote& a, const OutNote& b) { return a.start < b.start; });

    lastNoteCount.store (static_cast<int> (seq->notes.size()));

    const MidiSequence* raw = seq.get();
    ownedSeqs.push_back (std::move (seq));
    liveSeq.store (raw, std::memory_order_release);

    // 直近 2 世代のみ保持（オーディオが読みうるのは最新 or 1 世代前まで）。
    while (ownedSeqs.size() > 2)
        ownedSeqs.erase (ownedSeqs.begin());
}

void KemuriBassProcessor::setChoiceParam (const char* id, int index)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (index)));
}

// ── Analysis (message thread) ───────────────────────────────────────
void KemuriBassProcessor::updateAnalysisSummary()
{
    using namespace kemuri::core;
    juce::String neta, kick;

    if (harmonyV2.ok)
    {
        juce::String prog;
        for (int i = 0; i < harmonyV2.loopBars && i < 8; ++i)
        {
            const auto& h0 = harmonyV2.halfBar[static_cast<size_t> (i * 2)];
            const auto& h1 = harmonyV2.halfBar[static_cast<size_t> (i * 2 + 1)];
            if (i > 0) prog << "-";
            prog << chordName (h0);
            if (h1.root != h0.root || h1.quality != h0.quality) prog << "/" << chordName (h1);
        }
        int voiced = 0;
        for (int m : harmonyV2.bassMidi) if (m >= 0) ++voiced;
        const bool bassOk = voiced * 4 >= static_cast<int> (harmonyV2.bassMidi.size());

        neta = u8 ("\xE3\x83\x8D\xE3\x82\xBF(") + harmonySource + "): Key " + kKeyNames[harmonyV2.keyRoot]
               + (harmonyV2.keyMode == 0 ? " Maj" : " Min")
               + u8 (" | \xE3\x83\xAB\xE3\x83\xBC\xE3\x83\x97") + juce::String (harmonyV2.loopBars)
               + u8 ("\xE5\xB0\x8F\xE7\xAF\x80 | ") + prog
               + (bassOk ? u8 (" | \xE3\x82\xB5\xE3\x83\xB3\xE3\x83\x97\xE3\x83\xAB\xE3\x81\xAE\xE3\x83\x99\xE3\x83\xBC\xE3\x82\xB9\xE3\x81\x82\xE3\x82\x8A")
                         : u8 (" | \xE3\x82\xB5\xE3\x83\xB3\xE3\x83\x97\xE3\x83\xAB\xE3\x81\xAE\xE3\x83\x99\xE3\x83\xBC\xE3\x82\xB9\xE5\xBC\xB1\xE3\x81\x84"));
    }
    else
    {
        // R8: 入力なし → 手動 Key/Mode を維持。読めなかった理由と直し方を出す。
        neta = u8 ("\xE2\x9A\xA0 \xE3\x83\x8D\xE3\x82\xBF\xE6\x9C\xAA\xE8\xA7\xA3\xE6\x9E\x90: ")   // ⚠ ネタ未解析:
               + (netaProblem.isNotEmpty() ? netaProblem
                                           : u8 ("\xE5\x86\x8D\xE7\x94\x9F\xE4\xB8\xAD\xE3\x81\xAB Analyze \xE3\x82\x92\xE6\x8A\xBC\xE3\x81\x97\xE3\x81\xA6\xE3\x81\x8F\xE3\x81\xA0\xE3\x81\x95\xE3\x81\x84"));
    }

    if (kickV2.ok)
    {
        int total = 0;
        for (const auto& b : kickV2.bars) total += static_cast<int> (b.size());
        kick = u8 ("\xE3\x82\xAD\xE3\x83\x83\xE3\x82\xAF: ") + abletonNoteName (kickV2.kickNote)
               + " | " + juce::String (static_cast<double> (total) / kickV2.loopBars, 1)
               + u8 ("\xE6\x89\x93/\xE5\xB0\x8F\xE7\xAF\x80 | \xE3\x83\xAB\xE3\x83\xBC\xE3\x83\x97")
               + juce::String (kickV2.loopBars) + u8 ("\xE5\xB0\x8F\xE7\xAF\x80")
               + (kickV2.hasSwing ? " | swing " + juce::String (kickV2.swingPercent) + "%" : juce::String());
        if (kickProblem.isNotEmpty())   // 今回は読めなかった → 前回のキックを使い続ける
            kick << u8 ("\xEF\xBC\x88\xE5\x89\x8D\xE5\x9B\x9E\xEF\xBC\x89") << "\n" << u8 ("\xE2\x9A\xA0 \xE4\xBB\x8A\xE5\x9B\x9E: ") << kickProblem;
    }
    else
    {
        kick = u8 ("\xE2\x9A\xA0 \xE3\x82\xAD\xE3\x83\x83\xE3\x82\xAF\xE6\x9C\xAA\xE6\xA4\x9C\xE5\x87\xBA: ")   // ⚠ キック未検出:
               + (kickProblem.isNotEmpty() ? kickProblem
                                           : u8 ("MIDI From \xE3\x82\x92\xE3\x83\x89\xE3\x83\xA9\xE3\x83\xA0\xE3\x81\xAE\xE3\x83\x88\xE3\x83\xA9\xE3\x83\x83\xE3\x82\xAF\xE3\x81\xAB"
                                                 "\xEF\xBC\x88\xE3\x81\x84\xE3\x81\xBE\xE3\x81\xAF\xE5\xAE\x9A\xE7\x95\xAA\xE3\x82\xAD\xE3\x83\x83\xE3\x82\xAF\xE3\x81\xA7\xE7\x94\x9F\xE6\x88\x90\xEF\xBC\x89"));
    }

    if (harmonyV2.ok && netaProblem.isNotEmpty())
        neta << u8 ("\xEF\xBC\x88\xE5\x89\x8D\xE5\x9B\x9E\xEF\xBC\x89");   // 今回は読めなかった → 前回のネタの解析を使い続ける
    analysisSummary = neta + "\n" + kick;
}

void KemuriBassProcessor::requestAnalyze()
{
    using namespace kemuri::core;

    drainCapture();
    drainAudio();

    // ── ネタ: サイドチェインの音から（2 小節以上取り込めていれば）
    AudioFeatures feats;
    bool haveAudio = false;
    netaProblem.clear();
    const double capturedBeats = haveAudioSegment ? static_cast<double> (captured.samples.size()) * captured.beatsPerSample : 0.0;
    float capturedPeak = 0.0f;
    for (float s : captured.samples) capturedPeak = std::max (capturedPeak, std::abs (s));

    if (! sidechainEnabled.load())
        // サイドチェインが無効 → Live のデバイスビューで kemuriBass の左側の Sidechain をオンにして Audio From = ネタ
        netaProblem = u8 ("\xE3\x82\xB5\xE3\x82\xA4\xE3\x83\x89\xE3\x83\x81\xE3\x82\xA7\xE3\x82\xA4\xE3\x83\xB3\xE3\x81\x8C\xE7\x84\xA1\xE5\x8A\xB9 \xE2\x80\x94 "
                          "\xE7\x94\xBB\xE9\x9D\xA2\xE4\xB8\x8B\xE3\x81\xAE\xE3\x83\x87\xE3\x83\x90\xE3\x82\xA4\xE3\x82\xB9\xE3\x81\xA7 kemuriBass \xE3\x81\xAE\xE5\xB7\xA6\xE5\x81\xB4\xE3\x81\xAE "
                          "Sidechain \xE3\x82\x92\xE3\x82\xAA\xE3\x83\xB3\xE3\x80\x81" "Audio From = \xE3\x83\x8D\xE3\x82\xBF\xE3\x81\xAE\xE3\x83\x88\xE3\x83\xA9\xE3\x83\x83\xE3\x82\xAF");
    else if (capturedBeats < 8.0)
        // 再生しながら 2 小節以上流してから Analyze
        netaProblem = u8 ("\xE5\x86\x8D\xE7\x94\x9F\xE3\x81\x97\xE3\x81\xAA\xE3\x81\x8C\xE3\x82\x89 2 \xE5\xB0\x8F\xE7\xAF\x80\xE4\xBB\xA5\xE4\xB8\x8A\xE6\xB5\x81\xE3\x81\x97\xE3\x81\xA6\xE3\x81\x8B\xE3\x82\x89 Analyze");
    else if (capturedPeak < 1.0e-4f)
        // サイドチェインに音が来ていない → Audio From にネタのトラックを選ぶ
        netaProblem = u8 ("\xE3\x82\xB5\xE3\x82\xA4\xE3\x83\x89\xE3\x83\x81\xE3\x82\xA7\xE3\x82\xA4\xE3\x83\xB3\xE3\x81\xAB\xE9\x9F\xB3\xE3\x81\x8C\xE6\x9D\xA5\xE3\x81\xA6\xE3\x81\x84\xE3\x81\xBE\xE3\x81\x9B\xE3\x82\x93 \xE2\x80\x94 "
                          "Audio From \xE3\x81\xAB\xE3\x83\x8D\xE3\x82\xBF\xE3\x81\xAE\xE3\x83\x88\xE3\x83\xA9\xE3\x83\x83\xE3\x82\xAF\xE3\x82\x92\xE9\x81\xB8\xE3\x82\x93\xE3\x81\xA7\xE3\x81\x8F\xE3\x81\xA0\xE3\x81\x95\xE3\x81\x84");
    else
    {
        feats = computeAudioFeatures (captured, 16);
        const auto h = analyzeHarmony (feats);
        if (h.ok)
        {
            harmonyV2     = h;
            harmonySource = u8 ("\xE9\x9F\xB3");   // 音
            haveAudio     = true;
            setChoiceParam (pid::key,  h.keyRoot);
            setChoiceParam (pid::mode, h.keyMode);
        }
    }

    // ── キック: ドラム MIDI（最後の連続した再生分。ネタと同じ小節範囲、ネタが無ければ直近 16 小節）
    // 今回読めなかったときは前回のキックを使い続け、理由だけ表示する（kickV2 は上書きしない）
    kickProblem.clear();
    if (! recentEvents.empty() && ! haveAudio && recentEvents.back().ppq - recentEvents.front().ppq < 7.0)
        kickProblem = u8 ("\xE5\x86\x8D\xE7\x94\x9F\xE3\x81\x97\xE3\x81\xAA\xE3\x81\x8C\xE3\x82\x89 2 \xE5\xB0\x8F\xE7\xAF\x80\xE4\xBB\xA5\xE4\xB8\x8A\xE6\xB5\x81\xE3\x81\x97\xE3\x81\xA6\xE3\x81\x8B\xE3\x82\x89 Analyze");
    else if (recentEvents.empty())
        kickProblem = u8 ("\xE5\x86\x8D\xE7\x94\x9F\xE4\xB8\xAD\xE3\x81\xAB\xE3\x83\x89\xE3\x83\xA9\xE3\x83\xA0\xE3\x81\xAE MIDI \xE3\x81\x8C\xE5\xB1\x8A\xE3\x81\x84\xE3\x81\xA6\xE3\x81\x84\xE3\x81\xBE\xE3\x81\x9B\xE3\x82\x93 \xE2\x80\x94 MIDI From = \xE3\x83\x89\xE3\x83\xA9\xE3\x83\xA0\xE3\x81\xAE\xE3\x83\x88\xE3\x83\xA9\xE3\x83\x83\xE3\x82\xAF\xEF\xBC\x88\xE4\xB8\x8B\xE3\x81\xAE\xE6\xAC\x84 Pre FX\xEF\xBC\x89");
    else
    {
        long firstBar, endBar;
        if (haveAudio)
        {
            firstBar = feats.firstBar;
            endBar   = feats.firstBar + feats.numBars;
        }
        else
        {
            endBar   = static_cast<long> (std::floor (recentEvents.back().ppq / 4.0)) + 1;
            firstBar = std::max (static_cast<long> (std::floor (recentEvents.front().ppq / 4.0)), endBar - 16);
        }
        std::vector<RawEvent> events;
        events.reserve (recentEvents.size());
        for (const auto& e : recentEvents) events.push_back ({ e.ppq, e.pitch, e.isOn });
        const auto k = analyzeKick (events, firstBar, endBar);
        const bool padRange   = k.kickNote >= 35 && k.kickNote <= 51;
        // Drum Rack のチェーンの再生音程（既定 C3 = 60）の 1 音だけで届いている:
        //  - キックだけのチェーンを選んでいる → 打数が少なくスネアの 2・4 拍が無い → キックとして採用
        //  - 全パッドが 1 音にまとまっている → キックを区別できないので Pre FX / Kick Drum を案内
        // それ以外の音（ネタのチョップのパッド等）は従来どおりパッド範囲で判定する。
        const bool singleNote = k.ok && k.distinctNotes == 1 && k.kickNote == 60;
        const bool kickOnly   = singleNote && k.kickPerBar <= 6.0 && k.backbeatRate < 0.75;
        if (! k.ok)
            kickProblem = u8 ("\xE3\x83\x8D\xE3\x82\xBF\xE3\x82\x92\xE8\xA7\xA3\xE6\x9E\x90\xE3\x81\x97\xE3\x81\x9F\xE5\x8C\xBA\xE9\x96\x93\xE3\x81\xAB\xE3\x83\x89\xE3\x83\xA9\xE3\x83\xA0\xE3\x81\xAE MIDI \xE3\x81\x8C\xE3\x81\x82\xE3\x82\x8A\xE3\x81\xBE\xE3\x81\x9B\xE3\x82\x93 \xE2\x80\x94 \xE3\x83\x89\xE3\x83\xA9\xE3\x83\xA0\xE3\x81\xAE\xE3\x82\xAF\xE3\x83\xAA\xE3\x83\x83\xE3\x83\x97\xE3\x82\x82\xE9\xB3\xB4\xE3\x82\x89\xE3\x81\x97\xE3\x81\xA6\xE5\x86\x8D\xE7\x94\x9F");
        else if (singleNote && ! kickOnly)
            kickProblem = abletonNoteName (k.kickNote) + u8 (" \xE3\x81\xAE 1 \xE9\x9F\xB3\xE3\x81\xAB\xE3\x83\x89\xE3\x83\xA9\xE3\x83\xA0\xE3\x81\x8C\xE5\x85\xA8\xE9\x83\xA8\xE3\x81\xBE\xE3\x81\xA8\xE3\x81\xBE\xE3\x81\xA3\xE3\x81\xA6\xE5\xB1\x8A\xE3\x81\x84\xE3\x81\xA6\xE3\x81\x84\xE3\x81\xBE\xE3\x81\x99 \xE2\x80\x94 MIDI From \xE3\x81\xAE\xE4\xB8\x8B\xE3\x81\xAE\xE6\xAC\x84\xE3\x82\x92 Pre FX \xE3\x81\x8B Kick Drum \xE3\x81\xAB");
        else if (! padRange && ! kickOnly)
            // Drum Rack のパッド範囲（C1〜D#2 = 36〜51）外 → ドラムのトラックではない可能性が高い。
            // ネタのチョップをキックと取り違えないよう採用しない。
            kickProblem = abletonNoteName (k.kickNote) + u8 (" \xE3\x81\xAF\xE3\x82\xAD\xE3\x83\x83\xE3\x82\xAF\xE3\x81\xA7\xE3\x81\xAF\xE3\x81\xAA\xE3\x81\x95\xE3\x81\x9D\xE3\x81\x86\xE3\x81\xA7\xE3\x81\x99 \xE2\x80\x94 MIDI From \xE3\x81\x8C\xE3\x83\x89\xE3\x83\xA9\xE3\x83\xA0\xE3\x81\xAE\xE3\x83\x88\xE3\x83\xA9\xE3\x83\x83\xE3\x82\xAF\xE3\x81\x8B\xE7\xA2\xBA\xE8\xAA\x8D\xEF\xBC\x88\xE3\x83\x8D\xE3\x82\xBF\xE3\x81\xAE\xE3\x83\x88\xE3\x83\xA9\xE3\x83\x83\xE3\x82\xAF\xE3\x81\xAB\xE3\x81\xAA\xE3\x81\xA3\xE3\x81\xA6\xE3\x81\x84\xE3\x81\xBE\xE3\x81\x9B\xE3\x82\x93\xE3\x81\x8B\xEF\xBC\x9F\xEF\xBC\x89");
        else
            kickV2 = k;
    }

    updateAnalysisSummary();
}

// ドロップした .mid（音程のある MIDI のネタ）を解析。小節 0 = クリップ先頭。
bool KemuriBassProcessor::analyzeMidiFile (const juce::File& file)
{
    using namespace kemuri::core;

    juce::FileInputStream in (file);
    if (! in.openedOk())
        return false;

    juce::MidiFile mf;
    if (! mf.readFrom (in))
        return false;

    double tpq = static_cast<double> (mf.getTimeFormat());   // >0 = ticks/quarter
    if (tpq <= 0.0) tpq = 960.0;                              // SMPTE は非対応→既定値

    std::vector<RawNote> notes;
    double minStart = 1e18;

    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (t));
        seq.updateMatchedPairs();
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            auto* ev = seq.getEventPointer (i);
            if (ev == nullptr || ! ev->message.isNoteOn())
                continue;
            const double onBeat  = ev->message.getTimeStamp() / tpq;
            const double offBeat = (ev->noteOffObject != nullptr)
                                       ? ev->noteOffObject->message.getTimeStamp() / tpq
                                       : onBeat + 0.25;
            notes.push_back ({ ev->message.getNoteNumber(), onBeat,
                               std::max (0.05, offBeat - onBeat) });
            minStart = std::min (minStart, onBeat);
        }
    }

    if (notes.empty())
        return false;

    // 丸ごと小節ぶんだけ引いて拍内位置を保存（SMF の t=0 が小節グリッド原点）
    const double barOffset = barAlignOffset (minStart);
    for (auto& n : notes) n.start -= barOffset;

    const auto r = analyzeNotes (notes);
    if (! r.hasInput)
        return false;

    // AnalysisResult → HarmonyAnalysis（ループ長は 2 の冪に丸める）
    HarmonyAnalysis h;
    h.ok = true; h.keyRoot = r.keyRoot; h.keyMode = r.keyMode;
    int L = 1;
    while (L < std::min (16, std::max (1, r.loopBars))) L *= 2;
    h.loopBars = L;
    for (int i = 0; i < L; ++i)
    {
        const auto& pb = r.progBar[static_cast<size_t> (i % static_cast<int> (r.progBar.size()))];
        h.perBar.push_back ({ i * 4.0, 4.0, pb.root, pb.quality });
        for (int half = 0; half < 2; ++half)
        {
            const size_t idx = static_cast<size_t> ((i * 2 + half) % static_cast<int> (r.progHalfBar.size()));
            const auto& ph = r.progHalfBar[idx];
            h.halfBar.push_back ({ (i * 2 + half) * 2.0, 2.0, ph.root, ph.quality });
        }
    }
    // ネタの最低音（C3 未満）を「サンプルのベース」とみなす
    h.bassMidi.assign (static_cast<size_t> (16 * L), -1);
    for (int st = 0; st < 16 * L; ++st)
    {
        const double t = st * 0.25 + 0.125;
        int lowest = 128;
        for (const auto& n : notes)
            if (n.start <= t && n.start + n.duration > t && n.pitch < 48) lowest = std::min (lowest, n.pitch);
        if (lowest < 128) h.bassMidi[static_cast<size_t> (st)] = lowest;
    }
    h.onsetHist = r.onsetHist;
    h.hasOnset  = r.hasOnset;

    harmonyV2     = h;
    harmonySource = ".mid";
    setChoiceParam (pid::key,  h.keyRoot);
    setChoiceParam (pid::mode, h.keyMode);
    updateAnalysisSummary();
    return true;
}

// オーディオスレッドが積んだ MIDI イベントを message thread の 64 小節リングへ移す。
void KemuriBassProcessor::drainCapture()
{
    int start1, size1, start2, size2;
    const int ready = captureFifo.getNumReady();
    captureFifo.prepareToRead (ready, start1, size1, start2, size2);
    // 新しい再生区間のイベントが来たら古い区間は捨てる（前の再生・別のルーティングの頃の
    // イベントが同じソング位置に残って混ざるのを防ぐ。ネタの音と同じく最後の連続した再生分だけ使う）
    auto take = [this] (const CapturedEvent& e)
    {
        if (e.seg != recentSegment)
        {
            recentEvents.clear();
            recentSegment = e.seg;
        }
        if (e.pitch >= 0) recentEvents.push_back (e);   // 区間の始まりの印は保存しない
    };
    for (int i = 0; i < size1; ++i) take (captureBuffer[static_cast<size_t> (start1 + i)]);
    for (int i = 0; i < size2; ++i) take (captureBuffer[static_cast<size_t> (start2 + i)]);
    captureFifo.finishedRead (size1 + size2);

    if (! recentEvents.empty())
    {
        const double cutoff = recentEvents.back().ppq - kWindowBeats;
        while (! recentEvents.empty() && recentEvents.front().ppq < cutoff)
            recentEvents.pop_front();
    }
}

// サイドチェインの音を連続区間（一定テンポ・ソング位置つき）に組み立てる。
void KemuriBassProcessor::drainAudio()
{
    // 1) サンプル（区間の先頭サンプル番号 capturedStartIndex から連続）
    int s1, z1, s2, z2;
    audioFifo.prepareToRead (audioFifo.getNumReady(), s1, z1, s2, z2);
    auto& v = captured.samples;
    v.insert (v.end(), audioFifoBuf.begin() + s1, audioFifoBuf.begin() + s1 + z1);
    if (z2 > 0) v.insert (v.end(), audioFifoBuf.begin() + s2, audioFifoBuf.begin() + s2 + z2);
    audioFifo.finishedRead (z1 + z2);

    // 2) 区間マーカー（届いたサンプル範囲に入ったものから適用）
    int m1, n1, m2, n2;
    markerFifo.prepareToRead (markerFifo.getNumReady(), m1, n1, m2, n2);
    for (int i = 0; i < n1; ++i) pendingMarkers.push_back (markerBuf[static_cast<size_t> (m1 + i)]);
    for (int i = 0; i < n2; ++i) pendingMarkers.push_back (markerBuf[static_cast<size_t> (m2 + i)]);
    markerFifo.finishedRead (n1 + n2);

    while (! pendingMarkers.empty())
    {
        const auto mk = pendingMarkers.front();
        const long long endIdx = capturedStartIndex + static_cast<long long> (v.size());
        if (mk.sampleIndex > endIdx) break;   // サンプルがまだ届いていない
        const long long rel = mk.sampleIndex - capturedStartIndex;
        if (rel > 0)      v.erase (v.begin(), v.begin() + static_cast<long> (rel));
        else if (rel < 0) v.clear();
        capturedStartIndex      = mk.sampleIndex;
        captured.ppq0           = mk.ppq;
        captured.beatsPerSample = mk.beatsPerSample;
        captured.sampleRate     = mk.sampleRate;
        haveAudioSegment        = true;
        pendingMarkers.pop_front();
    }

    // 3) 直近 32 小節ぶんだけ保持（区間が無いときは 30 秒で打ち切り）
    const size_t maxKeep = (haveAudioSegment && captured.beatsPerSample > 0.0)
                               ? static_cast<size_t> (128.0 / captured.beatsPerSample)
                               : static_cast<size_t> (30.0 * kTargetDecimRate);
    if (v.size() > maxKeep + maxKeep / 4)
    {
        const size_t drop = v.size() - maxKeep;
        v.erase (v.begin(), v.begin() + static_cast<long> (drop));
        capturedStartIndex += static_cast<long long> (drop);
        if (haveAudioSegment) captured.ppq0 += static_cast<double> (drop) * captured.beatsPerSample;
    }
}

void KemuriBassProcessor::timerCallback()
{
    drainCapture();
    drainAudio();
}

// ── Realtime MIDI capture（R11: alloc/lock/file-IO なし）─────────────
void KemuriBassProcessor::captureIncoming (const juce::MidiBuffer& midi, double blockPpq,
                                           double beatsPerSample, bool isPlaying, int numSamples)
{
    // 再生中だけ記録する（停止中に届いた音はソング位置が意味を持たない）。
    // 再生開始・位置ジャンプで区間番号を進める。
    if (! isPlaying)
    {
        midiCapturing = false;
        return;
    }
    if (! midiCapturing || std::abs (blockPpq - midiExpectedPpq) > 0.02)
    {
        ++midiSegment;
        midiCapturing = true;
        // 区間の始まりの印（pitch = -1）。ドラムが 1 音も来ない再生でも古いイベントを捨てられる
        int s1, z1, s2, z2;
        captureFifo.prepareToWrite (1, s1, z1, s2, z2);
        if (z1 > 0)      captureBuffer[static_cast<size_t> (s1)] = { blockPpq, -1, false, midiSegment };
        else if (z2 > 0) captureBuffer[static_cast<size_t> (s2)] = { blockPpq, -1, false, midiSegment };
        captureFifo.finishedWrite (z1 + z2);
    }
    midiExpectedPpq = blockPpq + numSamples * beatsPerSample;

    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const bool on  = msg.isNoteOn();
        const bool off = msg.isNoteOff();
        if (! on && ! off) continue;

        const double ppq = blockPpq + meta.samplePosition * beatsPerSample;

        int start1, size1, start2, size2;
        captureFifo.prepareToWrite (1, start1, size1, start2, size2);
        if (size1 > 0)
            captureBuffer[static_cast<size_t> (start1)] = { ppq, msg.getNoteNumber(), on, midiSegment };
        else if (size2 > 0)
            captureBuffer[static_cast<size_t> (start2)] = { ppq, msg.getNoteNumber(), on, midiSegment };
        captureFifo.finishedWrite (size1 + size2);   // 満杯なら 0（最新をドロップ）
    }
}

// ── Realtime sidechain capture（R11: alloc/lock/file-IO なし）────────
void KemuriBassProcessor::captureSidechain (juce::AudioBuffer<float>& buffer, double ppqStart,
                                            double beatsPerSample, bool isPlaying)
{
    // ホストが有効にした最初の入力バス（通常はサイドチェイン = バス 0）から読む
    int scIndex = -1;
    for (int i = 0; i < getBusCount (true); ++i)
        if (auto* b = getBus (true, i); b != nullptr && b->isEnabled() && b->getNumberOfChannels() > 0)
        {
            scIndex = i;
            break;
        }
    const bool busOn = scIndex >= 0;
    sidechainEnabled.store (busOn);
    if (! busOn || ! isPlaying || beatsPerSample <= 0.0)
    {
        audioCapturing = false;
        sidechainLevel.store (sidechainLevel.load() * 0.9f);
        return;
    }
    const auto sc  = getBusBuffer (buffer, true, scIndex);
    const int  nCh = sc.getNumChannels();
    const int  n   = sc.getNumSamples();
    if (nCh <= 0 || n <= 0)
    {
        audioCapturing = false;
        return;
    }
    if (n / decimFactor + 1 > static_cast<int> (decimScratch.size()))
    {
        audioCapturing = false;   // 想定外に巨大なブロック → 区間を切る
        return;
    }

    const double bpsDecim = beatsPerSample * decimFactor;
    const bool discontinuity = ! audioCapturing || audioOverflow
                               || std::abs (ppqStart - expectedPpq) > 0.02
                               || std::abs (bpsDecim - lastBpsDecim) > 1.0e-6 * bpsDecim;
    if (discontinuity)
    {
        int a1, b1, a2, b2;
        markerFifo.prepareToWrite (1, a1, b1, a2, b2);
        if (b1 + b2 == 0)
        {
            audioCapturing = false;   // マーカーを積めない → 次のブロックで再試行
            return;
        }
        markerBuf[static_cast<size_t> (b1 > 0 ? a1 : a2)] = { decimCounter, ppqStart, bpsDecim, decimRate };
        markerFifo.finishedWrite (1);
        decimPhase = 0;
        aa1.reset(); aa2.reset();
        audioCapturing = true;
        audioOverflow  = false;
    }

    int   m = 0;
    float peak = 0.0f;
    const float invCh = 1.0f / static_cast<float> (nCh);
    for (int j = 0; j < n; ++j)
    {
        float x = 0.0f;
        for (int ch = 0; ch < nCh; ++ch) x += sc.getSample (ch, j);
        x *= invCh;
        peak = std::max (peak, std::abs (x));
        const float y = aa2.process (aa1.process (x));
        if (decimPhase == 0) decimScratch[static_cast<size_t> (m++)] = y;
        decimPhase = (decimPhase + 1) % decimFactor;
    }

    int w1, l1, w2, l2;
    audioFifo.prepareToWrite (m, w1, l1, w2, l2);
    if (l1 + l2 < m)
    {
        audioOverflow = true;     // 取りこぼし → 次のブロックで区間を切り直す
    }
    else
    {
        std::copy (decimScratch.begin(), decimScratch.begin() + l1, audioFifoBuf.begin() + w1);
        if (l2 > 0) std::copy (decimScratch.begin() + l1, decimScratch.begin() + l1 + l2, audioFifoBuf.begin() + w2);
        audioFifo.finishedWrite (l1 + l2);
        decimCounter += m;
    }
    expectedPpq  = ppqStart + n * beatsPerSample;
    lastBpsDecim = bpsDecim;
    sidechainLevel.store (std::max (peak, sidechainLevel.load() * 0.95f));
}

// ── Realtime MIDI output ────────────────────────────────────────────
void KemuriBassProcessor::renderSequence (const MidiSequence& seq, juce::MidiBuffer& midi,
                                          double ppqStart, double beatsPerSample, int numSamples)
{
    const double loopLen = seq.lengthBeats;
    if (loopLen <= 0.0 || beatsPerSample <= 0.0 || numSamples <= 0) return;

    const double ppqEnd = ppqStart + numSamples * beatsPerSample;
    const long   kStart = static_cast<long> (std::floor (ppqStart / loopLen));
    const long   kEnd   = static_cast<long> (std::floor (ppqEnd   / loopLen));

    for (long k = kStart; k <= kEnd; ++k)
    {
        const double base = k * loopLen;
        for (const auto& n : seq.notes)
        {
            const double onPpq  = base + n.start;
            const double offPpq = base + n.start + n.dur;

            if (onPpq >= ppqStart && onPpq < ppqEnd)
            {
                int s = static_cast<int> ((onPpq - ppqStart) / beatsPerSample + 0.5);
                s = std::clamp (s, 0, numSamples - 1);
                midi.addEvent (juce::MidiMessage::noteOn (kMidiChannel, n.pitch,
                                                          static_cast<juce::uint8> (n.vel)), s);
            }
            if (offPpq >= ppqStart && offPpq < ppqEnd)
            {
                int s = static_cast<int> ((offPpq - ppqStart) / beatsPerSample + 0.5);
                s = std::clamp (s, 0, numSamples - 1);
                midi.addEvent (juce::MidiMessage::noteOff (kMidiChannel, n.pitch), s);
            }
        }
    }
}

void KemuriBassProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const MidiSequence* seq = liveSeq.load (std::memory_order_acquire);

    // ── Transport / tempo (R12: PlayHead 無しは内部 120 BPM) ────────
    double bpm       = 120.0;
    double ppqStart  = internalPpq;
    bool   isPlaying = true;   // PlayHead 無し（スタンドアロン）は常時内部再生
    bool   hostPpq   = false;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())          bpm = *b;
            isPlaying = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) { ppqStart = *p; hostPpq = true; }
        }
    }

    const double beatsPerSample = (bpm / 60.0) / sampleRate;

    // ネタの音（サイドチェイン）は出力で上書きする前に読む（同じチャンネルを共有するため）。
    // Live はサイドチェイン未選択時にこのプラグインの前回出力を流すが、出力は常に無音なので無害。
    captureSidechain (buffer, ppqStart, beatsPerSample, isPlaying && hostPpq);
    buffer.clear();

    // 入力 MIDI（ドラム）をキック解析用にキャプチャしてから消す
    captureIncoming (midiMessages, ppqStart, beatsPerSample, isPlaying, numSamples);
    midiMessages.clear();

    // ハングノート防止: シーケンス差し替え / 停止遷移で all-notes-off
    const bool seqChanged  = (seq != lastRendered);
    const bool justStopped = (! isPlaying && wasPlaying);
    if (seqChanged || justStopped)
    {
        midiMessages.addEvent (juce::MidiMessage::allNotesOff (kMidiChannel), 0);
        midiMessages.addEvent (juce::MidiMessage::allControllersOff (kMidiChannel), 0);
    }

    if (seq != nullptr && isPlaying)
        renderSequence (*seq, midiMessages, ppqStart, beatsPerSample, numSamples);

    // 内部クロックを進める（PlayHead 無し時のみ意味を持つ）
    if (getPlayHead() == nullptr)
        internalPpq += numSamples * beatsPerSample;

    lastRendered = seq;
    wasPlaying   = isPlaying;
}

// ── Preview snapshot (UI, message thread) ───────────────────────────
std::vector<kemuri::core::OutNote> KemuriBassProcessor::getPreviewNotes() const
{
    if (const MidiSequence* seq = liveSeq.load (std::memory_order_acquire))
        return seq->notes;
    return {};
}

double KemuriBassProcessor::getPreviewLengthBeats() const
{
    if (const MidiSequence* seq = liveSeq.load (std::memory_order_acquire))
        return seq->lengthBeats;
    return 0.0;
}

// ── SMF export (R3) ─────────────────────────────────────────────────
bool KemuriBassProcessor::exportToMidiFile (const juce::File& dest)
{
    const MidiSequence* seq = liveSeq.load (std::memory_order_acquire);
    if (seq == nullptr)
        return false;
    return writeSequenceToSmf (seq->notes, dest);
}

// ── State ───────────────────────────────────────────────────────────
void KemuriBassProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void KemuriBassProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* KemuriBassProcessor::createEditor()
{
    return new KemuriBassEditor (*this);
}

} // namespace kemuri

// JUCE プラグインエントリポイント
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new kemuri::KemuriBassProcessor();
}
