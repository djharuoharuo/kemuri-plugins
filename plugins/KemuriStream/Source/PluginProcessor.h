#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace kemuri
{

// パラメータ ID（APVTS / オートメーション用）— R10
namespace pid
{
    inline constexpr const char* platform  = "platform";  // 0..5
    inline constexpr const char* bypass    = "bypass";
    inline constexpr const char* autoLoud  = "autoloud";
    inline constexpr const char* realCodec = "realcodec";
} // namespace pid

// live.tab と同じ並び（Off / Spotify / YouTube / Apple / TIDAL / SoundCloud）
inline const juce::StringArray kPlatformNames {
    "Off", "Spotify", "YouTube", "Apple Music", "TIDAL", "SoundCloud"
};

// KemuriStream — 配信シミュレーター + AI アドバイザー（M0: パススルー骨格）
class KemuriStreamProcessor : public juce::AudioProcessor
{
public:
    KemuriStreamProcessor();
    ~KemuriStreamProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override            { return false; }
    bool producesMidi() const override           { return false; }
    bool isMidiEffect() const override           { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // ホスト汎用バイパスを APVTS の bypass に接続する（R8）
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    int getNumPrograms() override                              { return 1; }
    int getCurrentProgram() override                           { return 0; }
    void setCurrentProgram (int) override                      {}
    const juce::String getProgramName (int) override           { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getApvts() { return apvts; }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioParameterBool* bypassParam = nullptr;

    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KemuriStreamProcessor)
};

} // namespace kemuri
