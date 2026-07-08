"""学習結果を patterns.json (kemuriBass VST3 の R6 スキーマ) として書き出す。

M4L 版は kemuri_generator.js へ直接注入していたが、VST3 版はプラグインが
%APPDATA%/KemuriBeat/patterns.json を起動時に読み込む（PatternsJson.h）。
スキーマは docs/patterns.sample.json と同一:

    { "version": 1,
      "libraries": {
        "premier"|"dilla"|"ninth"|"pete"|"pool": {
          "patterns":   [ { "name","swing","jitter","notes":[{"pos","dur","pitch"}] } ],
          "transitions":{ "from": { "to": weight } },
          "groove":     { "timing": [ [mean,std] | null, ... x16 ] } } } }

- pitch は文字列統一（数値オフセットも "0" / "-5" と書く）。
- groove は timing のみ（velocity は常に 127 固定＝ユーザー決定事項のため出さない）。
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any

PRODUCERS = ("premier", "dilla", "ninth", "pete")


def _pitch_str(p: Any) -> str:
    """pitch を文字列トークンへ。数値はそのまま文字列化、文字列はそのまま。"""
    if isinstance(p, str):
        return p
    return str(int(p))


def _pattern_obj(p: dict[str, Any]) -> dict[str, Any]:
    return {
        "name": p["name"],
        "swing": int(p.get("swing", 0)),
        "jitter": float(p.get("jitter", 0.0)),
        "notes": [
            {"pos": round(float(n["pos"]), 3),
             "dur": round(float(n["dur"]), 3),
             "pitch": _pitch_str(n["pitch"])}
            for n in p["notes"]
        ],
    }


def _library_obj(patterns: list[dict[str, Any]],
                 transitions: dict[str, Any] | None,
                 groove: dict[str, Any] | None) -> dict[str, Any]:
    lib: dict[str, Any] = {"patterns": [_pattern_obj(p) for p in patterns]}
    if transitions:
        # {from: {to: count}} の count を float に統一
        lib["transitions"] = {
            frm: {to: float(w) for to, w in row.items()}
            for frm, row in transitions.items() if row
        }
    if groove and groove.get("timing"):
        # timing のみ（16 要素、各 [mean,std] または null）
        lib["groove"] = {"timing": groove["timing"]}
    return lib


def build_patterns_json(pool: list[dict[str, Any]],
                        by_producer: dict[str, list[dict[str, Any]]],
                        transitions: dict[str, Any],
                        groove: dict[str, Any]) -> dict[str, Any]:
    """集約済みの学習結果を patterns.json の dict へ組み立てる。"""
    libraries: dict[str, Any] = {}
    for tag in PRODUCERS:
        libraries[tag] = _library_obj(
            by_producer.get(tag, []),
            transitions.get(tag),
            groove.get(tag),
        )
    libraries["pool"] = _library_obj(
        pool,
        transitions.get("pool"),
        groove.get("pool"),
    )
    return {"version": 1, "libraries": libraries}


def write_patterns_json(out_path: str | Path,
                        pool: list[dict[str, Any]],
                        by_producer: dict[str, list[dict[str, Any]]],
                        transitions: dict[str, Any],
                        groove: dict[str, Any]) -> None:
    data = build_patterns_json(pool, by_producer, transitions, groove)
    out = Path(out_path)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n",
                   encoding="utf-8")
    total = len(pool) + sum(len(v) for v in by_producer.values())
    print(f"Wrote {total} learned patterns → {out}")
    for tag in PRODUCERS:
        n = len(by_producer.get(tag, []))
        if n:
            print(f"  - {tag}: {n}")
    if pool:
        print(f"  - pool (Boom-Bap Mix): {len(pool)}")


# ── 自己テスト（stdlib のみ・音源不要）─────────────────────────────
def _selftest() -> int:
    pool = [{"name": "LRN_pool_a",
             "notes": [{"pos": 0, "dur": 0.5, "pitch": 0},
                       {"pos": 2, "dur": 0.5, "pitch": "octave"}]}]
    by_producer = {
        "premier": [{"name": "LRN_prm_a", "swing": 0, "jitter": 0.0,
                     "notes": [{"pos": 0, "dur": 0.75, "pitch": 0},
                               {"pos": 2.5, "dur": 0.4, "pitch": -5}]}],
    }
    transitions = {"premier": {"LRN_prm_a": {"PRM_mass_appeal": 3}}, "pool": {}}
    timing = [[0.0, 0.01], None] + [None] * 14
    groove = {"premier": {"timing": timing}, "pool": None}

    data = build_patterns_json(pool, by_producer, transitions, groove)

    assert data["version"] == 1
    libs = data["libraries"]
    assert set(libs.keys()) == {"premier", "dilla", "ninth", "pete", "pool"}
    assert libs["premier"]["patterns"][0]["name"] == "LRN_prm_a"
    # pitch は必ず文字列
    for lib in libs.values():
        for pat in lib["patterns"]:
            for n in pat["notes"]:
                assert isinstance(n["pitch"], str), n
    assert libs["premier"]["patterns"][0]["notes"][1]["pitch"] == "-5"
    assert libs["pool"]["patterns"][0]["notes"][1]["pitch"] == "octave"
    assert libs["premier"]["transitions"]["LRN_prm_a"]["PRM_mass_appeal"] == 3.0
    assert libs["premier"]["groove"]["timing"][0] == [0.0, 0.01]
    # 空の producer は patterns 空配列
    assert libs["dilla"]["patterns"] == []
    # JSON 往復
    round_trip = json.loads(json.dumps(data))
    assert round_trip == data
    print("patterns_json_writer selftest: PASS")
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(_selftest())
