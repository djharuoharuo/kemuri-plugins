#include "PluginEditor.h"

namespace kemuri
{

KemuriStreamEditor::KemuriStreamEditor (KemuriStreamProcessor& p)
    : juce::AudioProcessorEditor (&p), processorRef (p)
{
    setLookAndFeel (&lookAndFeel);

    titleLabel.setText (juce::String::fromUTF8 ("KemuriStream — \xE9\x85\x8D\xE4\xBF\xA1\xE3\x82\xB7\xE3\x83\x9F\xE3\x83\xA5\xE3\x83\xAC\xE3\x83\xBC\xE3\x82\xBF\xE3\x83\xBC"),
                       juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, ui::colours::accent);
    addAndMakeVisible (titleLabel);

    platformBox.addItemList (kPlatformNames, 1);
    addAndMakeVisible (platformBox);
    addAndMakeVisible (bypassButton);
    addAndMakeVisible (autoLoudButton);
    addAndMakeVisible (realCodecButton);

    auto& apvts = processorRef.getApvts();
    platformAtt  = std::make_unique<ComboAttachment>  (apvts, pid::platform,  platformBox);
    bypassAtt    = std::make_unique<ButtonAttachment> (apvts, pid::bypass,    bypassButton);
    autoLoudAtt  = std::make_unique<ButtonAttachment> (apvts, pid::autoLoud,  autoLoudButton);
    realCodecAtt = std::make_unique<ButtonAttachment> (apvts, pid::realCodec, realCodecButton);

    setSize (420, 260);
}

KemuriStreamEditor::~KemuriStreamEditor()
{
    setLookAndFeel (nullptr);
}

void KemuriStreamEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::colours::background);
}

void KemuriStreamEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    titleLabel.setBounds (area.removeFromTop (32));
    area.removeFromTop (12);

    platformBox.setBounds (area.removeFromTop (30));
    area.removeFromTop (12);

    bypassButton.setBounds    (area.removeFromTop (28));
    autoLoudButton.setBounds  (area.removeFromTop (28));
    realCodecButton.setBounds (area.removeFromTop (28));
}

} // namespace kemuri
