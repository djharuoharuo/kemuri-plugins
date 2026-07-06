#pragma once

#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include <kemuri_core/MidiAnalyzer.h>
#include <kemuri_core/Rng.h>

#include "MidiSequence.h"

namespace kemuri
{

// パラメータ ID（APVTS / オートメーション用）
namespace pid
{
    inline constexpr const char* style      = "style";
    inline constexpr const char* complexity = "complexity";
    inline constexpr const char* fill       = "fill";
    inline constexpr const char* bars       = "bars";
    inline constexpr const char* key        = "key";
    inline constexpr const char* mode       = "mode";
} // namespace pid

// KemuriBass — Boom-Bap / Soul-Jazz ベースライン・ジェネレーター
class KemuriBassProcessor : public juce::AudioProcessor,
                            private juce::Timer
{
public:
    KemuriBassProcessor();
    ~KemuriBassProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override           { return true; }
    bool producesMidi() const override          { return true; }
    bool isMidiEffect() const override          { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override                              { return 1; }
    int getCurrentProgram() override                           { return 0; }
    void setCurrentProgram (int) override                      {}
    const juce::String getProgramName (int) override           { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getApvts() { return apvts; }

    // ── Generation / Analysis (message thread) ──────────────────────
    void requestGenerate();
    void requestAnalyze();

    int          getLastNoteCount()   const { return lastNoteCount.load(); }
    juce::String getAnalysisSummary() const { return analysisSummary; }

    bool exportToMidiFile (const juce::File& dest);

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;   // MIDI キャプチャのドレイン（message thread）
    void drainCapture();

    juce::AudioProcessorValueTreeState apvts;
    kemuri::core::Rng                  rng;

    // ── Lock-free sequence handoff（生成）────────────────────────────
    std::atomic<const MidiSequence*>            liveSeq { nullptr };
    std::vector<std::unique_ptr<MidiSequence>>  ownedSeqs;   // message thread only
    std::atomic<int>                            lastNoteCount { -1 };

    // ── Realtime MIDI capture（解析用, R5 / R11）─────────────────────
    struct CapturedEvent { double ppq; int pitch; bool isOn; };
    static constexpr int              kFifoCapacity = 8192;
    juce::AbstractFifo                captureFifo { kFifoCapacity };
    std::array<CapturedEvent, kFifoCapacity> captureBuffer {};
    std::deque<CapturedEvent>         recentEvents;   // message thread only（直近64小節）
    static constexpr double           kWindowBeats = 64.0 * 4.0;

    // 解析結果（message thread）
    kemuri::core::AnalysisResult analysis;
    bool                         hasAnalysis = false;
    juce::String                 analysisSummary { "no analysis" };

    // オーディオスレッド状態
    const MidiSequence* lastRendered = nullptr;
    bool                wasPlaying   = false;
    double              sampleRate   = 44100.0;
    double              internalPpq  = 0.0;

    void captureIncoming (const juce::MidiBuffer& midi, double blockPpq, double beatsPerSample);
    void renderSequence (const MidiSequence& seq, juce::MidiBuffer& midi,
                         double ppqStart, double beatsPerSample, int numSamples);
    void setChoiceParam (const char* id, int index);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KemuriBassProcessor)
};

} // namespace kemuri
