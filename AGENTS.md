# kemuri-plugins — JUCE VST3 プラグイン開発モノレポ 仕様書

version 1.1.0 (2026-07-07) — 正本。実装とズレたらコードを直す。仕様変更は version bump + changelog。

## 目的

1. KemuriBeat Bass Generator (Max for Live 版: [kemuri-bass-generator](https://github.com/djharuoharuo/kemuri-bass-generator)) を DAW 非依存の **VST3 プラグイン「KemuriBass」** として移植する
2. 将来のプラグインを同じ基盤 (`common/`) で量産できるモノレポを整備する

## 非目的

- M4L 版の廃止・機能変更（M4L 版は別リポジトリで存続）
- macOS / AU 対応（M5 まで着手しない。Windows VST3 が先）
- オーディオ信号からのうわネタ解析（初期は MIDI 入力解析のみ。オーディオは M5 以降）
- インストーラ・コード署名・ストア配布
- 学習パイプラインの C++ 化（Python のまま。`patterns.json` 経由で連携）

## 前提・制約

- Windows 11 x64 / Visual Studio 2022 Build Tools (MSVC) / CMake >= 3.22 / **JUCE 8** (git submodule) / C++20
- プラグインフォーマットは VST3 のみ（JUCE の設定で後から AU 追加可能な構成にしておく）
- アルゴリズムの正本: `docs/reference/kemuri_generator.js`（M4L 版スナップショット）。移植は `docs/PORTING.md` の対応表に従う
- 秘匿情報: なし（API キー不使用）。リポジトリに入れない物: 学習用音源（*.wav 等）、ビルド成果物、DAW プロジェクト

## 要件 (EARS)

### 正常系

- **R1**: システムは VST3 (Windows x64) としてビルドされ、pluginval strictness 10 を PASS する
- **R2**: Generate 実行時、システムは Style(8種) / Complexity / Fill / Bars(4·8·16) / Key / Mode に基づき、パターン選択 → Markov 遷移 → 変奏 → フレーズ展開でベースラインを生成する
- **R2.1** (v1.1, JS からの意図的変更): Boom-Bap 系スタイルは 1 ループぶんの「型」を生成して繰り返し、フレーズ端（4=bar3 / 8=bar7 / 16=bar11·15）でのみ展開する（ヒップホップのループ構造）。ループ長 L は解析済みなら検出ループ、無ければ既定 2 小節。Soul-Jazz（歩くベース）はロックせず毎小節生成する。決定的サブコンポーネントの G3 参照ベクトル一致は維持（buildNotes の構造のみ変更）
- **R3**: ユーザーが UI のドラッグ領域を DAW へドラッグしたとき、システムは生成結果を SMF format 0 (PPQ 480) の一時 .mid ファイルとして渡す
- **R4**: ホスト再生中、システムは生成済みループをホストの拍位置に同期して processBlock から MIDI 出力する
- **R5**: MIDI 入力ノートを常時リングバッファ（直近 64 小節）に記録し、Analyze 実行時にキー検出 (Krumhansl-Schmuckler)・1小節/2拍コード進行・16分オンセットヒストグラム（コール&レスポンス用）を計算する
- **R6**: `%APPDATA%/KemuriBeat/patterns.json` が存在する場合、システムは起動時に読み込みハードコードライブラリへマージする（スキーマは `tools/learning/` の出力と同一）
- **R7**: 全生成パラメータを AudioProcessorValueTreeState で公開し、DAW からオートメーション可能とする

### 異常系

- **R8**: もし MIDI 入力が空のまま Analyze されたら、システムは「入力なし」を UI に表示し、手動 Key/Mode 設定を維持する（例外を投げない）
- **R9**: もし patterns.json のパースに失敗したら、システムはハードコードのみで動作し UI に警告バッジを表示する（起動を中断しない）
- **R10**: もし生成結果が 0 ノートなら、システムはルート全音符 × Bars のフォールバックを出力する
- **R11**: processBlock 内ではヒープ確保・ファイル I/O・ロック取得を行わない。生成はメッセージスレッドで実行し、完成シーケンスをアトミック swap で渡す
- **R12**: もしホストが PlayHead を提供しなければ、システムは内部 120 BPM で動作する

## アーキテクチャ

```
kemuri-plugins/
  CMakeLists.txt            # ルート: JUCE + 全プラグインのスーパービルド
  libs/JUCE/                # git submodule (JUCE 8 リリースタグ固定)
  common/
    kemuri_core/            # JUCE 非依存・ヘッダ中心: 音楽理論 / PatternEngine /
                            #   Markov / Groove(swing・jitter) / KeyDetect / ChordDetect
    kemuri_ui/              # 共有 LookAndFeel (ダークテーマ)・共通ウィジェット
  plugins/KemuriBass/       # 初号機: Processor / Editor / リソース
  tests/                    # ctest: kemuri_core 単体テスト + JS 参照ベクトル一致テスト
  tools/learning/           # Python 学習パイプライン (M4L リポジトリから移設し
                            #   出力先を patterns.json に変更)
  docs/reference/           # kemuri_generator.js スナップショット
  docs/PORTING.md           # JS → C++ 対応表 (移植の作業指示書)
```

- パターンライブラリは JSON リソース（BinaryData 埋め込み）とし、ハードコード / 学習パターンを同一スキーマ・同一パーサで扱う
- UI 方針: ピアノロール式の生成プレビュー、ドラッグアウトチップ、Style セレクタ、Analyze 状態表示。配色・タイポグラフィは `kemuri_ui` の LookAndFeel に集約し全プラグインで共有

## 検証ゲート

- **G1** (毎コミット): `cmake --build build` 成功 + `ctest` 全 PASS
- **G2** (各マイルストーン完了時): `pluginval --strictness-level 10` PASS
- **G3** (移植検証): 決定的サブコンポーネント（pitch トークン解決の全組合せ、フレーズ展開フラグ 4/8/16 小節、コード検出・キー検出の固定入力、interaction score）が JS 版から抽出した参照ベクトル (`tests/reference/*.json`) と一致。乱数を含む全体出力は分布検査（生成 100 回でノート数・音域が JS 版の範囲内）
- **G4** (手動): Ableton Live 12 で読込 → Analyze → Generate → ドラッグアウト → MIDI クリップが鳴るまでを確認

## 自律実行の停止条件

- 同一エラーでビルドが 3 回連続失敗 → 停止して報告
- pluginval がクラッシュを検出 → 停止して報告
- JUCE submodule の版上げが必要になった → 独断で更新せず報告
- G3 の参照ベクトルと不一致で、仕様と JS 正本のどちらが正か判断できない → 停止して報告

## マイルストーン

| # | 内容 | 完了条件 |
|---|------|---------|
| M0 | リポジトリ骨格 + CMake + JUCE submodule + 空プラグイン | Live 12 でプラグインが読める |
| M1 | kemuri_core 移植 | G1 + G3 PASS |
| M2 | パラメータ公開 (R7) + ループ MIDI 出力 (R4) + ドラッグアウト (R3) | G2 PASS + Live で MIDI が鳴る |
| M3 | MIDI 入力解析 (R5, R8) | Analyze がキー/進行を正しく表示 |
| M4 | patterns.json (R6, R9) + UI 仕上げ | G4 PASS = 完了の定義 |
| M5 | 未定: macOS/AU・オーディオ解析・他プラグイン | 需要を見て判断（理由: Mac 実機と検証環境が現状ない） |

## 完了の定義

G1〜G4 全 PASS。Ableton Live 12 実機で「Analyze → Generate → ドラッグ&ドロップ → 鳴る」のデモが通ること。

## Changelog

- 1.1.0 (2026-07-07): ループロック生成 (R2.1)。ノート内容ベースのループ検出 (`detectNoteLoopBars`, 1/2/4/8/16 小節) を追加し、Boom-Bap 系はループの型を繰り返してフレーズ端でのみ展開するよう変更（毎小節ランダムを廃止）。うわネタ MIDI のドラッグ&ドロップ解析を追加（VST3 は他トラックを読めないため）。M4L の「毎小節生成」からの意図的な逸脱。決定的サブコンポーネントの G3 は維持
- 1.0.0 (2026-07-06): 初版。M4L 版 (kemuri-bass-generator @ c587b5b) を正本として作成
