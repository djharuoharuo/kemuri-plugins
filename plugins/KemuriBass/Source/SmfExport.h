#pragma once

#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include <kemuri_core/Types.h>

namespace kemuri
{

inline constexpr int kSmfPpq     = 480;   // SMF format 0 の分解能（R3）
inline constexpr int kSmfChannel = 1;

// OutNote 列を SMF format 0 (PPQ 480) として dest へ書く。
// Processor（ドラッグアウト）と単体テストで共有する実体。
inline bool writeSequenceToSmf (const std::vector<core::OutNote>& notes, const juce::File& dest)
{
    if (notes.empty())
        return false;

    juce::MidiMessageSequence track;
    for (const auto& n : notes)
    {
        const double onTick  = n.start * kSmfPpq;
        const double offTick = (n.start + n.dur) * kSmfPpq;
        track.addEvent (juce::MidiMessage::noteOn (kSmfChannel, n.pitch,
                                                   static_cast<juce::uint8> (n.vel)), onTick);
        track.addEvent (juce::MidiMessage::noteOff (kSmfChannel, n.pitch), offTick);
    }
    track.updateMatchedPairs();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (kSmfPpq);
    file.addTrack (track);

    dest.deleteFile();
    juce::FileOutputStream stream (dest);
    if (! stream.openedOk())
        return false;
    return file.writeTo (stream);   // 単一トラック = SMF format 0
}

} // namespace kemuri
