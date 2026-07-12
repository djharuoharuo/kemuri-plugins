# KemuriStream — 配信シミュレーター + AI アドバイザー内蔵 VST3 仕様書

version 1.1.0 (2026-07-09) — 正本。実装とズレたらコードを直す。仕様変更は version bump + changelog。
モノレポ共通の前提（ツールチェーン・ビルド手順・停止条件）はルート [AGENTS.md](../../AGENTS.md) を継承する。

## 目的

**「作っている音（音量・音色）が配信後どう変化するかを、Ableton 内でリアルタイムに聴きながら、AI のアドバイスを受けて調整する」** を 1 つの VST3 で実現する。

1. KemuriStreamSim（M4L 版: [kemuri-stream-checker](https://github.com/djharuoharuo/kemuri-stream-checker)）を DAW 非依存の **VST3 オーディオエフェクト「KemuriStream」** として移植する
2. Gemini API（無料枠）による **AI マスタリングアドバイザーをプラグインに内蔵**する（M4L 版にない新機能）
3. YouTube 相当の **本物の Opus コーデック往復**をリアルタイムで聴けるようにする（M4L 版は EQ 近似のみ）

## 非目的

- M4L 版・Python アプリの廃止（kemuri-stream-checker は別リポジトリで存続。ヌルテスト・オフライン精密解析は Python アプリの役割のまま）
- Opus 以外の本物コーデック内蔵（Vorbis は 100ms 超遅延、AAC はライセンス問題。EQ 近似で表現する）
- macOS / AU 対応（ルート仕様と同じく Windows VST3 先行）
- アドバイスの自動定期実行（無料枠保護。手動ボタンのみ）
- Claude API 対応（Gemini 無料枠のみ。判断根拠は kemuri-stream-checker の CLAUDE.md 参照）

## 前提・制約

- ツールチェーンはルート仕様を継承（CMake / MSVC / JUCE 8 / C++20 / `/utf-8`）
- **libopus**: CMake FetchContent でタグ固定（v1.5.x）。BSD ライセンス・**追加費用なし**
- **Gemini API**: REST `v1beta/models/gemini-2.5-flash:generateContent`、構造化出力（`responseMimeType: application/json` + `responseSchema`）。SDK 不使用（`juce::URL` の HTTPS POST で足りる）
- **秘匿情報**: API キーは環境変数 `GEMINI_API_KEY`（または `GOOGLE_API_KEY`）のみ。リポジトリ・バイナリ・ログ・プラグイン state に**絶対に入れない**
- アルゴリズム正本: [docs/reference/codec_filter.js](docs/reference/codec_filter.js)（M4L 版スナップショット）。PRESETS（着色 EQ 係数・target/tp/boost/boost_cap）、BS.1770 ゲート（絶対 -70 / 相対 -10）、減衰下限 -24dB、RBJ biquad 式はこのファイルの値と一致させる
- UI 言語は日本語（JUCE で日本語フォント表示。Windows 標準の Meiryo / Yu Gothic UI を使用）

## 要件 (EARS)

### 正常系

- **R1**: システムは VST3 オーディオエフェクト (Windows x64) としてビルドされ、pluginval strictness 10 を PASS する
- **R2**: システムは入力を K-weighting した 400ms ブロック平均パワー（100ms ホップ・チャンネル和 G=1.0）から、BS.1770 二段ゲート（絶対 -70 LUFS → 相対 -10 LU）で integrated LUFS を算出し、momentary LUFS とともに表示する
- **R2.1** (v1.1, JS からの意図的変更): K-weighting 係数は **pyloudnorm.Meter デフォルト**（high-shelf 1500Hz/+4.0dB/Q=1/√2、high-pass 38Hz/Q=0.5）を使う。M4L 版 codec_filter.js は教科書 BS.1770 値（1681.97/38.13）だったが、kemuri-stream-checker の Python 側（analyzer.py・-14 LUFS 較正ファイル・test_normalize.py）は全て pyloudnorm デフォルトを真値とするため、これに一致させメーター精度を M4L より向上させる。式は RBJ cookbook（pyloudnorm と同一）。G3 で係数一致（1e-9）と integrated LUFS 一致（±0.1 LU）を検証済み
- **R3**: システムは True Peak を 4x オーバーサンプリングで測定し dBTP 表示する（M4L 版のサンプルピークからの改善）。PLR（TP − integrated）も表示する
- **R4**: プラットフォーム選択時（Off / Spotify / YouTube / Apple Music / TIDAL / SoundCloud）、システムは正本 PRESETS と同一の着色 EQ とポリシー（target / tp / boost / boost_cap）を適用する
- **R5**: AutoLoud ON の間、システムは correction = target − integrated（減衰下限 −24dB、ブーストは Spotify のみ上限 +3dB）を 2 秒ランプで適用し、ブースト適用中のみ TP シーリングのリミッターを有効化する
- **R6**: Advice ボタン押下時、システムは測定スナップショット（integrated / momentary / TP / PLR / 補正 dB / 3 帯域エネルギー比率（低 <250Hz・中 250–4000Hz・高 >4000Hz、Python 版 reference.py と同じ境界）/ プラットフォーム情報）を JSON で Gemini に送信し、構造化出力（explanation_ja / eq_suggestions[freq, gain_db, q, reason_ja] / verdict_ja）をプラグイン内に日本語表示する
- **R7**: RealCodec ON かつ YouTube 選択中、システムは libopus 128kbps エンコード→デコード往復を音声パスに挿入し、遅延を `setLatencySamples` でホストへ報告する（Opus は 48kHz 動作。ホストが他レートのときは往復リサンプリング）
- **R8**: Bypass ON のとき、システムは原音を出力する（全処理スルー）
- **R9**: Reset 押下時、システムは測定履歴をクリアする
- **R10**: システムは platform / bypass / autoloud / realcodec を AudioProcessorValueTreeState で公開し、DAW からオートメーション可能とする
- **R11** (優先度低・M5): 質問テキスト欄に入力がある場合、システムは測定 JSON とともに質問文を送信し、回答に反映させる

### 異常系

- **R12**: processBlock 内ではヒープ確保・ロック取得・ファイル I/O・ネットワークを行わない。測定値は atomic 渡し、Gemini 通信は専用バックグラウンドスレッドで行う
- **R13**: もし API キーが未設定なら、Advice 押下時に取得手順（aistudio.google.com/apikey → `setx GEMINI_API_KEY` → DAW 再起動）を UI 表示し、音声処理は継続する
- **R14**: もし Gemini API がエラー（401/403/429/ネットワーク/スキーマ不一致）を返したら、システムは日本語メッセージを UI 表示し、音声処理に影響させない
- **R15**: 入力が絶対ゲート（-70 LUFS）未満の間、システムは integrated と補正ゲインを凍結する（M4L で踏んだ「無音→爆音」の再発防止）
- **R16**: もし libopus の初期化に失敗したら、システムは EQ 近似のみで動作し警告バッジを表示する（クラッシュしない）
- **R17**: サンプルレート変更時（prepareToPlay）、システムは全フィルタ係数・測定窓・リサンプラを再計算する（M4L 版の 44.1kHz 固定を直す）

## アーキテクチャ

```
plugins/KemuriStream/
  CMakeLists.txt            # juce_add_plugin(FX) + libopus FetchContent
  Source/
    PluginProcessor.{h,cpp} # APVTS / processBlock（測定→EQ→ゲイン→リミッター→Opus→bypass selector）
    PluginEditor.{h,cpp}    # kemuri_ui LookAndFeel / タブ / メーター / アドバイス表示
  Source/dsp/
    KWeighting.h            # K-weighting biquad カスケード
    GatedLoudness.h         # 400ms ブロック + 二段ゲート積分（正本の移植）
    TruePeak.h              # 4x オーバーサンプリング TP
    CodecColorEQ.h          # PRESETS 着色 EQ（RBJ）
    PolicyGain.h            # 正規化ポリシー + 2秒ランプ + ブースト時リミッター
    OpusRoundtrip.h         # libopus 往復 + リサンプラ + レイテンシ報告
  Source/advisor/
    GeminiClient.{h,cpp}    # juce::URL POST / responseSchema / エラー分類（バックグラウンドスレッド）
    Snapshot.h              # 測定スナップショット → JSON
  docs/reference/codec_filter.js   # アルゴリズム正本（M4L スナップショット）
```

- モノレポ共通資産を使う: `kemuri_ui`（LookAndFeel）、ルート CMake スーパービルド、`tests/` の ctest 基盤
- 参照ベクトル（G3）は kemuri-stream-checker の Python 実装（pyloudnorm / codec_eq.py / test_normalize.py）から生成し `tests/reference/stream_*.json` に静的コミットする（出所をファイル内に記録）

## 検証ゲート

- **G1** (毎コミット): `cmake --build build` 成功 + `ctest` 全 PASS
- **G2** (各マイルストーン): `pluginval --strictness-level 10` PASS
- **G3** (移植検証):
  - biquad 係数（全プラットフォームの着色 EQ + K-weighting）が Python 版 `codec_eq.py` の出力と一致（誤差 1e-9）
  - -14 LUFS ピンクノイズ（`make_test_tone.py` 生成 WAV）の integrated LUFS が pyloudnorm 値 ±0.1 LU
  - 正規化ポリシー判定（ラウド曲減衰 / 静音曲ブースト上限 / SoundCloud 不介入）が Python 版 `test_normalize.py` の全ケースと一致
  - True Peak: 既知のインターサンプルピーク信号で Python 版（4x FFT OS）と ±0.3 dB
- **G4** (手動・完了の定義): Ableton Live 12 のマスターに挿し「再生 → メーター動作 → Spotify 選択 → AutoLoud で配信後の音 → Advice ボタンで日本語アドバイス表示」が通る

## 自律実行の停止条件

ルート仕様の停止条件に加えて:

- Gemini API の無料枠仕様変更で構造化出力が使えなくなった → 独断で有料化せず報告
- API キーがログ・state・リポジトリに書き込まれるコードパスを検出した → 即修正、修正不能なら報告

## マイルストーン

| # | 内容 | 完了条件 |
|---|------|---------|
| M0 | plugins/KemuriStream 骨格（パススルー FX）+ ルート CMake 登録 | Live 12 でマスターに挿せる |
| M1 | 測定エンジン（R2, R3, R15, R17） | G1 + G3 PASS |
| M2 | ポリシー + 着色EQ + AutoLoud + リミッター + Bypass/Reset + UI（R4, R5, R8, R9, R10） | M4L 版と機能同等、G2 PASS |
| M3 | AI アドバイザー（R6, R13, R14） | 実機で日本語アドバイスが返る |
| M4 | 本物 Opus 往復（R7, R16） | 報告レイテンシが実測遅延と一致（±1 サンプル、ctest で検証）+ Live 12 で bypass A/B しても他トラックとタイミングずれがない（手動） |
| M5 | 質問テキスト欄（R11）+ UI 仕上げ | G4 PASS = 完了の定義 |

## 完了の定義

G1〜G4 全 PASS。Live 12 実機で「マスターに挿す → 再生 → Spotify 選択 → AutoLoud で配信後の音を聴く → Advice ボタンで日本語アドバイスを読む → 調整して再測定」のループが一度も DAW を離れずに回ること。

## Changelog

- 1.1.0 (2026-07-09): M0（パススルー FX 骨格、pluginval L10 PASS）+ M1（測定エンジン）実装。
  Biquad / KWeighting / GatedLoudness / TruePeak を JUCE 非依存ヘッダで実装し、
  Processor に配線（integrated/momentary/TP/PLR をメーター表示、Reset、無音凍結 R15、
  サンプルレート再計算 R17、音声スレッド禁則 R12）。K-weighting を pyloudnorm デフォルト
  係数に一致させる意図的変更を R2.1 として明記。G3 参照テスト（tests/StreamTests.cpp +
  tools/gen_reference.py）で biquad 係数 1e-9・integrated LUFS ±0.1LU・True Peak ±0.3dB
  の Python 正本一致を検証。ctest 4/4 PASS。M1 の音声はパススルー（処理チェーンは M2）
- 1.0.0 (2026-07-09): 初版。M4L 版 KemuriStreamSim (kemuri-stream-checker) を正本として作成。AI アドバイザー内蔵（Gemini 無料枠）と本物 Opus 往復を新規要件として追加
