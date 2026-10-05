#include "PluginEditor.h"

#include <cmath>
#include <utility>

#include <kemuri_core/MusicTheory.h>
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
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
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

// ── PianoRollPreview ────────────────────────────────────────────────
void PianoRollPreview::setSequence (std::vector<kemuri::core::OutNote> n, double len,
                                    std::vector<double> kickBeats)
{
    notes       = std::move (n);
    kicks       = std::move (kickBeats);
    lengthBeats = len;
    repaint();
}

void PianoRollPreview::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (ui::colours::panel);
    g.fillRoundedRectangle (b, 5.0f);

    if (notes.empty() || lengthBeats <= 0.0)
    {
        g.setColour (ui::colours::textSecondary);
        g.setFont (juce::FontOptions (16.0f));
        g.drawText ("preview", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // 小節グリッド（4 beats ごと）
    const int bars = juce::jmax (1, static_cast<int> (std::lround (lengthBeats / 4.0)));
    g.setColour (ui::colours::background.withAlpha (0.6f));
    for (int bar = 1; bar < bars; ++bar)
    {
        const float x = b.getX() + b.getWidth() * (bar * 4.0f / (float) lengthBeats);
        g.drawVerticalLine (static_cast<int> (x), b.getY(), b.getBottom());
    }

    // ベース音域（28-47）を縦にマップ
    constexpr int lo = kemuri::core::kBassMin;
    constexpr int hi = kemuri::core::kBassMax;
    const float pad  = 4.0f;
    const float rowH = (b.getHeight() - 2 * pad) / static_cast<float> (hi - lo + 1);

    for (const auto& n : notes)
    {
        const float x = b.getX() + b.getWidth() * (float) (n.start / lengthBeats);
        const float w = juce::jmax (2.0f, b.getWidth() * (float) (n.dur / lengthBeats) - 1.0f);
        const int   pc = juce::jlimit (lo, hi, n.pitch);
        const float y  = b.getBottom() - pad - (pc - lo + 1) * rowH;
        g.setColour (ui::colours::accent);
        g.fillRoundedRectangle (x + 0.5f, y, w, juce::jmax (2.0f, rowH - 1.0f), 2.0f);
    }

    // あなたのキック位置（下端のマーカー）
    g.setColour (ui::colours::textPrimary.withAlpha (0.7f));
    for (double k : kicks)
    {
        const float x = b.getX() + b.getWidth() * (float) (k / lengthBeats);
        g.fillRect (x, b.getBottom() - 8.0f, 3.0f, 8.0f);
    }
}

// ── Editor ──────────────────────────────────────────────────────────
KemuriBassEditor::KemuriBassEditor (KemuriBassProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    lookAndFeel.setBaseFontSize (16.0f);   // 読みやすい文字サイズ（v2.0.1）
    setLookAndFeel (&lookAndFeel);

    titleLabel.setText ("kemuriBass", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (32.0f, juce::Font::bold));
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
        l.setFont (juce::FontOptions (15.0f));
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

    analyzeButton.setColour (juce::TextButton::buttonColourId, ui::colours::panel);
    analyzeButton.setColour (juce::TextButton::textColourOffId, ui::colours::textPrimary);
    analyzeButton.onClick = [this]
    {
        processorRef.requestAnalyze();
        timerCallback();
    };
    addAndMakeVisible (analyzeButton);

    analysisLabel.setColour (juce::Label::textColourId, ui::colours::textPrimary);
    analysisLabel.setJustificationType (juce::Justification::topLeft);
    analysisLabel.setFont (juce::FontOptions (16.0f));
    analysisLabel.setMinimumHorizontalScale (0.75f);
    addAndMakeVisible (analysisLabel);

    generateLabel.setColour (juce::Label::textColourId, ui::colours::accent);
    generateLabel.setJustificationType (juce::Justification::topLeft);
    generateLabel.setFont (juce::FontOptions (16.0f));
    generateLabel.setMinimumHorizontalScale (0.75f);
    addAndMakeVisible (generateLabel);

    sidechainLabel.setJustificationType (juce::Justification::centredRight);
    sidechainLabel.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    addAndMakeVisible (sidechainLabel);

    bankLabel.setJustificationType (juce::Justification::centredLeft);
    bankLabel.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (bankLabel);

    addAndMakeVisible (preview);

    statusLabel.setColour (juce::Label::textColourId, ui::colours::textSecondary);
    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setFont (juce::FontOptions (16.0f));
    addAndMakeVisible (statusLabel);

    addAndMakeVisible (dragSource);

    timerCallback();
    startTimerHz (10);

    setSize (780, 720);
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
    analysisLabel.setText (processorRef.getAnalysisSummary(), juce::dontSendNotification);
    generateLabel.setText (processorRef.getGenerateSummary(), juce::dontSendNotification);

    // サイドチェインの 3 状態: 無効 / 有効だが無音 / 受信中
    const int sc = processorRef.getSidechainState();
    const char* scText = sc == 2 ? "\xE2\x97\x8F \xE3\x83\x8D\xE3\x82\xBF\xE5\x8F\x97\xE4\xBF\xA1\xE4\xB8\xAD"                                   // ● ネタ受信中
                       : sc == 1 ? "\xE2\x97\x8B \xE3\x82\xB5\xE3\x82\xA4\xE3\x83\x89\xE3\x83\x81\xE3\x82\xA7\xE3\x82\xA4\xE3\x83\xB3\xE7\x84\xA1\xE9\x9F\xB3"  // ○ サイドチェイン無音
                                 : "\xE2\x97\x8B \xE3\x82\xB5\xE3\x82\xA4\xE3\x83\x89\xE3\x83\x81\xE3\x82\xA7\xE3\x82\xA4\xE3\x83\xB3\xE7\x84\xA1\xE5\x8A\xB9"; // ○ サイドチェイン無効
    sidechainLabel.setText (juce::String::fromUTF8 (scText), juce::dontSendNotification);
    sidechainLabel.setColour (juce::Label::textColourId,
                              sc == 2 ? ui::colours::accent : juce::Colour (0xffd08a3d));

   #ifdef JucePlugin_VersionString
    const juce::String version = "v" JucePlugin_VersionString "  |  ";
   #else
    const juce::String version = "dev  |  ";
   #endif
    bankLabel.setText (version + processorRef.getBankStatus(), juce::dontSendNotification);
    bankLabel.setColour (juce::Label::textColourId,
                         processorRef.hasBankWarning() ? juce::Colour (0xffd08a3d)
                                                       : ui::colours::textSecondary);

    preview.setSequence (processorRef.getPreviewNotes(), processorRef.getPreviewLengthBeats(),
                         processorRef.getPreviewKicks());
}

// ── File drag-in analyze ────────────────────────────────────────────
namespace
{
    bool hasMidiFile (const juce::StringArray& files)
    {
        for (const auto& f : files)
            if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi"))
                return true;
        return false;
    }
} // namespace

bool KemuriBassEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return hasMidiFile (files);
}

void KemuriBassEditor::fileDragEnter (const juce::StringArray& files, int, int)
{
    dropHighlight = hasMidiFile (files);
    repaint();
}

void KemuriBassEditor::fileDragExit (const juce::StringArray&)
{
    dropHighlight = false;
    repaint();
}

void KemuriBassEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dropHighlight = false;
    repaint();
    for (const auto& f : files)
    {
        if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi"))
        {
            processorRef.analyzeMidiFile (juce::File (f));
            timerCallback();
            break;
        }
    }
}

void KemuriBassEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::colours::background);

    if (dropHighlight)
    {
        g.setColour (ui::colours::accent);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (3.0f), 8.0f, 2.5f);
        g.setColour (ui::colours::accent.withAlpha (0.12f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (3.0f), 8.0f);
    }
}

void KemuriBassEditor::resized()
{
    auto area = getLocalBounds().reduced (18);

    auto header = area.removeFromTop (48);
    titleLabel.setBounds (header.removeFromLeft (240));
    analyzeButton.setBounds (header.removeFromRight (140).reduced (0, 4));
    header.removeFromRight (12);
    sidechainLabel.setBounds (header.removeFromRight (240));

    area.removeFromTop (10);

    // 上段: Style / Key / Mode / Bars（キャプション + コンボ）
    auto row = area.removeFromTop (60);
    auto cell = [&row] (int w) { auto c = row.removeFromLeft (w); row.removeFromLeft (14); return c; };

    auto placeCombo = [] (juce::Rectangle<int> c, juce::Label& cap, juce::ComboBox& box)
    {
        cap.setBounds (c.removeFromTop (22));
        box.setBounds (c.removeFromTop (34));
    };
    placeCombo (cell (240), styleLabel, styleBox);
    placeCombo (cell (120), keyLabel,   keyBox);
    placeCombo (cell (150), modeLabel,  modeBox);
    placeCombo (cell (110), barsLabel,  barsBox);

    area.removeFromTop (12);

    // 中段: Complexity / Fill ノブ
    auto knobs = area.removeFromTop (120);
    auto knobCell = [&knobs] () { auto c = knobs.removeFromLeft (120); knobs.removeFromLeft (20); return c; };
    auto placeKnob = [] (juce::Rectangle<int> c, juce::Label& cap, juce::Slider& s)
    {
        cap.setBounds (c.removeFromTop (22));
        s.setBounds (c);
    };
    placeKnob (knobCell(), complexityLabel, complexitySlider);
    placeKnob (knobCell(), fillLabel,       fillSlider);

    // 解析サマリ（ネタ / キック の 2 行）+ 生成（警告含め最大 2 行）+ 学習パターン状態
    area.removeFromTop (10);
    analysisLabel.setBounds (area.removeFromTop (68));   // ネタ / キック / 今回の警告（前回のキックを使う時）
    area.removeFromTop (4);
    generateLabel.setBounds (area.removeFromTop (44));
    bankLabel.setBounds (area.removeFromTop (20));

    // 下段: Generate / status / drag
    auto footer = area.removeFromBottom (58);
    generateButton.setBounds (footer.removeFromLeft (150).reduced (0, 8));
    footer.removeFromLeft (14);
    dragSource.setBounds (footer.removeFromRight (180).reduced (0, 6));
    statusLabel.setBounds (footer.reduced (4, 0));

    // ピアノロールプレビュー（残り全部）
    area.removeFromTop (8);
    area.removeFromBottom (8);
    preview.setBounds (area);
}

} // namespace kemuri
