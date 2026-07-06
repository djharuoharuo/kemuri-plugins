# kemuri-plugins

JUCE ベースの VST3 プラグイン開発モノレポ。

第1弾: **KemuriBass** — Boom-Bap / Soul-Jazz ベースライン・ジェネレーター
（[kemuri-bass-generator](https://github.com/djharuoharuo/kemuri-bass-generator) の Max for Live 版からの移植）。

- 仕様書（正本）: [AGENTS.md](AGENTS.md)
- 移植対応表: [docs/PORTING.md](docs/PORTING.md)
- アルゴリズム参照実装: [docs/reference/kemuri_generator.js](docs/reference/kemuri_generator.js)

## ビルド

```
git clone --recurse-submodules https://github.com/djharuoharuo/kemuri-plugins.git
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

成果物: `build/plugins/KemuriBass/KemuriBass_artefacts/Release/VST3/kemuriBass.vst3`

## Status

- [x] M0: リポジトリ骨格 + 空プラグイン（Live 12 読込・pluginval strictness 10 PASS）
- [x] M1: kemuri_core 移植（G1 + G3 PASS）
- [x] M2: パラメータ公開 + ループ MIDI 出力 + ドラッグアウト（G2 PASS）
- [x] M3: MIDI 入力解析（Analyze でキー/進行/ループ検出、progression 追従）
- [ ] M4: 学習パターン + UI

## 使い方（Ableton Live 12）

kemuriBass は VST3 **インストゥルメント**として登録される（Ableton はサードパーティ VST3 を
「音源の前の MIDI エフェクト」枠に置けないため。Scaler / Cthulhu 等と同じ）。ベース音源
（例: 自作の `sabu base.adg`）と同じトラックには挿さず、次のいずれかで使う:

1. **ドラッグアウト（推奨・確実）**: kemuriBass を専用トラックに挿す → `Generate` →
   UI の「⇩ Drag MIDI」チップをベース音源トラックの空クリップスロットへドラッグ。
   生成クリップがベーストラックに乗り、そのまま鳴る。
2. **MIDI ルーティング（リアルタイム）**: kemuriBass を別トラックに置き、ベーストラックの
   `MIDI From` をそのトラック＋`kemuriBass`、`Monitor` を `In` にする。ホスト再生中に駆動。

### Analyze（うわネタ解析, M3）

うわネタ（コード/メロディ）を解析すると、キー（Krumhansl-Schmuckler）・コード進行（1 小節 / 2 拍）・
ループ長・16 分オンセット密度を検出し、以後の `Generate` がその進行に追従してコール&レスポンスで
隙間を埋める。入力手段は 2 通り:

1. **MIDI ドロップ（推奨・ルーティング不要）**: うわネタの `.mid` ファイル（または Ableton の
   MIDI クリップ）を kemuriBass の画面へドラッグ&ドロップ → 自動で解析。
2. **MIDI ルーティング**: 別トラックから kemuriBass に MIDI を入力し、ホスト再生しながら `Analyze`。
   直近 64 小節を解析する。

> VST3 はホストの他トラック/クリップを直接読めない（Live API はプラグインから使えない）ため、
> M4L 版のような「ソーストラックを選んで読む」方式は不可。上記のドロップ or 入力で渡す。
> 入力が無ければ「入力なし」を表示し、手動 Key/Mode を維持する（R8）。

> M4L 版のように「同一トラック内のデバイスでクリップへ直接書き込む」ことは VST3 では不可
> （Live API はプラグインから使えない）。その用途は Max for Live 版 kemuri-bass-generator を継続利用する。

## テスト（G3）

`tools/` の Node スクリプトで JS 正本（`docs/reference/kemuri_generator.js`）から
決定的サブコンポーネントの参照ベクトル `tests/reference/*.json` を抽出し、
C++ 実装が一致することを ctest で検証する。乱数を含む生成全体は分布・不変条件検査。
