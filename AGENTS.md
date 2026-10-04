# kemuri-plugins — JUCE VST3 プラグイン開発モノレポ 仕様書

version 2.0.0 (2026-10-04) — 正本。実装とズレたらコードを直す。仕様変更は version bump + changelog。

## 目的

1. KemuriBeat Bass Generator (Max for Live 版: [kemuri-bass-generator](https://github.com/djharuoharuo/kemuri-bass-generator)) を DAW 非依存の **VST3 プラグイン「KemuriBass」** として移植する
2. 将来のプラグインを同じ基盤 (`common/`) で量産できるモノレポを整備する
3. 配信シミュレーター **「KemuriStream」**（M4L 版: [kemuri-stream-checker](https://github.com/djharuoharuo/kemuri-stream-checker)）を 2 号機として追加する — 仕様は [plugins/KemuriStream/AGENTS.md](plugins/KemuriStream/AGENTS.md)（本ファイルの前提・停止条件を継承）

## 非目的

- M4L 版の廃止・機能変更（M4L 版は別リポジトリで存続）
- macOS / AU 対応（M5 まで着手しない。Windows VST3 が先）
- （v2.0 で撤回）オーディオ信号からのうわネタ解析 — うわネタはサンプルのチョップで MIDI ノート番号が音程を表さないため、サイドチェインのオーディオ解析を R5 の正規経路にした
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
- **R2**: Generate 実行時、システムは Style(8種) / Complexity / Fill / Bars(4·8·16) / Key / Mode と解析結果（R5）に基づきベースラインを生成する
- **R2.2** (v2.0, ブームバップ系 = Mix/Premier/Dilla/9th/Pete): 90 年代の作り方をモデル化した v2 エンジン（`BassEngineV2.h`）で生成する。リズム = ユーザーのキック（ドラム MIDI。無ければ実曲由来の定番キック）。1 拍目は必ず、ネタの和音が変わる位置にも必ず打点。音高 = その瞬間のネタの和音（2 拍単位）とネタ自身のベース音。プロデューサーの手癖は検証済みリサーチ準拠: Premier = サンプルのベースを verbatim / キックのみ / 短いミュート（Mass Appeal）または Full Clip 型（ルート+低い 5 度+下の ♭7）、Pete Rock = サンプルのベースの動きを 8 分でなぞるレガート、9th = 自前のルート追従・伸ばす・4 小節ごとに 2 音の経過音、Dilla = 1 拍目+シンコペーション・順次進行・長短交互・後ろノリ（毎ループ同じズレ）。1 ループ分の決定を固定してタイルし（完全反復）、変化はフレーズ端のみ。音域 E1..G2（MIDI 28..43）で直前音に最も近いオクターブ。出力小節数はネタ/キックのループ長の倍数へ自動延長（最大 16）。出力の小節 0 はソング小節 ≡ 0 (mod 長さ) に対応する
- **R2.1** (v1.2〜1.6, Soul-Jazz/Funk/Lo-Fi で継続): モチーフ固定のループロック生成（旧エンジン）。Soul-Jazz（歩くベース）はロックせず毎小節生成する
- **R5.1** (v1.2): 解析の既定は キー=Temperley-Kostka-Payne プロファイル / コード進行=テンプレートスコア+Viterbi 平滑化（切替ペナルティ 0.5 log 単位・emission β=4・検出キーのダイアトニック事前分布、遷移は学習しない）/ ループ周期=Dice 類似度（しきい値 0.72、候補 1·2·4·8·16）/ クリップ小節数=最終オンセット基準（サスティン食み出しを無視）。JS 正本互換の旧検出器は G3 参照テスト用に保持する
- **R3**: ユーザーが UI のドラッグ領域を DAW へドラッグしたとき、システムは生成結果を SMF format 0 (PPQ 480) の一時 .mid ファイルとして渡す
- **R4**: ホスト再生中、システムは生成済みループをホストの拍位置に同期して processBlock から MIDI 出力する
- **R5** (v2.0 改): (a) ネタ = サイドチェインのオーディオを常時取り込み（モノラル化→アンチエイリアス→約 11 kHz へ間引き→ロックフリー FIFO、再生中のみ、ソング位置マーカー付き）、Analyze 時に直近 16 小節を 16 分単位の特徴量（クロマ/オンセット/低域 YIN によるネタ自身のベース音）にし、キー（Temperley）・2 拍/1 小節の和音（Viterbi + ベース音のルート証拠）・ループ長（小節ベクトルのコサイン類似, 1/2/4/8/16）を求め、ソング小節 ≡ i (mod L) ごとの多数決で位相をソング基準に揃える（取り込み開始位置に依存しない）。(b) キック = MIDI 入力（ドラムのトラック）を常時記録し、1 拍目率・打点密度・ノート番号からキックのノートを推定、ループ長と小節内の実位置（スイング込み）、ドラム全体のスイング % を求める。(c) 音程のある MIDI のネタは .mid のドロップでも解析できる（小節 0 = クリップ先頭）
- **R6**: `%APPDATA%/KemuriBeat/patterns.json` が存在する場合、システムは起動時に読み込みハードコードライブラリへマージする（スキーマは `tools/learning/` の出力と同一）
- **R7**: 全生成パラメータを AudioProcessorValueTreeState で公開し、DAW からオートメーション可能とする

### 異常系

- **R8**: もしネタの音（サイドチェイン）やドラムの MIDI が無いまま Analyze されたら、システムはそれぞれ「未解析 / 未検出」と接続方法を UI に表示し、手動 Key/Mode と定番キックで動作を続ける（例外を投げない）
- **R13** (v2.0): サイドチェインは出力で上書きする前に読む（Live は未選択時にこのプラグインの前回出力を流すが、出力は常に無音なので無害）。取り込みはロックフリー（R11）
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

- 2.0.0 (2026-10-04): **v2 エンジン（根本作り直し）**。ユーザーのうわネタは「サンプルのチョップを MIDI で叩く」形で、MIDI ノート番号は音程ではなくパッド番号だった — これまでの MIDI 解析は原理的に誤っていた（ネタとベースが合わない最大原因）。加えて実測で 2 つのバグを確認: (1) リアルタイム解析の位相ズレ（取り込み開始がソング 0 小節目以外だと全小節が逆の和音）、(2) ブームバップ系が小節途中の和音変化を無視。ライブラリのパターンも実曲の書き起こしではなかった（例: PRM_full_clip は全部ルート、実タブ譜は Bb/F/Ab）。対応: ネタはサイドチェインのオーディオから解析（`Dsp.h` / `AudioAnalyzer.h`: FFT クロマ・低域 YIN・Viterbi+ベース証拠・ソング位相合わせ）、キックはドラム MIDI から（`DrumAnalyzer.h`）、ブームバップ系は v2 エンジン（`BassEngineV2.h`、R2.2）。Viterbi を音価ノート/オーディオクロマ共通化（`viterbiChordsFromPch`、ルート不在判定を相対しきい値に）。テスト: kemuri_v2_tests（合成オーディオで和音/キー/ベース/ループ/位相/ノイズ耐性、キック/スイング、生成のキック同期/和音一致/反復/音域/密度）。非目的からオーディオ解析を撤回

- 1.6.0 (2026-07-20): KemuriBass 解析精度 + 生成リアリズム（検証済みリサーチ準拠）。
  【解析】(1) 小節グリッド整列: リアルタイム解析窓と .mid ドロップの原点を小節境界に整列（途中再生/アウフタクトで全小節判定がズレるバグ修正）。(2) キー検出を音価重み付きに。(3) コード検出に 7th テンプレート（dom7/m7/maj7 → maj/min にマップ）+ 低音バイアス + ルート不在ペナルティ（C-E-G が Am7 へ吸着する相対調誤りを防止）。(4) スイング推定: 16 分オフビートの遅れの中央値から swing% を検出し生成へ反映（うわネタと同じポケット）。UI に swing% / 推奨 Complexity 表示。
  【生成】実曲タブ譜・本人談の検証で「boom-bap ベースは 2-4 音/小節・E1〜G#2・オクターブ跳躍は稀・ループは verbatim 反復」を確認（Full Clip = 3 音 5 ヒット/ループ跳躍ゼロ等、11 クレーム敵対的検証通過）。(1) 密度: ghost c*0.45→c*0.20（+12 ゴースト廃止・常に低域）、オフビート間引き 0.35-c*0.20、オクターブ跳躍 c*0.06・70% 下方向。パターン選択を密度重み付きに（target=base+span*c、Premier 2+2c/cap5）。(2) 音域: ソフト上限 G#2(44)・短音のみ +2・超過は 1oct 下へ反射。ジャズ系（Funk/SoulJazz）のみ A2(45)。(3) プロデューサ再現: 変異強度をスタイル別に（Premier 0.15≈verbatim / Pete・9th 0.4 / Dilla 0.7 / ジャズ系 1.0）。Boom-Bap Mix は 1 生成 = 1 プロデューサ束（セル毎シャッフル廃止）。クライマックスの 16 分ペンタ 4 連を廃止 → 50% subtractive / 50% 低域 2 音フィル。ターンアラウンドは確率制 0.5+c*0.5。velocity 127 固定は維持
- 1.5.0 (2026-07-09): 2号機 KemuriStream（配信シミュレーター + Gemini AI アドバイザー内蔵 VST3）を追加。仕様は plugins/KemuriStream/AGENTS.md に分離（プラグインごとに仕様書を独立させる方針に変更）。秘匿情報の記述を更新（GEMINI_API_KEY は環境変数のみ）
- 1.4.0 (2026-07-08): 学習パイプライン (Python) を M4L 版から tools/learning/ へ移設。出力先を kemuri_generator.js 注入から patterns.json へ変更（WAV→Demucs→basic-pitch→パターン/遷移/グルーヴ抽出→patterns.json）。groove は timing のみ出力（velocity は 127 固定）。C++ ローダとの相互検証テスト（kemuri_patterns_tests が Python 出力と docs/patterns.sample.json を読めることを確認）
- 1.3.0 (2026-07-08): M4 実装。patterns.json ロード（R6: %APPDATA%/KemuriBeat/patterns.json をハードコードへマージ、プロデューサ別 patterns/transitions/groove）・パース失敗時の警告バッジ（R9）・生成の PatternBank 化（学習 Markov 遷移と学習グルーヴを配線）・ピアノロールプレビュー等の UI 仕上げ。G4（Live 実機デモ）はユーザー確認。ロードは kemuri_patterns_tests で検証
- 1.2.0 (2026-07-07): 解析・生成の設計見直し（リサーチに基づく）。解析: キー検出を Temperley-Kostka-Payne プロファイルへ（K-S 比で高精度、複数研究で一致）、コード検出に Viterbi 平滑化（フラッピング G#m↔Gm の根治、Segmental CRF/HMM 系の実用形）、ループ周期を Dice 類似度に（変奏耐性）、クリップ小節数を最終オンセット基準に（「8小節が9小節」誤検出の根治）(R5.1)。生成: 音の固定→モチーフ固定に変更。パターンを固定しコード変化には再解決で追従、同一ハーモニーの繰り返しは完全同一、L は L×2≤Bars のときだけ検出値を採用（過大検出で毎小節ランダムに退化するバグ修正）(R2.1 改)。旧検出器は G3 用に保持
- 1.1.0 (2026-07-07): ループロック生成 (R2.1)。ノート内容ベースのループ検出 (`detectNoteLoopBars`, 1/2/4/8/16 小節) を追加し、Boom-Bap 系はループの型を繰り返してフレーズ端でのみ展開するよう変更（毎小節ランダムを廃止）。うわネタ MIDI のドラッグ&ドロップ解析を追加（VST3 は他トラックを読めないため）。M4L の「毎小節生成」からの意図的な逸脱。決定的サブコンポーネントの G3 は維持
- 1.0.0 (2026-07-06): 初版。M4L 版 (kemuri-bass-generator @ c587b5b) を正本として作成
