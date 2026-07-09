# kemuri-plugins — JUCE VST3 プラグイン開発モノレポ 仕様書

version 1.5.0 (2026-07-09) — 正本。実装とズレたらコードを直す。仕様変更は version bump + changelog。

## 目的

1. KemuriBeat Bass Generator (Max for Live 版: [kemuri-bass-generator](https://github.com/djharuoharuo/kemuri-bass-generator)) を DAW 非依存の **VST3 プラグイン「KemuriBass」** として移植する
2. 将来のプラグインを同じ基盤 (`common/`) で量産できるモノレポを整備する
3. 配信シミュレーター **「KemuriStream」**（M4L 版: [kemuri-stream-checker](https://github.com/djharuoharuo/kemuri-stream-checker)）を 2 号機として追加する — 仕様は [plugins/KemuriStream/AGENTS.md](plugins/KemuriStream/AGENTS.md)（本ファイルの前提・停止条件を継承）

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
- 秘匿情報: KemuriBass は API キー不使用。KemuriStream は環境変数 `GEMINI_API_KEY` を使う（リポジトリ・バイナリ・ログに絶対入れない — 詳細は plugins/KemuriStream/AGENTS.md）。リポジトリに入れない物: 学習用音源（*.wav 等）、ビルド成果物、DAW プロジェクト

## 要件 (EARS)

### 正常系

- **R1**: システムは VST3 (Windows x64) としてビルドされ、pluginval strictness 10 を PASS する
- **R2**: Generate 実行時、システムは Style(8種) / Complexity / Fill / Bars(4·8·16) / Key / Mode に基づき、パターン選択 → Markov 遷移 → 変奏 → フレーズ展開でベースラインを生成する
- **R2.1** (v1.2, JS からの意図的変更): Boom-Bap 系スタイルは 1 ループぶんの「モチーフ（パターン）」を選んで固定し、繰り返す。音そのものではなくモチーフを固定し、コードが変わる小節では同じモチーフをそのコードで再解決する（リフのコード追従）。同一ハーモニーの繰り返しは完全同一。展開はフレーズ端のみ（4=bar3 / 8=bar7 / 16=bar11·15、fill 加飾は同位置で決定的）。ループ長 L は検出ループが 2 回以上繰り返せる（L×2 ≤ Bars）ときだけ採用し、それ以外は既定 2 小節。Soul-Jazz（歩くベース）はロックせず毎小節生成する
- **R5.1** (v1.2): 解析の既定は キー=Temperley-Kostka-Payne プロファイル / コード進行=テンプレートスコア+Viterbi 平滑化（切替ペナルティ 0.5 log 単位・emission β=4・検出キーのダイアトニック事前分布、遷移は学習しない）/ ループ周期=Dice 類似度（しきい値 0.72、候補 1·2·4·8·16）/ クリップ小節数=最終オンセット基準（サスティン食み出しを無視）。JS 正本互換の旧検出器は G3 参照テスト用に保持する
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
  plugins/KemuriStream/     # 2号機: 配信シミュレーター + AI アドバイザー (仕様は同dir の AGENTS.md)
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

- 1.5.0 (2026-07-09): 2号機 KemuriStream（配信シミュレーター + Gemini AI アドバイザー内蔵 VST3）を追加。仕様は plugins/KemuriStream/AGENTS.md に分離（プラグインごとに仕様書を独立させる方針に変更）。秘匿情報の記述を更新（GEMINI_API_KEY は環境変数のみ）
- 1.4.0 (2026-07-08): 学習パイプライン (Python) を M4L 版から tools/learning/ へ移設。出力先を kemuri_generator.js 注入から patterns.json へ変更（WAV→Demucs→basic-pitch→パターン/遷移/グルーヴ抽出→patterns.json）。groove は timing のみ出力（velocity は 127 固定）。C++ ローダとの相互検証テスト（kemuri_patterns_tests が Python 出力と docs/patterns.sample.json を読めることを確認）
- 1.3.0 (2026-07-08): M4 実装。patterns.json ロード（R6: %APPDATA%/KemuriBeat/patterns.json をハードコードへマージ、プロデューサ別 patterns/transitions/groove）・パース失敗時の警告バッジ（R9）・生成の PatternBank 化（学習 Markov 遷移と学習グルーヴを配線）・ピアノロールプレビュー等の UI 仕上げ。G4（Live 実機デモ）はユーザー確認。ロードは kemuri_patterns_tests で検証
- 1.2.0 (2026-07-07): 解析・生成の設計見直し（リサーチに基づく）。解析: キー検出を Temperley-Kostka-Payne プロファイルへ（K-S 比で高精度、複数研究で一致）、コード検出に Viterbi 平滑化（フラッピング G#m↔Gm の根治、Segmental CRF/HMM 系の実用形）、ループ周期を Dice 類似度に（変奏耐性）、クリップ小節数を最終オンセット基準に（「8小節が9小節」誤検出の根治）(R5.1)。生成: 音の固定→モチーフ固定に変更。パターンを固定しコード変化には再解決で追従、同一ハーモニーの繰り返しは完全同一、L は L×2≤Bars のときだけ検出値を採用（過大検出で毎小節ランダムに退化するバグ修正）(R2.1 改)。旧検出器は G3 用に保持
- 1.1.0 (2026-07-07): ループロック生成 (R2.1)。ノート内容ベースのループ検出 (`detectNoteLoopBars`, 1/2/4/8/16 小節) を追加し、Boom-Bap 系はループの型を繰り返してフレーズ端でのみ展開するよう変更（毎小節ランダムを廃止）。うわネタ MIDI のドラッグ&ドロップ解析を追加（VST3 は他トラックを読めないため）。M4L の「毎小節生成」からの意図的な逸脱。決定的サブコンポーネントの G3 は維持
- 1.0.0 (2026-07-06): 初版。M4L 版 (kemuri-bass-generator @ c587b5b) を正本として作成
