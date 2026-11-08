#!/usr/bin/env python3
"""Write the local MM3D supported-revision recipe in Forge's
triaevum_supported_revisions_v1 shape (the same shape as OoT3D's
tools/triaevum_release/supported_revisions.json), from verified extracted
inputs. Title facts (guard identities) live in titles/mm3d/forge.

The recipe is LOCAL: it carries no title module, translator identity or
publication signature, so a packaged precompiled catalog still has to bind it
(validate_title requires abi_version 2 and an equal `inputs` contract).
"""
import argparse, hashlib, json, sys
from pathlib import Path

GUARD_CODE_SHA256 = "6eec27b1827eab2c87744ac38066d569ffe6fe75790b1eb3300bd91f5099038d"

def identity(path: Path) -> dict:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 22), b""):
            h.update(block)
    return {"bytes": path.stat().st_size, "sha256": h.hexdigest()}

def exheader_process(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) != 2048:
        raise SystemExit("ExHeader must be the 2048-byte decrypted header")
    entry = int.from_bytes(data[0x10:0x14], "little")
    size = int.from_bytes(data[0x14:0x18], "little") * 0x1000
    if size == 0 or entry == 0:
        raise SystemExit("ExHeader has no text segment")
    return {"base": entry, "entrypoint": entry, "executable_size": size}

def build(extract: Path, fragment: Path | None) -> dict:
    inputs = {k: identity(extract / f"{k}.bin") for k in ("code", "exheader", "romfs")}
    if inputs["code"]["sha256"] != GUARD_CODE_SHA256:
        raise SystemExit("code.bin is not the guarded MM3D EUR/USA-1.1 executable")
    recipe = {
        "id": f"mm3d-eur-local-{inputs['code']['sha256'][:8]}",
        "game": "mm3d",
        "title": "The Legend of Zelda: Majora's Mask 3D",
        "region": "EUR",
        "inputs": inputs,
        "process": exheader_process(extract / "exheader.bin"),
    }
    if fragment is not None:
        frag = json.loads(fragment.read_text())
        romfs = frag.get("inputs", {}).get("romfs", {}).get("sha256")
        if romfs != inputs["romfs"]["sha256"]:
            raise SystemExit("catalog fragment was derived from a different RomFS")
        for member in ("water_catalog", "room_catalog"):
            if member in frag:
                recipe[member] = frag[member]
    return {"format": "triaevum_supported_revisions_v1", "recipes": [recipe]}

def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--extract", type=Path, required=True)
    p.add_argument("--fragment", type=Path)
    p.add_argument("--out", type=Path, required=True)
    a = p.parse_args(argv)
    a.out.write_text(json.dumps(build(a.extract, a.fragment), indent=2, sort_keys=False) + "\n")

if __name__ == "__main__":
    sys.exit(main())
