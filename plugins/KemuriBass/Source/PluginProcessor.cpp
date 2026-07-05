#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace kemuri
{

KemuriBassProcessor::KemuriBassProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void KemuriBassProcessor::prepareToPlay (double, int)
{
}

void KemuriBassProcessor::releaseResources()
{
}

bool KemuriBassProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

void KemuriBassProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midiMessages);

    buffer.clear();
}

juce::AudioProcessorEditor* KemuriBassProcessor::createEditor()
{
    return new KemuriBassEditor (*this);
}

void KemuriBassProcessor::getStateInformation (juce::MemoryBlock&)
{
}

void KemuriBassProcessor::setStateInformation (const void*, int)
{
}

} // namespace kemuri

// JUCE プラグインエントリポイント
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new kemuri::KemuriBassProcessor();
}
