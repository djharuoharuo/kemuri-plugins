#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <kemuri_ui/KemuriLookAndFeel.h>

#include "PluginProcessor.h"

namespace kemuri
{

class KemuriBassEditor : public juce::AudioProcessorEditor
{
public:
    explicit KemuriBassEditor (KemuriBassProcessor&);
    ~KemuriBassEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    KemuriBassProcessor& processorRef;
    ui::KemuriLookAndFeel lookAndFeel;

    juce::Label titleLabel;
    juce::Label statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KemuriBassEditor)
};

} // namespace kemuri
