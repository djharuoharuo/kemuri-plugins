# kemuri-plugins

JUCE ベースの VST3 プラグイン開発モノレポ。

第1弾: **KemuriBass** — Boom-Bap / Soul-Jazz ベースライン・ジェネレーター
（[kemuri-bass-generator](https://github.com/djharuoharuo/kemuri-bass-generator) の Max for Live 版からの移植）。

- 仕様書（正本）: [AGENTS.md](AGENTS.md)
- 移植対応表: [docs/PORTING.md](docs/PORTING.md)
- アルゴリズム参照実装: [docs/reference/kemuri_generator.js](docs/reference/kemuri_generator.js)

## ビルド（予定）

```
git clone --recurse-submodules https://github.com/djharuoharuo/kemuri-plugins.git
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

## Status

- [ ] M0: リポジトリ骨格 + 空プラグイン
- [ ] M1: kemuri_core 移植
- [ ] M2: MIDI 出力 + ドラッグアウト
- [ ] M3: MIDI 入力解析
- [ ] M4: 学習パターン + UI
