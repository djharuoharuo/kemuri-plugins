#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace kemuri::ui
{

// 全 Kemuri プラグイン共通のダークテーマ配色
namespace colours
{
    inline const juce::Colour background     { 0xff17171b };
    inline const juce::Colour panel          { 0xff222228 };
    inline const juce::Colour accent         { 0xffe8a33d }; // amber
    inline const juce::Colour textPrimary    { 0xffe8e8ec };
    inline const juce::Colour textSecondary  { 0xff8a8a94 };
} // namespace colours

class KemuriLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KemuriLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, colours::background);
        setColour (juce::Label::textColourId,                 colours::textPrimary);
        setColour (juce::Slider::thumbColourId,               colours::accent);
        setColour (juce::Slider::trackColourId,               colours::panel);
        setColour (juce::ComboBox::backgroundColourId,        colours::panel);
        setColour (juce::ComboBox::textColourId,              colours::textPrimary);
        setColour (juce::TextButton::buttonColourId,          colours::panel);
        setColour (juce::TextButton::textColourOffId,         colours::textPrimary);
    }

    // 文字サイズの底上げ（0 = JUCE 既定のまま。プラグインごとに任意で設定）
    void setBaseFontSize (float size) { baseFontSize = size; }

    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        return baseFontSize > 0.0f ? juce::Font (juce::FontOptions (baseFontSize + 1.0f))
                                   : LookAndFeel_V4::getComboBoxFont (box);
    }

    juce::Font getPopupMenuFont() override
    {
        return baseFontSize > 0.0f ? juce::Font (juce::FontOptions (baseFontSize + 1.0f))
                                   : LookAndFeel_V4::getPopupMenuFont();
    }

    juce::Font getTextButtonFont (juce::TextButton& b, int buttonHeight) override
    {
        return baseFontSize > 0.0f ? juce::Font (juce::FontOptions (baseFontSize + 2.0f, juce::Font::bold))
                                   : LookAndFeel_V4::getTextButtonFont (b, buttonHeight);
    }

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        if (baseFontSize > 0.0f) l->setFont (juce::FontOptions (baseFontSize));
        return l;
    }

private:
    float baseFontSize = 0.0f;
};

} // namespace kemuri::ui
