# PORTING.md — kemuri_generator.js → C++ (kemuri_core) 対応表

正本: `docs/reference/kemuri_generator.js`（kemuri-bass-generator @ c587b5b のスナップショット）。
移植中に JS 側の意図が不明瞭な場合はこの表と AGENTS.md を優先し、それでも不明なら停止して報告。

## モジュール対応

| JS (kemuri_generator.js) | C++ (common/kemuri_core) | 備考 |
|---|---|---|
| `STYLES` 配列 | `enum class Style` + 表示名テーブル | 8種: BoomBapMix, Premier, Dilla, Ninth, Pete, SoulJazz, Funk, LoFi |
| `PREMIER_PATTERNS` 等 4 ライブラリ | `patterns/*.json` (BinaryData 埋め込み) → `PatternLibrary` | JSON スキーマは学習出力と共通化 |
| pitch トークン (`"3rd"`, `"low5"`, `"approach-1"` 等) | `PitchToken` (variant: int / enum) + `resolvePitch(token, ctx, nextCtx)` | 全トークン: 数値, 3rd, 4th, 5th, 6th, b6, b7, low5, octave, p2-p5, approach±1, approach-2 |
| `makeKeyContext(rootPc, quality)` | `KeyContext` struct | scale/chord/penta/lowAnchor/midAnchor |
| `clampBass` / `snapNear` | `Range::clampBass` / `snapNear` | BASS_MIN=28, BASS_MAX=47 |
| `_pickPattern` + `_sampleOne` | `MarkovSelector` | 70% 遷移追従 / 30% ランダム。クリップ毎に状態リセット |
| `_interactionScore` + `g_onsetHist` | `InteractionScorer` | 表拍 (slot%4==0): 0.6+0.4*h, 裏: 1.0-h |
| `_applyVariations` | `VariationEngine` | drop/octave-swap/ghost/turnaround/fill/climax。swing→jitter→learned groove の優先順は JS と同一（learned が最優先） |
| swing (`pat.swing`, 54-58%) | `GrooveEngine::applySwing` | 16分ウラ (pos%0.5==0.25) を (swing-50)/100*0.5 遅らせる |
| jitter (`pat.jitter`) | `GrooveEngine::applyJitter` | ±jitter の一様乱数 |
| `USER_GROOVE*` (timing [mean,std]×16) | `GrooveEngine::applyLearned` | ガウスサンプル、±0.12 clamp。**velocity は常に 127 固定**（学習 velocity は適用しない — ユーザー決定事項） |
| `buildNotes` のフレーズ構造 | `PhrasePlanner` | 展開はループ後半のみ: 4=bar3 turn / 8=bar7 climax のみ / 16=bar11 mid + bar15 climax |
| `genSoulJazz` / `genFunk` / `genLoFi` | `generators/WalkingBass.h` 等 | Soul-Jazz のみ half-bar 和声 (`useHalfBar`) |
| `_finishMidiAnalysis` (K-S キー検出) | `KeyDetector` | KS_MAJOR/KS_MINOR 定数はそのまま |
| `_detectProgression` (1小節/2拍) | `ChordDetector` | CHORD_TMPL_MAJ/MIN 重み付きテンプレート、空セグメントは前和音継承 |
| `_detectLoopBars` | `LoopDetector` | 周期候補 {4,8,16} |
| Live API クリップ書込 (`writeToClip`) | **廃止** → `MidiExporter` (SMF format 0, PPQ 480) + リアルタイム再生 | VST にはクリップ書込 API が無い。ドラッグ&ドロップ + ループ MIDI 出力で代替 |
| Live API クリップ読取 (`analyzeSource`) | **廃止** → MIDI 入力リングバッファ (64小節) | うわネタはトラックの MIDI ルーティングで受ける |
| OSC 受信 (`set_analysis`, port 8001) | **廃止**（M5 で必要なら再検討） | Python UI 連携は VST 版では非目的 |

## 乱数の扱い

JS は `Math.random()` 直呼びでシード不可。C++ では `std::mt19937` を注入可能にし、
テストは固定シードで決定的に実行する。JS との完全一致は求めない（AGENTS.md G3 参照:
決定的部分は参照ベクトル一致、確率的部分は分布検査）。

## 参照ベクトルの作り方

`pip install dukpy` で JS の純粋関数（resolvePitch 相当、KS 相関、コードテンプレートスコア）を
Python から実行し、`tests/reference/*.json` に書き出す。dukpy が JS 全体を食えない場合は
該当関数だけ切り出して評価する。

## patterns.json スキーマ（ハードコード・学習共通）

```json
{
  "version": 1,
  "libraries": {
    "premier": {
      "patterns": [
        { "name": "PRM_mass_appeal",
          "swing": 0, "jitter": 0,
          "notes": [ { "pos": 0, "dur": 0.75, "pitch": "0" } ] }
      ],
      "transitions": { "PRM_a": { "PRM_b": 3 } },
      "groove": { "timing": [[0.01, 0.02], null, "...x16"] }
    },
    "dilla": {}, "ninth": {}, "pete": {}, "pool": {}
  }
}
```

`pitch` は文字列統一（数値も "0" / "7" と書く）。パーサは 1 つ。
`tools/learning/`（Python）はこのスキーマで `%APPDATA%/KemuriBeat/patterns.json` を出力する。
