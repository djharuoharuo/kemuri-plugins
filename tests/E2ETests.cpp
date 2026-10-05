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

// ドラム MIDI の届き方（Live の MIDI From の下の欄）
enum class DrumSource
{
    pads,        // Pre FX: パッドのノート（キック C1 / スネア D1 / ハット F#1）
    mergedC3,    // Post FX: 全パッドがチェーンの再生音程 C3 の 1 音にまとまる
    kickChainC3  // Kick Drum のチェーンだけ: キックが C3 で届く
};

// 合成したネタの音（サイドチェイン, 2 小節ループ Am|F、ベース A1/F1）とドラム MIDI を
// [startBar, startBar + bars) の区間だけ processBlock に流す（1 回の連続した再生）。
// withDrums = false はドラムのクリップが止まっている再生。戻り値は最初のブロックの出力ピーク。
float playPass (KemuriBassProcessor& proc, FakePlayHead& ph, double startBar, int bars,
                DrumSource drumSource, bool withDrums = true)
{
    const double sr = 44100.0;
    const int bs = 512;
    const int numIn  = proc.getTotalNumInputChannels();
    const int numOut = proc.getTotalNumOutputChannels();
    juce::AudioBuffer<float> buf (std::max (2, std::max (numIn, numOut)), bs);
    juce::MidiBuffer midi;

    const double bps  = (ph.bpm / 60.0) / sr;
    const double ppq0 = startBar * 4.0;
    const long   total = static_cast<long> (bars * 4.0 / bps);
    ph.playing = true;

    // ドラム MIDI イベント（ソング絶対位置）
    std::vector<DrumEvent> drums;
    for (int bar = static_cast<int> (startBar); withDrums && bar < static_cast<int> (startBar) + bars; ++bar)
    {
        const double b0 = bar * 4.0;
        const bool merged = drumSource == DrumSource::mergedC3;
        const int  kickN  = drumSource == DrumSource::pads ? 36 : 60;
        const int  snareN = merged ? 60 : 38;
        const int  hatN   = merged ? 60 : 42;
        for (double k : { 0.0, 1.75, 2.5 }) { drums.push_back ({ b0 + k, kickN, true }); drums.push_back ({ b0 + k + 0.1, kickN, false }); }
        if (drumSource == DrumSource::kickChainC3) continue;
        for (double sn : { 1.0, 3.0 })      { drums.push_back ({ b0 + sn, snareN, true }); drums.push_back ({ b0 + sn + 0.1, snareN, false }); }
        for (int h = 0; h < 16; ++h)
        {
            const double pos = h * 0.25 + ((h % 2) ? 0.04 : 0.0);
            drums.push_back ({ b0 + pos, hatN, true }); drums.push_back ({ b0 + pos + 0.05, hatN, false });
        }
    }

    float firstPeak = 0.0f;
    for (long smp = 0; smp < total; smp += bs)
    {
        buf.clear();
        midi.clear();
        const double blockPpq = ppq0 + smp * bps;
        ph.ppq = blockPpq;

        // ネタの音（サイドチェイン = 入力チャンネル 0/1）
        for (int j = 0; j < bs; ++j)
        {
            const double ppq = blockPpq + j * bps;
            const double t   = (smp + j) / sr;
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

        if (smp == 0)
            for (int c = 0; c < numOut; ++c) firstPeak = std::max (firstPeak, buf.getMagnitude (c, 0, bs));
    }
    return firstPeak;
}

// 停止中のブロック（ソング位置 stopBar で止まっている）。notesWhileStopped を送っても
// キック解析に混ざらないこと。
void stoppedBlocks (KemuriBassProcessor& proc, FakePlayHead& ph, double stopBar, int blocks, int noteWhileStopped)
{
    juce::AudioBuffer<float> buf (std::max (2, proc.getTotalNumOutputChannels()), 512);
    juce::MidiBuffer midi;
    ph.playing = false;
    ph.ppq     = stopBar * 4.0;
    for (int i = 0; i < blocks; ++i)
    {
        buf.clear(); midi.clear();
        if (noteWhileStopped >= 0 && i % 4 == 0)
            midi.addEvent (juce::MidiMessage::noteOn (10, noteWhileStopped, static_cast<juce::uint8> (100)), 0);
        proc.processBlock (buf, midi);
    }
}

void runScenario (double startBar, int style, const char* name, DrumSource drumSource = DrumSource::pads)
{
    std::printf ("\n[%s] song starts at bar %.0f, style %d\n", name, startBar, style);

    KemuriBassProcessor proc;
    expect (proc.getBusCount (true) == 1 && ! proc.getPluginHasMainInput(),
            "single sidechain input, exposed as VST3 aux (no main input)");
    FakePlayHead ph;
    proc.setPlayHead (&ph);

    const double sr = 44100.0;
    const int bs = 512;
    proc.setRateAndBufferSizeDetails (sr, bs);
    proc.prepareToPlay (sr, bs);

    const int numIn = proc.getTotalNumInputChannels();
    expect (numIn >= 2, "sidechain bus enabled by default (input channels = " + juce::String (numIn) + ")");

    const float peak = playPass (proc, ph, startBar, 10, drumSource);
    expect (peak == 0.0f, "audio output stays silent");   // ベースは MIDI のみ

    proc.requestAnalyze();
    const auto summary = proc.getAnalysisSummary();
    std::printf ("  summary:\n%s\n", summary.toRawUTF8());
    expect (summary.contains ("Key A Min"), "key = A minor");
    expect (summary.contains (juce::String::fromUTF8 ("\xE3\x83\xAB\xE3\x83\xBC\xE3\x83\x97" "2")), "neta loop = 2 bars");
    expect (summary.contains ("Am-F"), "progression Am-F (song-phase)");

    if (drumSource == DrumSource::mergedC3)
    {
        // キックを区別できない → 採用せず、Pre FX / Kick Drum を案内。生成は定番キックで続行。
        expect (summary.contains ("C3") && summary.contains ("Pre FX") && summary.contains ("Kick Drum"),
                "merged C3 drums: explains and suggests Pre FX / Kick Drum");
        proc.requestGenerate();
        expect (! proc.getPreviewNotes().empty(), "still generates (fallback kicks)");
        proc.releaseResources();
        proc.setPlayHead (nullptr);
        return;
    }
    if (drumSource == DrumSource::kickChainC3)
        expect (summary.contains (juce::String::fromUTF8 ("\xE3\x82\xAD\xE3\x83\x83\xE3\x82\xAF: C3")),
                "kick-only chain on C3 accepted as kick");
    else
    {
        expect (summary.contains ("C1"), "kick note C1 (36)");
        expect (summary.contains ("swing 58%"), "swing 58% from drums");
    }

    if (auto* p = proc.getApvts().getParameter (pid::style))
        p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (style)));
    proc.requestGenerate();
    const auto notes = proc.getPreviewNotes();
    const double len = proc.getPreviewLengthBeats();
    std::printf ("  %s\n  %d notes / %.0f beats\n", proc.getGenerateSummary().toRawUTF8(), static_cast<int> (notes.size()), len);
    expect (! notes.empty() && len > 0.0, "generated");

    // 小節 0 = ソング小節 ≡ 0（偶数 = Am）→ A、小節 1 → F。
    // 1 拍目に鳴っている音で見る（和音の変化を食って前の小節から伸ばしている場合がある）
    int firstPc[2] = { -1, -1 };
    for (int b = 0; b < 2; ++b)
    {
        double latest = -1e9;
        for (const auto& n : notes)
            if (n.start <= b * 4.0 + 0.2 && n.start + n.dur > b * 4.0 + 0.01 && n.start > latest)
            {
                latest = n.start;
                firstPc[b] = n.pitch % 12;
            }
    }
    expect (firstPc[0] == 9, "bar0 downbeat = A (song-phase correct)");
    expect (firstPc[1] == 5, "bar1 downbeat = F");

    // キック位置（0 / 1.75 / 2.5）にベース
    int onKick = 0;
    for (double k : { 0.0, 1.75, 2.5, 4.0, 5.75, 6.5 })
        for (const auto& n : notes) if (std::abs (n.start - k) < 0.13) { ++onKick; break; }
    // キックの上で伸ばす・食ってつなぐ分は重ならない（9th は 1 拍目・和音の変化・後半 1 回だけ）
    const int needKick = (style == 3) ? 2 : (style == 1 ? 5 : 4);
    expect (onKick >= needKick, "bass locked to your kicks (" + juce::String (onKick) + "/6)");

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
    if (auto* sc = proc.getBus (true, 0)) sc->enable (false);
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

// 実機の報告（2026-10-05）: 1 回 Analyze（キック OK）→ スタイルを変えて何回か Generate →
// もう一度 Analyze すると「キック未検出: C3 の 1 音に…」。前の再生（Post FX の頃の C3）の
// イベントが同じソング位置に残り、最後の再生でドラムが届かなかった区間でそれだけ拾っていた。
void runReanalyzeScenario()
{
    std::printf ("\n[re-analyze] Post FX (C3) bars 0-10 -> Pre FX bars 10-26 Analyze -> Generate x3 -> stop, from top without drum MIDI -> Analyze\n");

    KemuriBassProcessor proc;
    FakePlayHead ph;
    proc.setPlayHead (&ph);
    proc.setRateAndBufferSizeDetails (44100.0, 512);
    proc.prepareToPlay (44100.0, 512);

    playPass (proc, ph, 0.0, 10, DrumSource::mergedC3);           // Post FX の頃（小節 0〜10）
    playPass (proc, ph, 10.0, 16, DrumSource::pads);              // 再生したまま Pre FX に直す（小節 10〜26）
    proc.requestAnalyze();
    const auto s1 = proc.getAnalysisSummary();
    std::printf ("  analyze 1:\n%s\n", s1.toRawUTF8());
    const auto kickLine = juce::String::fromUTF8 ("\xE3\x82\xAD\xE3\x83\x83\xE3\x82\xAF: C1");   // キック: C1
    expect (s1.contains (kickLine), "analyze 1: kick C1 (old C3 pass discarded)");

    for (int st : { 4, 3, 2 })
    {
        if (auto* p = proc.getApvts().getParameter (pid::style))
            p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (st)));
        proc.requestGenerate();
    }

    stoppedBlocks (proc, ph, 0.0, 40, 60);                        // 停止して頭へ。停止中に C3 が来ても無視
    playPass (proc, ph, 0.0, 4, DrumSource::pads, false);         // 頭から 4 小節、ドラムの MIDI が届かない再生
    proc.requestAnalyze();
    const auto s2 = proc.getAnalysisSummary();
    std::printf ("  analyze 2:\n%s\n", s2.toRawUTF8());
    expect (s2.contains (kickLine), "analyze 2: keeps the previous kick C1");
    expect (! s2.contains ("C3"), "analyze 2: no stale C3 from the Post FX pass / stopped notes");
    expect (s2.contains (juce::String::fromUTF8 ("\xE4\xBB\x8A\xE5\x9B\x9E")), "analyze 2: says what failed this time");   // 今回

    proc.requestGenerate();
    const auto gen = proc.getGenerateSummary();
    std::printf ("  %s\n", gen.toRawUTF8());
    expect (gen.contains (juce::String::fromUTF8 ("\xE3\x81\x82\xE3\x81\xAA\xE3\x81\x9F\xE3\x81\xAE\xE3\x82\xAD\xE3\x83\x83\xE3\x82\xAF")),   // あなたのキック
            "generate still syncs to your kick");

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
    runScenario (0.0, 1, "Drum Rack Post FX: all pads merged on C3", DrumSource::mergedC3);
    runScenario (0.0, 1, "Drum Rack Kick Drum chain on C3", DrumSource::kickChainC3);
    runMisroutedScenario();
    runReanalyzeScenario();
    std::printf ("\n%s (%d failures)\n", failures == 0 ? "E2E PASS" : "E2E FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
