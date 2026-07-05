#include "PluginEditor.h"

#include <kemuri_core/Version.h>

namespace kemuri
{

KemuriBassEditor::KemuriBassEditor (KemuriBassProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setLookAndFeel (&lookAndFeel);

    titleLabel.setText ("kemuriBass", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, ui::colours::accent);
    titleLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (titleLabel);

    statusLabel.setText (juce::String ("M0 skeleton  v") + core::versionString,
                         juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, ui::colours::textSecondary);
    statusLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (statusLabel);

    setSize (480, 320);
}

KemuriBassEditor::~KemuriBassEditor()
{
    setLookAndFeel (nullptr);
}

void KemuriBassEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::colours::background);
}

void KemuriBassEditor::resized()
{
    auto bounds = getLocalBounds();
    titleLabel.setBounds (bounds.removeFromTop (bounds.getHeight() / 2).reduced (10));
    statusLabel.setBounds (bounds.reduced (10));
}

} // namespace kemuri
