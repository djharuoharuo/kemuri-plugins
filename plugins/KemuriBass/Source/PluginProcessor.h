#pragma once

#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include <kemuri_core/AudioAnalyzer.h>
#include <kemuri_core/BassEngineV2.h>
#include <kemuri_core/DrumAnalyzer.h>
#include <kemuri_core/Dsp.h>
#include <kemuri_core/MidiAnalyzer.h>
#include <kemuri_core/PatternBank.h>
#include <kemuri_core/Rng.h>
#include <kemuri_core/Types.h>

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
    void requestAnalyze();                        // 入力キャプチャから解析
    bool analyzeMidiFile (const juce::File& file); // ドロップした .mid から解析

    int          getLastNoteCount()   const { return lastNoteCount.load(); }
    juce::String getAnalysisSummary() const { return analysisSummary; }
    juce::String getGenerateSummary() const { return generateSummary; }

    // ネタの音（サイドチェイン）がいま届いているか（UI 表示用）
    bool isSidechainReceiving() const { return sidechainLevel.load() > 1.0e-4f; }

    // 学習パターン (patterns.json) の状態（UI 表示用）
    juce::String getBankStatus() const { return bankStatus; }
    bool         hasBankWarning() const { return bankWarning; }

    // 直近生成のノート（UI プレビュー用のスナップショット、message thread）
    std::vector<kemuri::core::OutNote> getPreviewNotes() const;
    double getPreviewLengthBeats() const;
    std::vector<double> getPreviewKicks() const { return previewKickBeats; }   // キック位置（beats）

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

    // ── v2: ネタのオーディオ（サイドチェイン）取り込み（R5 改 / R11）────
    // オーディオスレッド: モノラル化 → アンチエイリアス LPF → 約 11 kHz へ間引き →
    // ロックフリー FIFO。区間の始まり（再生開始・テンポ変化・位置ジャンプ）には
    // マーカー（サンプル番号 ↔ ソング位置）を積む。message thread で連続区間に組み立てる。
    static constexpr int kAudioFifoCapacity = 1 << 20;       // 約 95 秒分
    juce::AbstractFifo   audioFifo { kAudioFifoCapacity };
    std::vector<float>   audioFifoBuf;                       // コンストラクタで確保（audio thread では確保しない）
    struct AudioMarker { long long sampleIndex; double ppq; double beatsPerSample; double sampleRate; };
    juce::AbstractFifo   markerFifo { 256 };
    std::array<AudioMarker, 256> markerBuf {};
    std::array<float, 8192> decimScratch {};                 // audio thread の作業領域

    // audio thread 状態
    int    decimFactor   = 4;
    double decimRate     = 11025.0;
    int    decimPhase    = 0;
    long long decimCounter = 0;
    bool   audioCapturing = false;
    bool   audioOverflow  = false;
    double expectedPpq    = -1.0;
    double lastBpsDecim   = 0.0;
    kemuri::core::dsp::Biquad aa1, aa2;
    std::atomic<float> sidechainLevel { 0.0f };

    // message thread 状態
    kemuri::core::CapturedAudio captured;
    long long capturedStartIndex = 0;
    bool      haveAudioSegment   = false;
    std::deque<AudioMarker> pendingMarkers;
    void drainAudio();
    void captureSidechain (juce::AudioBuffer<float>& buffer, double ppqStart,
                           double beatsPerSample, bool isPlaying);

    // ── 解析結果（message thread）────────────────────────────────────
    kemuri::core::HarmonyAnalysis harmonyV2;     // ネタ（音 or .mid）
    kemuri::core::KickAnalysis    kickV2;        // ドラム MIDI のキック
    juce::String                  harmonySource; // "音" / ".mid"
    juce::String                  generateSummary;
    std::vector<double>           previewKickBeats;
    // 初期ヒント
    juce::String                 analysisSummary {
        juce::CharPointer_UTF8 ("\xE3\x82\xB5\xE3\x82\xA4\xE3\x83\x89\xE3\x83\x81\xE3\x82\xA7\xE3\x82\xA4\xE3\x83\xB3 = "
                                "\xE3\x83\x8D\xE3\x82\xBF\xE3\x81\xAE\xE3\x83\x88\xE3\x83\xA9\xE3\x83\x83\xE3\x82\xAF / MIDI From = "
                                "\xE3\x83\x89\xE3\x83\xA9\xE3\x83\xA0 \xE2\x86\x92 \xE5\x86\x8D\xE7\x94\x9F\xE3\x81\x97\xE3\x81\xA6 Analyze") };

    // 学習パターン束（patterns.json をマージ済み, R6/R9）
    kemuri::core::PatternBank bank;
    juce::String              bankStatus;
    bool                      bankWarning = false;
    void loadPatternBank();

    // オーディオスレッド状態
    const MidiSequence* lastRendered = nullptr;
    bool                wasPlaying   = false;
    double              sampleRate   = 44100.0;
    double              internalPpq  = 0.0;

    void captureIncoming (const juce::MidiBuffer& midi, double blockPpq, double beatsPerSample);
    void renderSequence (const MidiSequence& seq, juce::MidiBuffer& midi,
                         double ppqStart, double beatsPerSample, int numSamples);
    void setChoiceParam (const char* id, int index);
    void updateAnalysisSummary();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KemuriBassProcessor)
};

} // namespace kemuri
