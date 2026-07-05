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
};

} // namespace kemuri::ui
