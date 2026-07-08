#pragma once

#include <memory>

#include <juce_audio_utils/juce_audio_utils.h>
#include <kemuri_ui/KemuriLookAndFeel.h>

#include "PluginProcessor.h"

namespace kemuri
{

// ドラッグすると生成済みループを .mid として DAW へ渡すチップ（R3）。
class MidiDragSource : public juce::Component
{
public:
    explicit MidiDragSource (KemuriBassProcessor& p) : processor (p) {}

    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    KemuriBassProcessor& processor;
    bool dragging = false;
};

// 生成結果のピアノロールプレビュー（M4 UI 仕上げ）。
class PianoRollPreview : public juce::Component
{
public:
    void setSequence (std::vector<kemuri::core::OutNote> notes, double lengthBeats);
    void paint (juce::Graphics&) override;

private:
    std::vector<kemuri::core::OutNote> notes;
    double lengthBeats = 0.0;
};

class KemuriBassEditor : public juce::AudioProcessorEditor,
                         public  juce::FileDragAndDropTarget,
                         private juce::Timer
{
public:
    explicit KemuriBassEditor (KemuriBassProcessor&);
    ~KemuriBassEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // うわネタ MIDI をドロップして解析（ルーティング不要）
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit  (const juce::StringArray& files) override;
    void filesDropped  (const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    bool dropHighlight = false;

    using APVTS = juce::AudioProcessorValueTreeState;

    KemuriBassProcessor&    processorRef;
    ui::KemuriLookAndFeel   lookAndFeel;

    juce::Label   titleLabel;

    juce::ComboBox styleBox, barsBox, keyBox, modeBox;
    juce::Slider   complexitySlider, fillSlider;
    juce::Label    styleLabel, barsLabel, keyLabel, modeLabel, complexityLabel, fillLabel;

    std::unique_ptr<APVTS::ComboBoxAttachment> styleAtt, barsAtt, keyAtt, modeAtt;
    std::unique_ptr<APVTS::SliderAttachment>   complexityAtt, fillAtt;

    juce::TextButton generateButton { "Generate" };
    juce::TextButton analyzeButton  { "Analyze" };
    juce::Label      statusLabel;
    juce::Label      analysisLabel;
    juce::Label      bankLabel;
    PianoRollPreview preview;
    MidiDragSource   dragSource { processorRef };

    void setupCombo (juce::ComboBox& box, const char* paramId);
    void setupRotary (juce::Slider& s);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KemuriBassEditor)
};

} // namespace kemuri
