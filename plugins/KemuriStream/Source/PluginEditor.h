#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <kemuri_ui/KemuriLookAndFeel.h>

#include "PluginProcessor.h"

namespace kemuri
{

// M1: メーター表示（Integrated / Momentary / True Peak / PLR）+ プラットフォーム選択
// + トグル + Reset。アドバイス表示は M3 で追加する。
class KemuriStreamEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit KemuriStreamEditor (KemuriStreamProcessor&);
    ~KemuriStreamEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // 「ラベル: 値」の 1 行メーターを作る補助
    struct MeterRow
    {
        juce::Label caption;
        juce::Label value;
    };
    void setupMeter (MeterRow& row, const juce::String& caption);
    void layoutMeter (MeterRow& row, juce::Rectangle<int> area);

    KemuriStreamProcessor& processorRef;
    ui::KemuriLookAndFeel  lookAndFeel;

    juce::Label     titleLabel;
    juce::ComboBox  platformBox;
    juce::ToggleButton bypassButton    { "Bypass" };
    juce::ToggleButton autoLoudButton  { "Auto LUFS" };
    juce::ToggleButton realCodecButton { "Real Codec (Opus)" };
    juce::TextButton   resetButton     { "Reset" };

    MeterRow integratedMeter, momentaryMeter, truePeakMeter, plrMeter;
    juce::Rectangle<int> meterPanel;

    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<ComboAttachment>  platformAtt;
    std::unique_ptr<ButtonAttachment> bypassAtt;
    std::unique_ptr<ButtonAttachment> autoLoudAtt;
    std::unique_ptr<ButtonAttachment> realCodecAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KemuriStreamEditor)
};

} // namespace kemuri
