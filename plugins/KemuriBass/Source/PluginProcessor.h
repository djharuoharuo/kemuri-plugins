#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

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
class KemuriBassProcessor : public juce::AudioProcessor
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

    // ── Generation (message thread) ─────────────────────────────────
    // 現在のパラメータでループを生成し、オーディオスレッドへ publish する。
    void requestGenerate();

    // 直近生成のノート数（UI 表示用、-1 = 未生成）
    int getLastNoteCount() const { return lastNoteCount.load(); }

    // 直近生成を SMF format 0 (PPQ 480) として指定パスへ書き出す（ドラッグアウト用）。
    // 生成済みシーケンスが無ければ false。
    bool exportToMidiFile (const juce::File& dest);

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    kemuri::core::Rng                  rng;

    // ── Lock-free single-producer/single-consumer sequence handoff ──
    // メッセージスレッドが所有・解放し、オーディオスレッドは生ポインタを読むだけ。
    std::atomic<const MidiSequence*>            liveSeq { nullptr };
    std::vector<std::unique_ptr<MidiSequence>>  ownedSeqs;   // message thread only
    std::atomic<int>                            lastNoteCount { -1 };

    // オーディオスレッド状態（ハングノート防止）
    const MidiSequence* lastRendered = nullptr;
    bool                wasPlaying   = false;
    double              sampleRate   = 44100.0;
    double              internalPpq  = 0.0;   // PlayHead 無し時の内部クロック

    void renderSequence (const MidiSequence& seq, juce::MidiBuffer& midi,
                         double ppqStart, double beatsPerSample, int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KemuriBassProcessor)
};

} // namespace kemuri
