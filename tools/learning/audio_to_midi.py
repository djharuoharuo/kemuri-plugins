"""basic-pitch wrapper — converts an audio file (typically an isolated bass
stem) into a MIDI file.

basic-pitch (Spotify) handles monophonic and polyphonic content; for bass
stems we tune the parameters to favor low-frequency, monophonic content.
"""
import os
import tempfile


def audio_to_midi(input_path: str, output_dir: str | None = None) -> str:
    """Convert `input_path` audio to a MIDI file. Returns the .mid path.

    Lazy-imports basic_pitch so the rest of the project doesn't have to
    load tensorflow at startup.
    """
    from basic_pitch.inference import predict_and_save
    from basic_pitch import ICASSP_2022_MODEL_PATH

    input_path = os.path.abspath(input_path)
    out_dir = output_dir or tempfile.mkdtemp(prefix="basicpitch_")
    os.makedirs(out_dir, exist_ok=True)

    print(f"  [basic-pitch] -> {out_dir}")
    # predict_and_save writes <stem>_basic_pitch.mid into out_dir
    predict_and_save(
        audio_path_list=[input_path],
        output_directory=out_dir,
        save_midi=True,
        sonify_midi=False,
        save_model_outputs=False,
        save_notes=False,
        model_or_model_path=ICASSP_2022_MODEL_PATH,
        # Bass-friendly thresholds:
        onset_threshold=0.5,        # default 0.5; lower = more notes
        frame_threshold=0.3,        # default 0.3
        minimum_note_length=58,     # ms; default 58 — keeps short hits
        minimum_frequency=27.5,     # A0 ~ keep sub-bass
        maximum_frequency=523.25,   # C5 — drop anything in the upper register
        multiple_pitch_bends=False,
        melodia_trick=True,         # helps monophonic bass tracking
    )

    stem = os.path.splitext(os.path.basename(input_path))[0]
    midi_path = os.path.join(out_dir, f"{stem}_basic_pitch.mid")
    if not os.path.isfile(midi_path):
        # Scan dir as fallback
        for fn in os.listdir(out_dir):
            if fn.endswith(".mid"):
                midi_path = os.path.join(out_dir, fn)
                break
        if not os.path.isfile(midi_path):
            raise RuntimeError(f"basic-pitch did not produce a MIDI file in {out_dir}")
    return midi_path
