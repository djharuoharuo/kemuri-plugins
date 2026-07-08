# tools/learning — WAV → patterns.json 学習パイプライン

実曲（WAV 等）のベースラインを解析し、kemuriBass VST3 が読み込む
`patterns.json`（R6）を生成する。M4L 版 kemuri-bass-generator からの移設で、
出力先を `kemuri_generator.js` 注入から `patterns.json` へ変更したもの。

## パイプライン

```
WAV → Demucs (bass stem 分離)
    → basic-pitch (audio → MIDI)
    → pattern_extractor.py (1 小節パターン + Markov 遷移 + グルーヴ統計)
    → patterns_json_writer.py (patterns.json 書き出し)
```

抽出されるもの:
- **パターン**: 1 小節ぶんの音型（コード根からの相対オフセットに正規化）
- **Markov 遷移**: 実曲の「パターン A→B」隣接カウント（生成の並びに反映）
- **グルーヴ**: 16 分スロットごとの micro-timing `[mean, std]`（velocity は学習しない＝常に 127）

## セットアップ

```
py -3.11 -m venv .venv && .venv\Scripts\activate   # basic-pitch は 3.11 推奨
pip install -r requirements.txt
```

## 実行

```
python learn_patterns.py                  # ./training_data → 既定の patterns.json
python learn_patterns.py path/to/wavs     # 入力フォルダ指定
python learn_patterns.py --out my.json    # 出力先指定
python learn_patterns.py --no-cache       # キャッシュ無視で全曲再処理
```

既定の出力先: `%APPDATA%/KemuriBeat/patterns.json`（プラグインが起動時に読む場所）。

### フォルダ構成

```
training_data/
    premier/  *.wav     → premier ライブラリ
    dilla/    *.wav      → dilla
    9th/ or ninth/       → ninth
    pete/                → pete
    *.wav                → pool（Boom-Bap Mix で全体に混ざる）
```

学習データ（`training_data/**/*.wav` 等）はリポジトリにコミットしない（.gitignore 済み）。

## 出力スキーマ

`patterns.json` の形式は [../../docs/patterns.sample.json](../../docs/patterns.sample.json) と同一。
C++ 側のローダは `plugins/KemuriBass/Source/PatternsJson.h`、テストは `tests/PatternsTests.cpp`。

## メモ
- `patterns_json_writer.py` は単体自己テスト付き: `python patterns_json_writer.py`（音源不要・stdlib のみ）。
- basic-pitch が 3.13 で入らない場合は 3.11 の venv を使う（CLAUDE の地雷メモ参照）。
