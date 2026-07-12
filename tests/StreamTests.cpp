// G3: KemuriStream の測定 DSP が Python 正本と一致することを検証する。
//   biquad 係数      -> codec_eq.py（RBJ cookbook）と 1e-9 一致
//   integrated LUFS  -> pyloudnorm（BS.1770-4）と ±0.1 LU 一致
//   True Peak        -> analyzer.py（4x FFT sinc）と ±0.3 dB 一致
// 参照値: tests/reference/stream_measurement.json（gen_reference.py 生成）
#include <juce_core/juce_core.h>

#include <cmath>
#include <cstdio>
#include <vector>

#include "dsp/Biquad.h"
#include "dsp/GatedLoudness.h"
#include "dsp/TruePeak.h"

using namespace kemuri;

namespace
{
int failures = 0;

void expect (bool c, const char* label)
{
    if (! c) { std::printf ("FAIL: %s\n", label); ++failures; }
}

void expectNear (double got, double want, double tol, const char* label)
{
    if (std::abs (got - want) > tol)
    {
        std::printf ("FAIL: %s  got=%.10f want=%.10f (tol=%g)\n", label, got, want, tol);
        ++failures;
    }
}

std::vector<float> makeSine (double freq, double amp, double sr, int n)
{
    std::vector<float> s (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
        s[static_cast<size_t> (i)] =
            static_cast<float> (amp * std::sin (2.0 * dsp::kPi * freq * i / sr));
    return s;
}
} // namespace

int main()
{
    const juce::File refFile (juce::String (KEMURI_REFERENCE_DIR) + "/stream_measurement.json");
    expect (refFile.existsAsFile(), "reference json exists");
    if (! refFile.existsAsFile())
        return 1;

    const juce::var root = juce::JSON::parse (refFile);

    // ── biquad 係数 ────────────────────────────────────────────────
    if (const auto* biquads = root["biquads"].getArray())
    {
        for (const auto& bv : *biquads)
        {
            const juce::String type = bv["type"].toString();
            const double freq = (double) bv["freq"];
            const double q    = (double) bv["q"];
            const double gain = (double) bv["gain_db"];
            const double sr   = (double) bv["sample_rate"];

            const auto c = dsp::makeBiquad (dsp::filterTypeFromString (type.toRawUTF8()),
                                            freq, q, gain, sr).toArray();

            const auto* want = bv["coeffs"].getArray();
            expect (want != nullptr && want->size() == 5, "biquad coeffs size");
            if (want != nullptr && want->size() == 5)
            {
                const juce::String tag = bv["group"].toString() + " s" + bv["stage"].toString()
                                       + " @" + juce::String (sr);
                for (int k = 0; k < 5; ++k)
                    expectNear (c[static_cast<size_t> (k)], (double) (*want)[k], 1e-9,
                                ("biquad " + tag).toRawUTF8());
            }
        }
    }
    else expect (false, "biquads array present");

    // ── integrated LUFS（pyloudnorm 一致）─────────────────────────
    if (const auto* cases = root["lufs"].getArray())
    {
        for (const auto& cv : *cases)
        {
            const double freq = (double) cv["freq"];
            const double amp  = (double) cv["amp"];
            const double sr   = (double) cv["sr"];
            const int    n    = (int)    cv["num_samples"];
            const int    ch   = (int)    cv["channels"];
            const double want = (double) cv["expected_lufs"];

            const auto mono = makeSine (freq, amp, sr, n);

            dsp::GatedLoudness meter;
            meter.prepare (sr, ch);

            // 全チャンネル同一信号。ブロック単位で流す（音声スレッド相当）。
            const int blockSize = 512;
            std::vector<const float*> ptrs (static_cast<size_t> (ch));
            for (int off = 0; off < n; off += blockSize)
            {
                const int len = std::min (blockSize, n - off);
                for (int c = 0; c < ch; ++c)
                    ptrs[static_cast<size_t> (c)] = mono.data() + off;
                meter.process (ptrs.data(), ch, len);
            }

            const juce::String tag = "LUFS " + juce::String (freq) + "Hz "
                                   + juce::String (amp) + " @" + juce::String (sr);
            expect (meter.hasIntegrated(), (tag + " has integrated").toRawUTF8());
            expectNear (meter.getIntegratedLufs(), want, 0.1, tag.toRawUTF8());
        }
    }
    else expect (false, "lufs array present");

    // ── True Peak（analyzer.py 4x FFT 一致）───────────────────────
    if (const auto* cases = root["true_peak"].getArray())
    {
        for (const auto& cv : *cases)
        {
            const double freq   = (double) cv["freq"];
            const double amp    = (double) cv["amp"];
            const double sr     = (double) cv["sr"];
            const int    n      = (int)    cv["num_samples"];
            const double want   = (double) cv["expected_tp_db"];
            const double speak  = (double) cv["sample_peak_db"];

            const auto mono = makeSine (freq, amp, sr, n);
            const double tp  = dsp::TruePeakMeter::measureMonoDb (mono.data(), n);

            const juce::String tag = "TP " + juce::String (freq) + "Hz " + juce::String (amp);
            expectNear (tp, want, 0.3, tag.toRawUTF8());
            // True Peak は必ずサンプルピーク以上（僅かな数値誤差を許容）
            expect (tp >= speak - 0.05, (tag + " >= sample peak").toRawUTF8());
        }
    }
    else expect (false, "true_peak array present");

    if (failures == 0)
        std::printf ("StreamTests: all PASS\n");
    else
        std::printf ("StreamTests: %d FAILURE(S)\n", failures);

    return failures == 0 ? 0 : 1;
}
