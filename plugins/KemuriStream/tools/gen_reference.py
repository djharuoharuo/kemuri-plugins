"""G3 参照ベクトル生成（KemuriStream）。

C++ 実装（Biquad / GatedLoudness / TruePeak）が一致すべき「正本」の数値を
tests/reference/stream_measurement.json に書き出す。

正本:
  - biquad 係数: RBJ Audio EQ Cookbook（kemuri-stream-checker/codec_eq.py と同一式）
  - integrated LUFS: pyloudnorm.Meter.integrated_loudness（ITU-R BS.1770-4）
  - True Peak: 4x FFT sinc オーバーサンプリング（analyzer._measure_true_peak_db と同一）

依存: numpy, pyloudnorm。
実行例（kemuri-stream-checker の venv を使う）:
  "D:/program files/ProgramData/kemuri-stream-checker/.venv/Scripts/python.exe" \
      plugins/KemuriStream/tools/gen_reference.py
"""

from __future__ import annotations

import json
import math
import os

import numpy as np
import pyloudnorm as pyln


# ---------------------------------------------------------------------------
# biquad（codec_eq.py と同一式・同一順序 [b0/a0, b1/a0, b2/a0, a1/a0, a2/a0]）
# ---------------------------------------------------------------------------
def biquad_coeffs(filter_type, freq, q, gain_db, sample_rate):
    if filter_type == "passthrough" or freq <= 0 or sample_rate <= 0:
        return [1.0, 0.0, 0.0, 0.0, 0.0]

    A = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * math.pi * freq / sample_rate
    cw, sw = math.cos(w0), math.sin(w0)
    alpha = sw / (2.0 * max(q, 1e-6))

    if filter_type == "peaking":
        b0, b1, b2 = 1.0 + alpha * A, -2.0 * cw, 1.0 - alpha * A
        a0, a1, a2 = 1.0 + alpha / A, -2.0 * cw, 1.0 - alpha / A
    elif filter_type == "highshelf":
        s = math.sqrt(A)
        b0 = A * ((A + 1) + (A - 1) * cw + 2 * s * alpha)
        b1 = -2 * A * ((A - 1) + (A + 1) * cw)
        b2 = A * ((A + 1) + (A - 1) * cw - 2 * s * alpha)
        a0 = (A + 1) - (A - 1) * cw + 2 * s * alpha
        a1 = 2 * ((A - 1) - (A + 1) * cw)
        a2 = (A + 1) - (A - 1) * cw - 2 * s * alpha
    elif filter_type == "lowshelf":
        s = math.sqrt(A)
        b0 = A * ((A + 1) - (A - 1) * cw + 2 * s * alpha)
        b1 = 2 * A * ((A - 1) - (A + 1) * cw)
        b2 = A * ((A + 1) - (A - 1) * cw - 2 * s * alpha)
        a0 = (A + 1) + (A - 1) * cw + 2 * s * alpha
        a1 = -2 * ((A - 1) + (A + 1) * cw)
        a2 = (A + 1) + (A - 1) * cw - 2 * s * alpha
    elif filter_type == "highpass":
        b0, b1, b2 = (1 + cw) / 2, -(1 + cw), (1 + cw) / 2
        a0, a1, a2 = 1 + alpha, -2 * cw, 1 - alpha
    elif filter_type == "lowpass":
        b0, b1, b2 = (1 - cw) / 2, 1 - cw, (1 - cw) / 2
        a0, a1, a2 = 1 + alpha, -2 * cw, 1 - alpha
    else:
        raise ValueError(filter_type)

    return [b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0]


# codec_filter.js PRESETS のコーデック着色 EQ（RBJ cookbook = codec_eq.py と一致）
FILTER_SPECS = {
    "spotify": [
        ("peaking", 250.0, 1.0, 0.5),
        ("peaking", 4000.0, 1.0, -0.3),
        ("highshelf", 16000.0, 0.7, -1.5),
    ],
    "youtube": [("highshelf", 18000.0, 0.7, -0.5)],
    "apple_music": [("highshelf", 17000.0, 0.7, -0.8)],
    "soundcloud": [
        ("peaking", 300.0, 1.0, 0.4),
        ("highshelf", 15500.0, 0.7, -2.0),
    ],
}

# KemuriStream の K-weighting 定数（pyloudnorm.Meter デフォルトと一致）
KWEIGHT_SPECS = [
    ("highshelf", 1500.0, 0.7071067811865475, 4.0),
    ("highpass", 38.0, 0.5, 0.0),
]


def gen_biquads():
    out = []
    for sr in (44100.0, 48000.0):
        # コーデック着色 EQ（RBJ）
        for name, specs in FILTER_SPECS.items():
            for i, (ftype, freq, q, gain) in enumerate(specs):
                out.append({
                    "group": name,
                    "stage": i,
                    "type": ftype,
                    "freq": freq,
                    "q": q,
                    "gain_db": gain,
                    "sample_rate": sr,
                    "coeffs": biquad_coeffs(ftype, freq, q, gain, sr),
                })
        # K-weighting: pyloudnorm の実フィルタ係数を正本として抽出し、
        # C++ makeBiquad(同一定数) が 1e-9 で一致することを検証する
        meter = pyln.Meter(int(sr))
        pyln_stages = {
            "high_shelf": KWEIGHT_SPECS[0],
            "high_pass": KWEIGHT_SPECS[1],
        }
        for stage_i, (fname, (ftype, freq, q, gain)) in enumerate(pyln_stages.items()):
            filt = meter._filters[fname]
            b = list(np.atleast_1d(filt.b))
            a = list(np.atleast_1d(filt.a))
            # [b0, b1, b2, a1, a2]（a0 正規化済み）
            coeffs = [float(b[0]), float(b[1]), float(b[2]), float(a[1]), float(a[2])]
            out.append({
                "group": "kweight",
                "stage": stage_i,
                "type": ftype,
                "freq": freq,
                "q": q,
                "gain_db": gain,
                "sample_rate": sr,
                "coeffs": coeffs,
            })
    return out


# ---------------------------------------------------------------------------
# 決定的な正弦波信号（C++ 側と sin(2*pi*f*n/sr) で完全再現）
# ---------------------------------------------------------------------------
def make_sine(freq, amp, sr, n):
    t = np.arange(n, dtype=np.float64)
    return amp * np.sin(2.0 * math.pi * freq * t / sr)


def measure_lufs(signal_2d, sr):
    meter = pyln.Meter(sr)
    return float(meter.integrated_loudness(signal_2d))


def measure_true_peak_db(mono):
    # analyzer._measure_true_peak_db と同一（4x FFT sinc）
    mono = mono.astype(np.float64)
    n = mono.size
    oversample = 4
    spectrum = np.fft.rfft(mono)
    padded = np.zeros(n * oversample // 2 + 1, dtype=complex)
    padded[: spectrum.size] = spectrum
    up = np.fft.irfft(padded, n=n * oversample) * oversample
    peak = float(np.max(np.abs(up)))
    return 20.0 * math.log10(peak) if peak > 0 else -200.0


def gen_lufs():
    cases = [
        {"freq": 1000.0, "amp": 0.5, "sr": 44100, "dur": 10.0, "channels": 2},
        {"freq": 1000.0, "amp": 0.1, "sr": 44100, "dur": 10.0, "channels": 2},
        {"freq": 100.0, "amp": 0.5, "sr": 44100, "dur": 10.0, "channels": 2},
        {"freq": 1000.0, "amp": 0.5, "sr": 48000, "dur": 10.0, "channels": 2},
    ]
    for c in cases:
        n = int(round(c["dur"] * c["sr"]))
        mono = make_sine(c["freq"], c["amp"], c["sr"], n)
        sig = np.column_stack([mono] * c["channels"]) if c["channels"] > 1 else mono
        c["num_samples"] = n
        c["expected_lufs"] = measure_lufs(sig, c["sr"])
    return cases


def gen_true_peak():
    cases = [
        {"freq": 1000.0, "amp": 0.5, "sr": 44100, "dur": 1.0},
        {"freq": 4000.0, "amp": 0.9, "sr": 44100, "dur": 1.0},
        {"freq": 7350.0, "amp": 0.7, "sr": 44100, "dur": 1.0},  # fs/6, ISP を誘発
    ]
    for c in cases:
        n = int(round(c["dur"] * c["sr"]))
        mono = make_sine(c["freq"], c["amp"], c["sr"], n)
        c["num_samples"] = n
        c["expected_tp_db"] = measure_true_peak_db(mono)
        c["sample_peak_db"] = 20.0 * math.log10(float(np.max(np.abs(mono))))
    return cases


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out_dir = os.path.join(here, "..", "..", "..", "tests", "reference")
    out_dir = os.path.normpath(out_dir)
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "stream_measurement.json")

    data = {
        "_source": "plugins/KemuriStream/tools/gen_reference.py",
        "_note": "biquad=RBJ cookbook (codec_eq.py), lufs=pyloudnorm BS.1770-4, tp=4x FFT sinc (analyzer.py)",
        "biquads": gen_biquads(),
        "lufs": gen_lufs(),
        "true_peak": gen_true_peak(),
    }

    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    print("wrote", out_path)
    print("  biquads:", len(data["biquads"]))
    print("  lufs   :", [round(c["expected_lufs"], 3) for c in data["lufs"]])
    print("  tp     :", [round(c["expected_tp_db"], 3) for c in data["true_peak"]])


if __name__ == "__main__":
    main()
