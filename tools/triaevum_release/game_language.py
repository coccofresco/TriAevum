"""One persisted game-language preference shared by Forge and the runtime."""
from pathlib import Path
import json
try:
    from .common import atomic_write_json, load_json_object
    from .native_process import run_native
except ImportError:
    from common import atomic_write_json, load_json_object
    from native_process import run_native

FORMAT = "triaevum_game_language_v1"


def validate(document: dict) -> dict:
    if not isinstance(document, dict):
        raise ValueError("Invalid game language document")
    entries = document.get("available")
    if document.get("format") != FORMAT or not isinstance(entries, list) or not entries:
        raise ValueError("Invalid game language information")
    codes = set()
    for entry in entries:
        if (not isinstance(entry, dict) or not isinstance(entry.get("code"), str)
                or not isinstance(entry.get("label"), str) or not entry["label"]
                or type(entry.get("system_id")) is not int or not 0 <= entry["system_id"] <= 11
                or entry["code"] in codes):
            raise ValueError("Invalid game language entry")
        codes.add(entry["code"])
    if document.get("selected") not in codes:
        raise ValueError("Selected game language is unavailable")
    return document


def discover(runtime: Path, romfs: Path) -> dict:
    result = run_native([str(runtime), "--game-language-info", str(romfs)],
                        cwd=runtime.parent, capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise ValueError(f"Cannot inspect ROM languages: {result.stderr[-2000:]}")
    return validate(json.loads(result.stdout))


def config_path(data_root: Path) -> Path:
    return data_root / "config" / "game_language.json"


def install(data_root: Path, detected: dict) -> None:
    document = dict(validate(detected))
    path = config_path(data_root)
    if path.exists():
        previous = validate(load_json_object(path))["selected"]
        if previous in {entry["code"] for entry in document["available"]}:
            document["selected"] = previous
    atomic_write_json(path, document)


def select(data_root: Path, code: str) -> None:
    path = config_path(data_root)
    document = load_json_object(path)
    document["selected"] = code
    atomic_write_json(path, validate(document))
