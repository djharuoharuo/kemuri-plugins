"""MIDI → 1-bar pattern extractor.

Reads a bass MIDI (from basic-pitch), detects tempo & bar grid, and produces
a list of 1-bar patterns in the same shape as the hardcoded
PREMIER_PATTERNS / DILLA_PATTERNS / NINTH_PATTERNS in kemuri_generator.js:

    { name: "learned_<source>_<bar>", notes: [
        { pos: 0,   dur: 0.5, pitch: 0 },
        { pos: 1.5, dur: 0.5, pitch: 0 },
        ...
    ] }

`pitch` is encoded as a *chromatic offset from the detected bar root* so the
pattern can be transposed by the generator to any key/chord.
"""
from __future__ import annotations
from typing import Any
import os


# 16th-note quantization grid for positions and durations
QUANT = 0.25
MIN_NOTES_PER_BAR = 2
MAX_NOTES_PER_BAR = 16
MAX_BARS_PER_FILE = 64       # safety cap


def _quantize(x: float, step: float = QUANT) -> float:
    return round(x / step) * step


def _detect_bar_root(notes_in_bar) -> int:
    """Pick the root pitch class for a bar.

    Heuristic: duration-weighted pitch-class histogram, lowest octave bias.
    """
    if not notes_in_bar:
        return 0
    score = [0.0] * 12
    for n in notes_in_bar:
        pc = n["pitch"] % 12
        # Weight lower notes higher (bass roots tend to be the lowest)
        weight = n["duration"] * (1.0 + max(0, (48 - n["pitch"])) * 0.02)
        score[pc] += weight
    return score.index(max(score))


def extract_patterns(midi_path: str, source_tag: str = "") -> list[dict[str, Any]]:
    """Back-compat wrapper: patterns only."""
    return extract_all(midi_path, source_tag)["patterns"]


def extract_all(midi_path: str, source_tag: str = "") -> dict[str, Any]:
    """Extract patterns + bar sequence + groove stats from a MIDI file.

    Returns:
        {
          "patterns": [pattern dicts as before],
          "sequence": [(bar_idx, pattern_name), ...]   # for Markov transitions
          "groove": {
            "timing":   {slot(0-15): [deviation_beats, ...]},
            "velocity": {slot(0-15): [velocity, ...]},
          }
        }
    """
    import pretty_midi

    empty = {"patterns": [], "sequence": [], "groove": {"timing": {}, "velocity": {}}}

    pm = pretty_midi.PrettyMIDI(midi_path)
    if not pm.instruments:
        return empty

    # Gather all notes from all instruments (basic-pitch usually outputs one).
    all_notes = []
    for inst in pm.instruments:
        for n in inst.notes:
            all_notes.append({
                "start": n.start,
                "end": n.end,
                "duration": n.end - n.start,
                "pitch": n.pitch,
                "velocity": n.velocity,
            })
    if not all_notes:
        return empty
    all_notes.sort(key=lambda x: x["start"])

    # Estimate tempo. basic-pitch usually outputs at 120 BPM unless specified;
    # we use pretty_midi's estimator as a sanity check.
    try:
        tempo = float(pm.estimate_tempo())
    except Exception:
        tempo = 120.0
    if tempo <= 0:
        tempo = 120.0
    sec_per_beat = 60.0 / tempo

    # Total length in beats
    end_sec = max(n["end"] for n in all_notes)
    total_beats = end_sec / sec_per_beat
    n_bars = min(MAX_BARS_PER_FILE, int(total_beats // 4))
    if n_bars <= 0:
        return empty

    patterns: list[dict[str, Any]] = []
    sequence: list[tuple[int, str]] = []
    groove_timing:   dict[int, list[float]] = {}
    groove_velocity: dict[int, list[int]]   = {}
    source_id = os.path.splitext(os.path.basename(midi_path))[0]
    if source_tag:
        source_id = f"{source_tag}_{source_id}"

    for bar_idx in range(n_bars):
        bar_start_sec = bar_idx * 4 * sec_per_beat
        bar_end_sec   = (bar_idx + 1) * 4 * sec_per_beat

        in_bar = [n for n in all_notes
                  if n["start"] >= bar_start_sec - 0.05 and n["start"] < bar_end_sec]
        if not (MIN_NOTES_PER_BAR <= len(in_bar) <= MAX_NOTES_PER_BAR):
            continue

        # Detect the bar's root pitch class
        root_pc = _detect_bar_root(in_bar)

        # Build notes relative to root
        pat_notes = []
        for n in in_bar:
            rel_sec = n["start"] - bar_start_sec
            rel_beats = rel_sec / sec_per_beat
            pos_beats = _quantize(rel_beats)
            if pos_beats < 0:
                pos_beats = 0.0
            if pos_beats >= 4.0:
                continue

            # Groove stats: micro-timing deviation from the 1/16 grid and
            # velocity, keyed by 16th-note slot (0-15 within the bar).
            slot = int(pos_beats / QUANT) % 16
            dev = rel_beats - pos_beats
            if abs(dev) <= 0.12:  # discard quantization mis-snaps
                groove_timing.setdefault(slot, []).append(round(dev, 4))
            groove_velocity.setdefault(slot, []).append(int(n["velocity"]))
            dur_beats = _quantize(n["duration"] / sec_per_beat)
            if dur_beats < 0.125:
                dur_beats = 0.125
            if dur_beats > 2.0:
                dur_beats = 2.0

            # Encode pitch as chromatic offset from root, preserving octave
            # bias: keep the bass octave layout intact so patterns stay in
            # range when the generator transposes.
            base_root_midi = 24 + root_pc   # C1 + root pc → "low anchor"
            interval = n["pitch"] - base_root_midi
            # Keep intervals in a sane range: -12..+24
            if interval < -12:
                interval += 12 * ((-12 - interval) // 12 + 1)
            if interval > 24:
                interval -= 12 * ((interval - 24) // 12 + 1)

            pat_notes.append({
                "pos": round(pos_beats, 3),
                "dur": round(dur_beats, 3),
                "pitch": interval,
            })

        if not pat_notes:
            continue
        # Sort and de-dup near-duplicate positions
        pat_notes.sort(key=lambda x: x["pos"])
        dedup = [pat_notes[0]]
        for nt in pat_notes[1:]:
            if abs(nt["pos"] - dedup[-1]["pos"]) < 0.05:
                continue
            dedup.append(nt)

        name = f"learned_{source_id}_b{bar_idx}"
        patterns.append({"name": name, "notes": dedup})
        sequence.append((bar_idx, name))

    return {
        "patterns": patterns,
        "sequence": sequence,
        "groove": {"timing": groove_timing, "velocity": groove_velocity},
    }
