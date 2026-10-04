// v2 統合テスト: プラグイン本体（KemuriBassProcessor）を丸ごと動かす。
// 合成したネタの音（サイドチェイン, 2 小節ループ Am|F、ベース A1/F1）と
// ドラム MIDI（キック 36 @ 0 / 1.75 / 2.5、スネア、16 分ハット）を processBlock に
// 流し込み、Analyze → Generate の結果を検査する。ソング途中（5 小節目）から再生した
// 場合でも位相がソング基準で合うこと（v1.x のバグの再発防止）を確認する。
#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace kemuri;

namespace
{
int failures = 0;
void expect (bool c, const juce::String& label)
{
    std::printf ("%s %s\n", c ? "  ok  " : "  FAIL", label.toRawUTF8());
    if (! c) ++failures;
}

struct FakePlayHead : public juce::AudioPlayHead
{
    double bpm = 90.0, ppq = 0.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setIsPlaying (playing);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

double m2hz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }

struct DrumEvent { double ppq; int note; bool on; };

void runScenario (double startBar, int style, const char* name)
{
    std::printf ("\n[%s] song starts at bar %.0f, style %d\n", name, startBar, style);

    KemuriBassProcessor proc;
    FakePlayHead ph;
    proc.setPlayHead (&ph);

    const double sr = 44100.0;
    const int bs = 512;
    proc.setRateAndBufferSizeDetails (sr, bs);
    proc.prepareToPlay (sr, bs);

    const int numIn  = proc.getTotalNumInputChannels();
    const int numOut = proc.getTotalNumOutputChannels();
    expect (numIn >= 2, "sidechain bus enabled by default (input channels = " + juce::String (numIn) + ")");
    juce::AudioBuffer<float> buf (std::max (numIn, numOut), bs);
    juce::MidiBuffer midi;

    const double bps  = (ph.bpm / 60.0) / sr;
    const int    bars = 10;
    const double ppq0 = startBar * 4.0;
    const long   total = static_cast<long> (bars * 4.0 / bps);

    // ドラム MIDI イベント（ソング絶対位置）
    std::vector<DrumEvent> drums;
    for (int bar = static_cast<int> (startBar); bar < static_cast<int> (startBar) + bars; ++bar)
    {
        const double b0 = bar * 4.0;
        for (double k : { 0.0, 1.75, 2.5 }) { drums.push_back ({ b0 + k, 36, true }); drums.push_back ({ b0 + k + 0.1, 36, false }); }
        for (double s : { 1.0, 3.0 })       { drums.push_back ({ b0 + s, 38, true }); drums.push_back ({ b0 + s + 0.1, 38, false }); }
        for (int h = 0; h < 16; ++h)
        {
            const double pos = h * 0.25 + ((h % 2) ? 0.04 : 0.0);
            drums.push_back ({ b0 + pos, 42, true }); drums.push_back ({ b0 + pos + 0.05, 42, false });
        }
    }

    for (long s = 0; s < total; s += bs)
    {
        buf.clear();
        midi.clear();
        const double blockPpq = ppq0 + s * bps;
        ph.ppq = blockPpq;

        // ネタの音（サイドチェイン = 入力チャンネル 0/1。主入力は無効で 0ch）
        for (int j = 0; j < bs; ++j)
        {
            const double ppq = blockPpq + j * bps;
            const double t   = (s + j) / sr;
            const long   bar = static_cast<long> (std::floor (ppq / 4.0));
            const bool   am  = (bar % 2 == 0);
            const int ch[3] = { am ? 57 : 53, am ? 60 : 57, am ? 64 : 60 };
            double x = 0.0;
            for (int m : ch)
                for (int hh = 1; hh <= 4; ++hh)
                    x += 0.12 / hh * std::sin (2.0 * juce::MathConstants<double>::pi * m2hz (m) * hh * t);
            const int bassM = am ? 33 : 29;
            const double env = std::exp (-std::fmod (ppq, 1.0) * 2.0);
            x += env * (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * m2hz (bassM) * t)
                        + 0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * m2hz (bassM) * 2 * t));
            for (int c = 0; c < std::min (2, buf.getNumChannels()); ++c)
                buf.setSample (c, j, static_cast<float> (x * 0.5));
        }

        // このブロックに入るドラム MIDI
        const double blockEnd = blockPpq + bs * bps;
        for (const auto& d : drums)
            if (d.ppq >= blockPpq && d.ppq < blockEnd)
            {
                const int pos = std::clamp (static_cast<int> ((d.ppq - blockPpq) / bps), 0, bs - 1);
                midi.addEvent (d.on ? juce::MidiMessage::noteOn (10, d.note, static_cast<juce::uint8> (110))
                                    : juce::MidiMessage::noteOff (10, d.note), pos);
            }

        proc.processBlock (buf, midi);

        // 出力は無音であること（ベースは MIDI のみ）
        if (s == 0)
        {
            float peak = 0.0f;
            for (int c = 0; c < numOut; ++c) peak = std::max (peak, buf.getMagnitude (c, 0, bs));
            expect (peak == 0.0f, "audio output stays silent");
        }
    }

    proc.requestAnalyze();
    const auto summary = proc.getAnalysisSummary();
    std::printf ("  summary:\n%s\n", summary.toRawUTF8());
    expect (summary.contains ("Key A Min"), "key = A minor");
    expect (summary.contains (juce::String::fromUTF8 ("\xE3\x83\xAB\xE3\x83\xBC\xE3\x83\x97" "2")), "neta loop = 2 bars");
    expect (summary.contains ("Am-F"), "progression Am-F (song-phase)");
    expect (summary.contains ("C1"), "kick note C1 (36)");
    expect (summary.contains ("swing 58%"), "swing 58% from drums");

    if (auto* p = proc.getApvts().getParameter (pid::style))
        p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (style)));
    proc.requestGenerate();
    const auto notes = proc.getPreviewNotes();
    const double len = proc.getPreviewLengthBeats();
    std::printf ("  %s\n  %d notes / %.0f beats\n", proc.getGenerateSummary().toRawUTF8(), static_cast<int> (notes.size()), len);
    expect (! notes.empty() && len > 0.0, "generated");

    // 小節 0 = ソング小節 ≡ 0（偶数 = Am）→ A、小節 1 → F
    int firstPc[2] = { -1, -1 };
    for (const auto& n : notes)
        for (int b = 0; b < 2; ++b)
            if (firstPc[b] < 0 && n.start >= b * 4.0 - 1e-6 && n.start < b * 4.0 + 0.2) firstPc[b] = n.pitch % 12;
    expect (firstPc[0] == 9, "bar0 downbeat = A (song-phase correct)");
    expect (firstPc[1] == 5, "bar1 downbeat = F");

    // キック位置（0 / 1.75 / 2.5）にベース
    int onKick = 0;
    for (double k : { 0.0, 1.75, 2.5, 4.0, 5.75, 6.5 })
        for (const auto& n : notes) if (std::abs (n.start - k) < 0.13) { ++onKick; break; }
    expect (onKick >= 5, "bass locked to your kicks (" + juce::String (onKick) + "/6)");

    bool reg = true;
    for (const auto& n : notes) if (n.pitch < 28 || n.pitch > 43) reg = false;
    expect (reg, "register E1..G2");

    proc.releaseResources();
    proc.setPlayHead (nullptr);
}

// 接続ミスの診断: サイドチェイン無効 + MIDI From がネタ（パッド D#3 = 63 で鳴るチョップ）
void runMisroutedScenario()
{
    std::printf ("\n[misrouted] sidechain disabled, MIDI input = neta chops (D#3)\n");

    KemuriBassProcessor proc;
    if (auto* sc = proc.getBus (true, 1)) sc->enable (false);
    FakePlayHead ph;
    proc.setPlayHead (&ph);
    const double sr = 44100.0;
    const int bs = 512;
    proc.setRateAndBufferSizeDetails (sr, bs);
    proc.prepareToPlay (sr, bs);

    juce::AudioBuffer<float> buf (std::max (2, proc.getTotalNumInputChannels()), bs);
    juce::MidiBuffer midi;
    const double bps = (ph.bpm / 60.0) / sr;
    const long total = static_cast<long> (8 * 4.0 / bps);
    for (long s = 0; s < total; s += bs)
    {
        buf.clear(); midi.clear();
        const double blockPpq = s * bps;
        ph.ppq = blockPpq;
        const double blockEnd = blockPpq + bs * bps;
        for (int bar = 0; bar < 8; ++bar)
            for (double p : { 0.0, 2.5 })
            {
                const double at = bar * 4.0 + p;
                if (at >= blockPpq && at < blockEnd)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 63, static_cast<juce::uint8> (100)),
                                   std::clamp (static_cast<int> ((at - blockPpq) / bps), 0, bs - 1));
            }
        proc.processBlock (buf, midi);
    }
    expect (proc.getSidechainState() == 0, "sidechain state = disabled");

    proc.requestAnalyze();
    const auto summary = proc.getAnalysisSummary();
    std::printf ("  summary:\n%s\n", summary.toRawUTF8());
    expect (summary.contains (juce::String::fromUTF8 ("\xE3\x82\xB5\xE3\x82\xA4\xE3\x83\x89\xE3\x83\x81\xE3\x82\xA7\xE3\x82\xA4\xE3\x83\xB3\xE3\x81\x8C\xE7\x84\xA1\xE5\x8A\xB9")),
            "explains: sidechain disabled");
    expect (summary.contains ("D#3") && summary.contains ("MIDI From"), "warns: D#3 is not a kick, check MIDI From");

    proc.requestGenerate();
    const auto gen = proc.getGenerateSummary();
    std::printf ("  %s\n", gen.toRawUTF8());
    expect (gen.contains (juce::String::fromUTF8 ("\xE3\x83\x8D\xE3\x82\xBF\xE6\x9C\xAA\xE8\xA7\xA3\xE6\x9E\x90")),
            "warns when generating without neta");

    proc.releaseResources();
    proc.setPlayHead (nullptr);
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    runScenario (0.0, 1, "Premier from bar 0");
    runScenario (5.0, 1, "Premier from bar 5 (phase)");
    runScenario (0.0, 4, "Pete Rock");
    runScenario (0.0, 3, "9th Wonder");
    runMisroutedScenario();
    std::printf ("\n%s (%d failures)\n", failures == 0 ? "E2E PASS" : "E2E FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
