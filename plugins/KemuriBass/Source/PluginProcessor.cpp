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
}

KemuriBassProcessor::~KemuriBassProcessor() = default;

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

    // M2: 入力 MIDI は使わず（M3 で解析に回す）、生成ループを出力する。
    midiMessages.clear();

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
