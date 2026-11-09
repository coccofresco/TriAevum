# Copyright TriAevum contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes a synthetic EUR-layout Restoration case and the desktop Forge's
(tools/mm3d/restoration_layout.py) reconstruction of it.

usage: restoration_case.py <repository> <out>
out/dump/<romfs path>, out/resources/<relative>, out/layout.recipe.json,
out/expected/<output path>. No game data: every byte is synthetic.
"""
import hashlib
import json
from pathlib import Path
import sys

repository, out = Path(sys.argv[1]), Path(sys.argv[2])
sys.path.insert(0, str(repository / "tools" / "mm3d"))
import restoration_layout as rl  # noqa: E402


def sha(data):
    return hashlib.sha256(data).hexdigest()


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


play, base = bytes(range(64)) * 3, b"layout-base-" * 20
modified = base[:40] + b"RESTORED" + base[48:] + b"tail"
own = b"shipped member" * 7
project_base = b"mfpj synthetic" * 11
project = project_base.replace(b"synthetic", b"restored!")
types = ["unknown", "mfpk", "mfl", "ctxb"]
dump_gar = rl.emit_gar({"hash": "jenkins", "types": types, "align": 4,
                        "members": [("Play.mfpk", play), ("Mod.mfl", base)]})
write(out / "dump" / rl.DUMP_ARCHIVE, dump_gar)
write(out / "dump" / rl.DUMP_PROJECT, project_base)
write(out / "resources/layout/patches/Mod.bps", rl.bps_encode(base, modified))
write(out / "resources/layout/patches/Joker.bps", rl.bps_encode(project_base, project))
write(out / "resources/layout/members/Own.mfl", own)
new_gar = rl.emit_gar({"hash": "jenkins", "types": types, "align": 4,
                       "members": [("Play.mfpk", play), ("Mod.mfl", modified), ("Own.mfl", own)]})
recipe = {"format": rl.FORMAT, "outputs": [
    {"path": "layout/Joker.Main.New.gar", "sha256": sha(new_gar),
     "gar": {"version": 2, "hash": "jenkins", "types": types, "align": 4},
     "members": [
         {"name": "Play.mfpk", "source": {"dump": rl.DUMP_ARCHIVE + "#Play.mfpk", "sha256": sha(play)}},
         {"name": "Mod.mfl", "source": {"patch": "layout/patches/Mod.bps", "base": rl.DUMP_ARCHIVE + "#Mod.mfl",
                                        "base_sha256": sha(base), "sha256": sha(modified)}},
         {"name": "Own.mfl", "source": {"ship": "layout/members/Own.mfl", "sha256": sha(own)}}]},
    {"path": "layout/Joker.mfpj", "sha256": sha(project),
     "source": {"patch": "layout/patches/Joker.bps", "base": rl.DUMP_PROJECT,
                "base_sha256": sha(project_base), "sha256": sha(project)}}]}
write(out / "layout.recipe.json", json.dumps(recipe, indent=2).encode())


def read_dump(name):
    path = out / "dump" / name
    return path.read_bytes() if path.is_file() else None


result = rl.reconstruct(recipe, read_dump, lambda relative: (out / "resources" / relative).read_bytes())
for path, data in sorted(result.items()):
    write(out / "expected" / path, data)
    print(path, sha(data))
