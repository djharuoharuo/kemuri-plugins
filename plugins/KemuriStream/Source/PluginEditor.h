#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <kemuri_ui/KemuriLookAndFeel.h>

#include "PluginProcessor.h"

namespace kemuri
{

// M0: 骨格エディタ（タイトル / プラットフォーム選択 / トグル）。
// メーターとアドバイス表示は M1〜M3 で追加する。
class KemuriStreamEditor : public juce::AudioProcessorEditor
{
public:
    explicit KemuriStreamEditor (KemuriStreamProcessor&);
    ~KemuriStreamEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    KemuriStreamProcessor& processorRef;
    ui::KemuriLookAndFeel  lookAndFeel;

    juce::Label     titleLabel;
    juce::ComboBox  platformBox;
    juce::ToggleButton bypassButton   { "Bypass" };
    juce::ToggleButton autoLoudButton { "Auto LUFS" };
    juce::ToggleButton realCodecButton { "Real Codec (Opus)" };

    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<ComboAttachment>  platformAtt;
    std::unique_ptr<ButtonAttachment> bypassAtt;
    std::unique_ptr<ButtonAttachment> autoLoudAtt;
    std::unique_ptr<ButtonAttachment> realCodecAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KemuriStreamEditor)
};

} // namespace kemuri
