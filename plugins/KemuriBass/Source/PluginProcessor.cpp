#include "PluginProcessor.h"
#include "PluginEditor.h"
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
} // namespace

KemuriBassProcessor::KemuriBassProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createLayout())
{
    startTimerHz (25);   // MIDI キャプチャのドレイン（message thread）
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
    sampleRate  = newSampleRate;
    internalPpq = 0.0;
    lastRendered = nullptr;
    wasPlaying   = false;
    captureFifo.reset();
}

void KemuriBassProcessor::releaseResources()
{
}

bool KemuriBassProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

// ── Generation (message thread) ─────────────────────────────────────
void KemuriBassProcessor::requestGenerate()
{
    using namespace kemuri::core;

    GenerateConfig cfg;
    cfg.style      = static_cast<int> (apvts.getRawParameterValue (pid::style)->load());
    cfg.complexity = static_cast<int> (apvts.getRawParameterValue (pid::complexity)->load());
    cfg.fill       = static_cast<int> (apvts.getRawParameterValue (pid::fill)->load());
    cfg.bars       = kBarValues[std::clamp (
        static_cast<int> (apvts.getRawParameterValue (pid::bars)->load()), 0, 2)];
    cfg.root       = static_cast<int> (apvts.getRawParameterValue (pid::key)->load());
    cfg.mode       = static_cast<int> (apvts.getRawParameterValue (pid::mode)->load());

    // 解析済みなら進行追従・コール&レスポンスを反映（R5）
    if (hasAnalysis && analysis.hasInput && ! analysis.progBar.empty())
    {
        cfg.useProgression = true;
        cfg.progBar        = analysis.progBar;
        cfg.progHalfBar    = analysis.progHalfBar;
        cfg.loopBars       = analysis.loopBars;
        if (analysis.hasOnset)
            cfg.onsetHist = analysis.onsetHist;
    }

    auto seq = std::make_unique<MidiSequence>();
    seq->notes       = buildNotes (cfg, rng);
    seq->lengthBeats = cfg.bars * 4.0;

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
void KemuriBassProcessor::applyAnalysis (const std::vector<kemuri::core::RawNote>& notes,
                                         const char* sourceTag)
{
    using namespace kemuri::core;

    analysis    = analyzeNotes (notes);
    hasAnalysis = analysis.hasInput;

    if (! analysis.hasInput)
    {
        analysisSummary = juce::String::fromUTF8 ("\xE5\x85\xA5\xE5\x8A\x9B\xE3\x81\xAA\xE3\x81\x97");
        return;   // R8: 手動 Key/Mode を維持、例外は投げない
    }

    // 検出したキー/モードを UI パラメータへ反映
    setChoiceParam (pid::key,  analysis.keyRoot);
    setChoiceParam (pid::mode, analysis.keyMode);

    // 進行サマリ
    juce::String prog;
    const int shown = std::min<int> (8, static_cast<int> (analysis.progBar.size()));
    for (int i = 0; i < shown; ++i)
    {
        if (i > 0) prog << "-";
        prog << kKeyNames[analysis.progBar[static_cast<size_t> (i)].root];
        if (analysis.progBar[static_cast<size_t> (i)].quality == "min") prog << "m";
    }
    if (static_cast<int> (analysis.progBar.size()) > shown) prog << "...";

    analysisSummary = juce::String (sourceTag) + "Key " + kKeyNames[analysis.keyRoot] + " "
                      + (analysis.keyMode == 0 ? "Maj" : "Min")
                      + "  |  Loop " + juce::String (analysis.loopBars) + " bars"
                      + "  |  " + juce::String (analysis.notesPerBar, 1) + " n/bar"
                      + "  |  " + prog;
}

void KemuriBassProcessor::requestAnalyze()
{
    using namespace kemuri::core;

    drainCapture();   // 最新のキャプチャを取り込む

    if (recentEvents.empty())
    {
        hasAnalysis     = false;
        analysisSummary = juce::String::fromUTF8 ("\xE5\x85\xA5\xE5\x8A\x9B\xE3\x81\xAA\xE3\x81\x97"
                                                  " \xE2\x80\x94 MIDI\xE3\x82\x92\xE3\x81\x93\xE3\x81\x93"
                                                  "\xE3\x81\xB8\xE3\x83\x89\xE3\x83\xAD\xE3\x83\x83\xE3\x83\x97"
                                                  "\xE3\x81\x99\xE3\x82\x8B\xE3\x81\x8B\xE3\x80\x81"
                                                  "MIDI From \xE3\x81\xA7\xE5\x85\xA5\xE5\x8A\x9B\xE3\x82\x92"
                                                  "\xE7\xB9\x8B\xE3\x81\x84\xE3\x81\xA7\xE5\x86\x8D\xE7\x94\x9F");
        return;
    }

    const double windowEnd   = recentEvents.back().ppq;
    const double windowStart = std::max (recentEvents.front().ppq, windowEnd - kWindowBeats);

    std::vector<RawEvent> events;
    events.reserve (recentEvents.size());
    for (const auto& e : recentEvents)
        events.push_back ({ e.ppq, e.pitch, e.isOn });

    applyAnalysis (pairEvents (events, windowStart, windowEnd), "");
}

// ドロップした .mid ファイルを解析（ルーティング不要）。
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

    for (auto& n : notes) n.start -= minStart;   // 先頭を 0 に揃える
    std::sort (notes.begin(), notes.end(),
               [] (const RawNote& a, const RawNote& b) { return a.start < b.start; });

    applyAnalysis (notes, "[file] ");
    return true;
}

// オーディオスレッドが積んだイベントを message thread の 64 小節リングへ移す。
void KemuriBassProcessor::drainCapture()
{
    int start1, size1, start2, size2;
    const int ready = captureFifo.getNumReady();
    captureFifo.prepareToRead (ready, start1, size1, start2, size2);
    for (int i = 0; i < size1; ++i) recentEvents.push_back (captureBuffer[static_cast<size_t> (start1 + i)]);
    for (int i = 0; i < size2; ++i) recentEvents.push_back (captureBuffer[static_cast<size_t> (start2 + i)]);
    captureFifo.finishedRead (size1 + size2);

    if (! recentEvents.empty())
    {
        const double cutoff = recentEvents.back().ppq - kWindowBeats;
        while (! recentEvents.empty() && recentEvents.front().ppq < cutoff)
            recentEvents.pop_front();
    }
}

void KemuriBassProcessor::timerCallback()
{
    drainCapture();
}

// ── Realtime MIDI capture（R11: alloc/lock/file-IO なし）─────────────
void KemuriBassProcessor::captureIncoming (const juce::MidiBuffer& midi, double blockPpq,
                                           double beatsPerSample)
{
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
            captureBuffer[static_cast<size_t> (start1)] = { ppq, msg.getNoteNumber(), on };
        else if (size2 > 0)
            captureBuffer[static_cast<size_t> (start2)] = { ppq, msg.getNoteNumber(), on };
        captureFifo.finishedWrite (size1 + size2);   // 満杯なら 0（最新をドロップ）
    }
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
    buffer.clear();

    const int numSamples = buffer.getNumSamples();
    const MidiSequence* seq = liveSeq.load (std::memory_order_acquire);

    // ── Transport / tempo (R12: PlayHead 無しは内部 120 BPM) ────────
    double bpm       = 120.0;
    double ppqStart  = internalPpq;
    bool   isPlaying = true;   // PlayHead 無し（スタンドアロン）は常時内部再生

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())          bpm = *b;
            isPlaying = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition())  ppqStart = *p;
        }
    }

    const double beatsPerSample = (bpm / 60.0) / sampleRate;

    // 入力 MIDI（うわネタ）を解析用にキャプチャしてから消す（R5）
    captureIncoming (midiMessages, ppqStart, beatsPerSample);
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
