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
- [x] M4: 学習パターン (patterns.json) + UI 仕上げ（ピアノロールプレビュー）
- [x] v2.0: ネタはオーディオ（サイドチェイン）から解析・ベースはあなたのキックに同期（根本作り直し）
- [x] v2.0.2: Live でサイドチェインが届かない問題を修正（入力 = サイドチェインのみ）、Drum Rack の C3 まとまりを判定
- [x] v2.1: ベースの作り直し — キックに乗せる・ルートだけ・1 小節 4 音まで（音数が多く音程が跳ねていた）
- [x] v2.2: キックと重ならないベース — キックの上で伸ばす・キックの間に入る・和音の変化を食う

## 使い方（Ableton Live 12）

kemuriBass は VST3 **インストゥルメント**として登録される（Ableton はサードパーティ VST3 を
「音源の前の MIDI エフェクト」枠に置けないため。Scaler / Cthulhu 等と同じ）。ベース音源
（例: 自作の `sabu base.adg`）と同じトラックには挿さず、次のいずれかで使う:

1. **ドラッグアウト（推奨・確実）**: kemuriBass を専用トラックに挿す → `Generate` →
   UI の「⇩ Drag MIDI」チップをベース音源トラックの空クリップスロットへドラッグ。
   生成クリップがベーストラックに乗り、そのまま鳴る。
2. **MIDI ルーティング（リアルタイム）**: kemuriBass を別トラックに置き、ベーストラックの
   `MIDI From` をそのトラック＋`kemuriBass`、`Monitor` を `In` にする。ホスト再生中に駆動。

### Analyze（v2: ネタは「音」から、キックは「ドラムの MIDI」から）

うわネタがサンプルのチョップ（Simpler / Drum Rack）の場合、MIDI のノート番号は音程ではなく
パッド番号なので、**ネタはオーディオで受け取って音から解析**する。

**Live の設定（kemuriBass のトラックで 1 回だけ）:**
1. **サイドチェイン = ネタのトラック** — 画面下のデバイスビューに並ぶ kemuriBass デバイスの
   **左側**にサイドチェインの設定が出る（Ableton マニュアル「Using Plug-ins」: サイドチェイン対応
   プラグインはデバイスの左側でサイドチェインのパラメータにアクセスできる）。`Sidechain` をオンにして
   `Audio From` にネタのトラックを選ぶ。再生中に UI 右上が「● ネタ受信中」になれば OK
   （「○ サイドチェイン無効」= オンになっていない / 「○ サイドチェイン無音」= Audio From 未選択 or 停止中）。
   ※ 入力構成が v2.0.2 で変わったので、古いセットの kemuriBass は一度削除して挿し直す
   （UI 下の小さな文字が `v2.2.0` なら新しい版）。
   ※ 元のサンプル（.wav）をドロップする方式はチョップの並び（実際に鳴っている和音の順番）と合わないので
   採用しない。サイドチェインは実際に鳴っているチョップをそのまま聴く。
2. **MIDI From = ドラムのトラック**（Drum Rack）、**下の欄 = `Pre FX`（または `Kick Drum` のチェーン）**
   — ベースをあなたのキックに同期させる。`Post FX` だと Drum Rack の全パッドがチェーンの再生音程
   （既定 C3）の 1 音にまとまって届き、キックを区別できない（UI が「C3 の 1 音に…」と案内する）。
   ネタのトラックのまま（v1 の設定）だとキックが D#3 などパッド範囲外になり、警告して採用しない。
3. 再生しながら `Analyze` → 「ネタ: Key / ループ長 / 和音 / サンプルのベース」「キック: ノート / 打数 / swing」が出る。
4. `Generate` → プレビュー下端の白い印が**あなたのキック**。ベースがそこに乗っているのを確認して `⇩ Drag MIDI`。

**生成（ブームバップ系 = Mix / Premier / Dilla / 9th / Pete Rock）:** 90 年代ヒップホップのベースの基本どおり
- **土台はあなたのキック**。ベースとキックの関係は 4 通り: 一緒に鳴る / キックの上で伸ばしたまま /
  キックとキックの間の裏拍に入る（ネタのアクセントがある所）/ 和音の変化を 8 分・16 分食って先に移る
- **ルートだけ**: 音はその時のネタの和音のルート（食った音は次の和音のルート）
- **1 小節 4 音まで**、音域は E1〜D#2 に固定（音程が跳ねない・高い音が出ない）
- ループは毎回同じ。変化は 4 小節ごとのフレーズの最後だけ（量は `Fill`）。ネタのループ長に合わせて小節数を自動延長
- プロデューサーの違い: Premier = ほぼキックと一緒で短く切る / Pete Rock = レガートでよく食う /
  9th = 1 拍目と和音の変化だけで長く伸ばし 4 小節ごとに音階で歩く / Dilla = キックの間に 16 分で入り後ろノリ
- `Complexity`: 0 = 全部キックの上、30（既定）= プロデューサーなりにキックから外す、上げるほど外す。
  35 を超えると同じ和音の中で下の 5 度・♭7 へも動く

> ドラッグしたクリップは**ソングの小節 1（またはループ長の倍数の位置）**から鳴らすと位相が合う。
> セッションビューならクリップの起動クオンタイズをループ長（2 / 4 Bars）にする。
> 音程のある MIDI のネタは `.mid` を画面へドロップしても解析できる（小節 0 = クリップ先頭）。
> ネタやドラムが無くても動く（未解析は手動 Key/Mode、キック未検出は定番キック, R8）。

### 学習パターン（patterns.json, M4）

`%APPDATA%/KemuriBeat/patterns.json` を置くと、起動時にハードコードのパターンライブラリへ
マージされる（R6）。プロデューサ別のパターン・Markov 遷移・学習グルーヴを追加でき、生成が
実曲寄りになる。スキーマは [docs/patterns.sample.json](docs/patterns.sample.json) 参照。
パース失敗時はハードコードのみで動作し UI に警告バッジを出す（R9、起動は中断しない）。
学習パイプライン（Python）は M4L 版 kemuri-bass-generator から移設予定で、出力先を
この patterns.json にする。

> M4L 版のように「同一トラック内のデバイスでクリップへ直接書き込む」ことは VST3 では不可
> （Live API はプラグインから使えない）。その用途は Max for Live 版 kemuri-bass-generator を継続利用する。

## テスト（G3）

`tools/` の Node スクリプトで JS 正本（`docs/reference/kemuri_generator.js`）から
決定的サブコンポーネントの参照ベクトル `tests/reference/*.json` を抽出し、
C++ 実装が一致することを ctest で検証する。乱数を含む生成全体は分布・不変条件検査。
