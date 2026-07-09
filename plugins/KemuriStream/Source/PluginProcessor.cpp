#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace kemuri
{

KemuriStreamProcessor::KemuriStreamProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "state", createLayout())
{
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (pid::bypass));
    jassert (bypassParam != nullptr);
}

KemuriStreamProcessor::~KemuriStreamProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout KemuriStreamProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { pid::platform, 1 }, "Platform", kPlatformNames, 0));
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { pid::bypass, 1 }, "Bypass", false));
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { pid::autoLoud, 1 }, "Auto LUFS", false));
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { pid::realCodec, 1 }, "Real Codec", false));

    return layout;
}

void KemuriStreamProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
}

void KemuriStreamProcessor::releaseResources() {}

bool KemuriStreamProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void KemuriStreamProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // 入力より多い出力チャンネルがあればクリア（安全策）
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // M0: パススルー（信号は素通し）。M1 以降で測定・処理を挿入する。
}

juce::AudioProcessorEditor* KemuriStreamProcessor::createEditor()
{
    return new KemuriStreamEditor (*this);
}

void KemuriStreamProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
}

void KemuriStreamProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

} // namespace kemuri

// DAW が生成するプラグイン・エントリポイント
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new kemuri::KemuriStreamProcessor();
}
