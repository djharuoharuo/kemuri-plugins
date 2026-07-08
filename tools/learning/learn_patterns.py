"""WAV → ベースパターン学習 → patterns.json 生成（kemuriBass VST3 用）。

M4L 版（kemuri-bass-generator）からの移設。出力先を kemuri_generator.js への
注入ではなく patterns.json に変更した。プラグインは起動時にこれを読み込む（R6）。

使い方:
    python learn_patterns.py                    # ./training_data → 既定の patterns.json
    python learn_patterns.py <folder>           # 入力フォルダ指定
    python learn_patterns.py --out path.json    # 出力先指定
    python learn_patterns.py --no-cache         # 全曲再処理

既定の出力先: %APPDATA%/KemuriBeat/patterns.json（Windows）
             ~/.config/KemuriBeat/patterns.json（他）

フォルダ構成（サブフォルダは任意）:
    training_data/
        premier/  *.wav      → premier ライブラリ
        dilla/    *.wav       → dilla
        9th/ (or ninth/)      → ninth
        pete/                 → pete
        *.wav                 → pool（Boom-Bap Mix で全体に混ざる）

パイプライン:
    WAV → Demucs (bass stem) → basic-pitch (MIDI) → 1 小節パターン抽出
        → patterns.json 書き出し

初回は Demucs モデル（約 300MB）の自動 DL。basic-pitch は Python 3.11 までを
公式サポート（3.12+ は要確認）。
"""
from __future__ import annotations

import argparse
import json
import os
import statistics
import sys
import tempfile
from pathlib import Path

from bass_separator import separate_bass
from audio_to_midi import audio_to_midi
from pattern_extractor import extract_all
from patterns_json_writer import write_patterns_json

PROJECT_ROOT = Path(__file__).parent.resolve()
CACHE_FILE = PROJECT_ROOT / ".pattern_cache.json"

AUDIO_EXTS = {".wav", ".mp3", ".flac", ".aiff", ".aif", ".m4a"}
PRODUCER_FOLDERS = {
    "premier": "premier",
    "dilla": "dilla",
    "9th": "ninth", "ninth": "ninth",
    "pete": "pete", "peterock": "pete", "pete rock": "pete", "pete_rock": "pete",
}
PRODUCERS = ("premier", "dilla", "ninth", "pete")


def default_output() -> Path:
    appdata = os.environ.get("APPDATA")
    base = Path(appdata) if appdata else (Path.home() / ".config")
    return base / "KemuriBeat" / "patterns.json"


def _load_cache() -> dict:
    if CACHE_FILE.is_file():
        try:
            return json.loads(CACHE_FILE.read_text(encoding="utf-8"))
        except Exception:
            pass
    return {}


def _save_cache(data: dict) -> None:
    CACHE_FILE.parent.mkdir(parents=True, exist_ok=True)
    CACHE_FILE.write_text(json.dumps(data, indent=2, ensure_ascii=False),
                          encoding="utf-8")


def _walk_audio(root: Path) -> list[tuple[Path, str | None]]:
    items: list[tuple[Path, str | None]] = []
    if not root.is_dir():
        return items
    for sub in root.iterdir():
        if sub.is_dir():
            tag = PRODUCER_FOLDERS.get(sub.name.lower())
            for p in sub.rglob("*"):
                if p.is_file() and p.suffix.lower() in AUDIO_EXTS:
                    items.append((p, tag))
    for p in root.iterdir():
        if p.is_file() and p.suffix.lower() in AUDIO_EXTS:
            items.append((p, None))
    return items


def process_one(audio_path: Path, work_dir: Path) -> dict:
    print(f"\n=== {audio_path.name} ===")
    print("  - separating bass stem (demucs)...")
    bass_wav = separate_bass(str(audio_path), output_dir=str(work_dir / "demucs"))
    print("  - converting to MIDI (basic-pitch)...")
    midi_path = audio_to_midi(bass_wav, output_dir=str(work_dir / "midi"))
    print("  - extracting 1-bar patterns + groove...")
    result = extract_all(midi_path, source_tag=audio_path.stem)
    print(f"    -> {len(result['patterns'])} patterns")
    return result


def _add_transitions(trans: dict, sequence: list) -> None:
    for (b0, n0), (b1, n1) in zip(sequence, sequence[1:]):
        if b1 == b0 + 1:
            trans.setdefault(n0, {})
            trans[n0][n1] = trans[n0].get(n1, 0) + 1


def _add_groove(acc: dict, groove: dict) -> None:
    # C++ 側は timing のみ利用（velocity は 127 固定）。timing だけ蓄積する。
    for slot, vals in groove.get("timing", {}).items():
        acc.setdefault(int(slot), []).extend(vals)


def _finalize_timing(acc: dict) -> dict | None:
    """生サンプル → 16 スロット [mean, std]（サンプル 3 未満は null）。"""
    slots = []
    has_any = False
    for s in range(16):
        vals = acc.get(s, [])
        if len(vals) >= 3:
            slots.append([round(statistics.fmean(vals), 4),
                          round(statistics.pstdev(vals), 4)])
            has_any = True
        else:
            slots.append(None)
    return {"timing": slots} if has_any else None


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder", nargs="?", default=str(PROJECT_ROOT / "training_data"),
                    help="WAV フォルダ（既定: ./training_data）")
    ap.add_argument("--out", default=str(default_output()),
                    help="出力 patterns.json のパス")
    ap.add_argument("--no-cache", action="store_true", help="全曲再処理")
    args = ap.parse_args()

    folder = Path(args.folder).resolve()
    if not folder.is_dir():
        print(f"ERROR: folder not found: {folder}", file=sys.stderr)
        return 1

    audio_items = _walk_audio(folder)
    if not audio_items:
        print(f"No audio files found under {folder}")
        print("Expected: training_data/<producer>/*.wav  or  training_data/*.wav")
        return 1

    print(f"Found {len(audio_items)} audio files under {folder}")
    cache = {} if args.no_cache else _load_cache()

    all_patterns: list[dict] = []
    by_producer: dict[str, list[dict]] = {t: [] for t in PRODUCERS}
    trans_pool: dict = {}
    trans_by_producer: dict[str, dict] = {t: {} for t in PRODUCERS}
    groove_pool: dict = {}
    groove_by_producer: dict[str, dict] = {t: {} for t in PRODUCERS}

    with tempfile.TemporaryDirectory(prefix="kemuri_learn_") as tmp_root:
        work_dir = Path(tmp_root)
        for audio_path, tag in audio_items:
            key = str(audio_path.resolve())
            mtime = os.path.getmtime(audio_path)
            cached = cache.get(key)
            if cached and cached.get("mtime") == mtime and "groove" in cached:
                result = cached["result"]
                print(f"\n=== {audio_path.name} (cached: {len(result['patterns'])} patterns) ===")
            else:
                try:
                    result = process_one(audio_path, work_dir)
                except Exception as e:
                    print(f"  !! failed: {e}", file=sys.stderr)
                    continue
                cache[key] = {"mtime": mtime, "tag": tag, "groove": True, "result": result}

            pats = result["patterns"]
            all_patterns.extend(pats)
            if tag and tag in by_producer:
                by_producer[tag].extend(pats)
                _add_transitions(trans_by_producer[tag], result["sequence"])
                _add_groove(groove_by_producer[tag], result["groove"])
            _add_transitions(trans_pool, result["sequence"])
            _add_groove(groove_pool, result["groove"])

    _save_cache(cache)
    print(f"\nTotal: {len(all_patterns)} learned patterns")

    if not all_patterns:
        print("No patterns extracted; nothing to write.")
        return 0

    transitions = {"pool": trans_pool, **trans_by_producer}
    groove = {"pool": _finalize_timing(groove_pool),
              **{t: _finalize_timing(groove_by_producer[t]) for t in PRODUCERS}}

    write_patterns_json(args.out, all_patterns, by_producer, transitions, groove)
    print("\nDone. プラグイン（kemuriBass）を再読み込みすると学習パターンが反映されます。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
