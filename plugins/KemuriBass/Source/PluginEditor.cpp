#include "PluginEditor.h"

#include <kemuri_core/Version.h>

namespace kemuri
{

// ── MidiDragSource ──────────────────────────────────────────────────
void MidiDragSource::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (ui::colours::panel);
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (ui::colours::accent);
    g.drawRoundedRectangle (b, 6.0f, 1.2f);
    g.setColour (ui::colours::textPrimary);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (juce::String::fromUTF8 ("\xE2\x87\xA9  Drag MIDI"),
                getLocalBounds(), juce::Justification::centred);
}

void MidiDragSource::mouseDrag (const juce::MouseEvent&)
{
    if (dragging)
        return;

    const juce::File tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("kemuriBass_"
                                              + juce::String (juce::Time::currentTimeMillis())
                                              + ".mid");
    if (! processor.exportToMidiFile (tmp))
        return;

    dragging = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles (
        { tmp.getFullPathName() }, /*canMoveFiles*/ false, this,
        [this] { dragging = false; });
}

// ── Editor ──────────────────────────────────────────────────────────
KemuriBassEditor::KemuriBassEditor (KemuriBassProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setLookAndFeel (&lookAndFeel);

    titleLabel.setText ("kemuriBass", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, ui::colours::accent);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    auto& apvts = processorRef.getApvts();

    setupCombo (styleBox, pid::style);
    setupCombo (barsBox,  pid::bars);
    setupCombo (keyBox,   pid::key);
    setupCombo (modeBox,  pid::mode);
    styleAtt = std::make_unique<APVTS::ComboBoxAttachment> (apvts, pid::style, styleBox);
    barsAtt  = std::make_unique<APVTS::ComboBoxAttachment> (apvts, pid::bars,  barsBox);
    keyAtt   = std::make_unique<APVTS::ComboBoxAttachment> (apvts, pid::key,   keyBox);
    modeAtt  = std::make_unique<APVTS::ComboBoxAttachment> (apvts, pid::mode,  modeBox);

    setupRotary (complexitySlider);
    setupRotary (fillSlider);
    complexityAtt = std::make_unique<APVTS::SliderAttachment> (apvts, pid::complexity, complexitySlider);
    fillAtt       = std::make_unique<APVTS::SliderAttachment> (apvts, pid::fill,       fillSlider);

    auto makeCaption = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (12.0f));
        l.setColour (juce::Label::textColourId, ui::colours::textSecondary);
        l.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (l);
    };
    makeCaption (styleLabel,      "Style");
    makeCaption (keyLabel,        "Key");
    makeCaption (modeLabel,       "Mode");
    makeCaption (barsLabel,       "Bars");
    makeCaption (complexityLabel, "Complexity");
    makeCaption (fillLabel,       "Fill");

    generateButton.setColour (juce::TextButton::buttonColourId, ui::colours::accent);
    generateButton.setColour (juce::TextButton::textColourOffId, ui::colours::background);
    generateButton.onClick = [this]
    {
        processorRef.requestGenerate();
        timerCallback();
    };
    addAndMakeVisible (generateButton);

    statusLabel.setColour (juce::Label::textColourId, ui::colours::textSecondary);
    statusLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (statusLabel);

    addAndMakeVisible (dragSource);

    timerCallback();
    startTimerHz (10);

    setSize (560, 360);
}

KemuriBassEditor::~KemuriBassEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void KemuriBassEditor::setupCombo (juce::ComboBox& box, const char* paramId)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (
            processorRef.getApvts().getParameter (paramId)))
        box.addItemList (choice->choices, 1);
    addAndMakeVisible (box);
}

void KemuriBassEditor::setupRotary (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 18);
    addAndMakeVisible (s);
}

void KemuriBassEditor::timerCallback()
{
    const int n = processorRef.getLastNoteCount();
    statusLabel.setText (n < 0 ? "no sequence — press Generate"
                               : juce::String (n) + " notes ready",
                         juce::dontSendNotification);
}

void KemuriBassEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::colours::background);
}

void KemuriBassEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto header = area.removeFromTop (40);
    titleLabel.setBounds (header.removeFromLeft (220));

    area.removeFromTop (8);

    // 上段: Style / Key / Mode / Bars（キャプション + コンボ）
    auto row = area.removeFromTop (64);
    auto cell = [&row] (int w) { auto c = row.removeFromLeft (w); row.removeFromLeft (10); return c; };

    auto placeCombo = [] (juce::Rectangle<int> c, juce::Label& cap, juce::ComboBox& box)
    {
        cap.setBounds (c.removeFromTop (16));
        box.setBounds (c.removeFromTop (28));
    };
    placeCombo (cell (180), styleLabel, styleBox);
    placeCombo (cell (90),  keyLabel,   keyBox);
    placeCombo (cell (110), modeLabel,  modeBox);
    placeCombo (cell (80),  barsLabel,  barsBox);

    area.removeFromTop (16);

    // 中段: Complexity / Fill ノブ
    auto knobs = area.removeFromTop (110);
    auto knobCell = [&knobs] () { auto c = knobs.removeFromLeft (100); knobs.removeFromLeft (16); return c; };
    auto placeKnob = [] (juce::Rectangle<int> c, juce::Label& cap, juce::Slider& s)
    {
        cap.setBounds (c.removeFromTop (16));
        s.setBounds (c);
    };
    placeKnob (knobCell(), complexityLabel, complexitySlider);
    placeKnob (knobCell(), fillLabel,       fillSlider);

    // 下段: Generate / status / drag
    auto footer = area.removeFromBottom (48);
    generateButton.setBounds (footer.removeFromLeft (120).reduced (0, 8));
    footer.removeFromLeft (12);
    dragSource.setBounds (footer.removeFromRight (140).reduced (0, 6));
    statusLabel.setBounds (footer.reduced (4, 0));
}

} // namespace kemuri
