"""Demucs wrapper — extracts the isolated bass stem from a mixed audio file.

Uses the `htdemucs` model (the 4-stem default). For our use case we just
need the bass stem, so we keep the bass and discard the rest.

The first run downloads ~300MB of model weights (cached under
`~/.cache/torch/hub/checkpoints/`).
"""
import os
import subprocess
import sys
import shutil
import tempfile
from pathlib import Path


def _which_python() -> str:
    """Return the Python executable to invoke for subprocess calls."""
    return sys.executable


def separate_bass(input_path: str, output_dir: str | None = None) -> str:
    """Run Demucs on `input_path` and return the path to the extracted bass stem.

    Demucs CLI writes to `<output_dir>/htdemucs/<stem_name>/bass.wav`. We move
    that into a stable filename next to `input_path` and return its path.
    """
    input_path = os.path.abspath(input_path)
    if not os.path.isfile(input_path):
        raise FileNotFoundError(input_path)

    stem_name = Path(input_path).stem
    work_dir = output_dir or tempfile.mkdtemp(prefix="demucs_")

    cmd = [
        _which_python(), "-m", "demucs",
        "--two-stems=bass",       # bass + (everything else); fastest 2-stem mode
        "-o", work_dir,
        input_path,
    ]
    print(f"  [demucs] {' '.join(cmd[3:])}")
    subprocess.run(cmd, check=True)

    # Demucs output convention
    bass_wav = os.path.join(work_dir, "htdemucs", stem_name, "bass.wav")
    if not os.path.isfile(bass_wav):
        # Fallback: scan for *bass*.wav under work_dir
        for root, _, files in os.walk(work_dir):
            for fn in files:
                if fn.lower().startswith("bass") and fn.lower().endswith(".wav"):
                    bass_wav = os.path.join(root, fn)
                    break
        if not os.path.isfile(bass_wav):
            raise RuntimeError(f"Demucs did not produce a bass stem under {work_dir}")

    return bass_wav
