#include "PluginProcessor.h"

#include <cmath>

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

    // R17: サンプルレート変更のたびに係数・測定窓を再計算する
    const int nc = juce::jmax (1, getTotalNumInputChannels());
    loudness.prepare (sampleRate, nc);
    truePeak.prepare (nc);
    truePeakHoldLin = 0.0f;

    integratedLufs.store (-80.0f,  std::memory_order_relaxed);
    momentaryLufs.store  (-80.0f,  std::memory_order_relaxed);
    truePeakDb.store     (-200.0f, std::memory_order_relaxed);
    plr.store            (0.0f,    std::memory_order_relaxed);
    measured.store       (false,   std::memory_order_relaxed);
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

    // Reset 要求（R9）: 履歴・ピークホールドをクリア
    if (resetRequested.exchange (false, std::memory_order_relaxed))
    {
        loudness.reset();
        truePeak.reset();
        truePeakHoldLin = 0.0f;
        measured.store (false, std::memory_order_relaxed);
    }

    // ── 測定（入力を非破壊で解析）。R12: 確保・ロックなし ─────────────
    const int numCh      = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();
    if (numCh > 0 && numSamples > 0)
    {
        const float* chPtrs[2] = { nullptr, nullptr };
        for (int ch = 0; ch < numCh; ++ch)
            chPtrs[ch] = buffer.getReadPointer (ch);

        loudness.process (chPtrs, numCh, numSamples);
        truePeak.process (chPtrs, numCh, numSamples);

        // True Peak はピークホールド（Reset まで最大値を保持）
        const float tpLin = truePeak.getPeakLinear();
        if (tpLin > truePeakHoldLin) truePeakHoldLin = tpLin;

        const float li = static_cast<float> (loudness.getIntegratedLufs());
        const float mo = static_cast<float> (loudness.getMomentaryLufs());
        const float tp = (truePeakHoldLin > 0.0f)
                             ? static_cast<float> (20.0 * std::log10 (truePeakHoldLin))
                             : -200.0f;

        integratedLufs.store (li, std::memory_order_relaxed);
        momentaryLufs.store  (mo, std::memory_order_relaxed);
        truePeakDb.store     (tp, std::memory_order_relaxed);
        if (loudness.hasIntegrated())
        {
            plr.store (tp - li, std::memory_order_relaxed);   // PLR = TP - integrated
            measured.store (true, std::memory_order_relaxed);
        }
    }

    // M1: 音声はパススルー（測定のみ）。M2 で処理チェーンを挿入する。
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
