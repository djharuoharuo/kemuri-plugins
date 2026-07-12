#include "PluginEditor.h"

namespace kemuri
{

static juce::String lufsStr (float v)
{
    if (v <= -79.0f) return juce::String::fromUTF8 ("--");
    return juce::String (v, 1) + " LUFS";
}

static juce::String dbStr (float v, const char* unit)
{
    if (v <= -199.0f) return juce::String::fromUTF8 ("--");
    return juce::String (v, 1) + " " + unit;
}

void KemuriStreamEditor::setupMeter (MeterRow& row, const juce::String& caption)
{
    row.caption.setText (caption, juce::dontSendNotification);
    row.caption.setColour (juce::Label::textColourId, ui::colours::textSecondary);
    row.caption.setFont (juce::Font (juce::FontOptions (13.0f)));
    addAndMakeVisible (row.caption);

    row.value.setText (juce::String::fromUTF8 ("--"), juce::dontSendNotification);
    row.value.setColour (juce::Label::textColourId, ui::colours::textPrimary);
    row.value.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    row.value.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (row.value);
}

void KemuriStreamEditor::layoutMeter (MeterRow& row, juce::Rectangle<int> area)
{
    row.caption.setBounds (area.removeFromLeft (area.getWidth() / 2));
    row.value.setBounds (area);
}

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

    resetButton.onClick = [this] { processorRef.requestResetMeasurement(); };
    addAndMakeVisible (resetButton);

    setupMeter (integratedMeter, juce::String::fromUTF8 ("Integrated"));
    setupMeter (momentaryMeter,  juce::String::fromUTF8 ("Momentary"));
    setupMeter (truePeakMeter,   juce::String::fromUTF8 ("True Peak"));
    setupMeter (plrMeter,        juce::String::fromUTF8 ("PLR"));

    auto& apvts = processorRef.getApvts();
    platformAtt  = std::make_unique<ComboAttachment>  (apvts, pid::platform,  platformBox);
    bypassAtt    = std::make_unique<ButtonAttachment> (apvts, pid::bypass,    bypassButton);
    autoLoudAtt  = std::make_unique<ButtonAttachment> (apvts, pid::autoLoud,  autoLoudButton);
    realCodecAtt = std::make_unique<ButtonAttachment> (apvts, pid::realCodec, realCodecButton);

    setSize (420, 400);
    startTimerHz (20);
}

KemuriStreamEditor::~KemuriStreamEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void KemuriStreamEditor::timerCallback()
{
    integratedMeter.value.setText (lufsStr (processorRef.getIntegratedLufs()), juce::dontSendNotification);
    momentaryMeter.value.setText  (lufsStr (processorRef.getMomentaryLufs()),  juce::dontSendNotification);
    truePeakMeter.value.setText   (dbStr (processorRef.getTruePeakDb(), "dBTP"), juce::dontSendNotification);
    plrMeter.value.setText        (processorRef.hasMeasurement()
                                       ? dbStr (processorRef.getPlr(), "LU")
                                       : juce::String::fromUTF8 ("--"),
                                   juce::dontSendNotification);
}

void KemuriStreamEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::colours::background);

    // メーター背景パネル
    g.setColour (ui::colours::panel);
    g.fillRoundedRectangle (meterPanel.toFloat(), 6.0f);
}

void KemuriStreamEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    titleLabel.setBounds (area.removeFromTop (32));
    area.removeFromTop (10);

    platformBox.setBounds (area.removeFromTop (30));
    area.removeFromTop (10);

    // メーターパネル（4 行）
    meterPanel = area.removeFromTop (4 * 28 + 16);
    auto meters = meterPanel.reduced (10, 8);
    layoutMeter (integratedMeter, meters.removeFromTop (28));
    layoutMeter (momentaryMeter,  meters.removeFromTop (28));
    layoutMeter (truePeakMeter,   meters.removeFromTop (28));
    layoutMeter (plrMeter,        meters.removeFromTop (28));

    area.removeFromTop (12);
    bypassButton.setBounds    (area.removeFromTop (26));
    autoLoudButton.setBounds  (area.removeFromTop (26));
    realCodecButton.setBounds (area.removeFromTop (26));
    area.removeFromTop (8);
    resetButton.setBounds (area.removeFromTop (30).removeFromLeft (120));
}

} // namespace kemuri
