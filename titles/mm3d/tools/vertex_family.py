"""MM3D title vertex-program family (the MM counterpart of OoT3D's built-in
tools/oot3d/native_pica_frontend/oot3d_native_vertex.spvpack).

Same production path as OoT3D: the title's original SHBIN programs go through
tools/renderer/tev_program/build_vertex_artifacts (--translate), which emits the
translated vertex programs and the title pass programs (shadow casters,
surface lighting) into one title program pack. MM additionally scopes the
build with --vertex-only because LineShader carries a geometry DVLE that is
reported, not translated.

The only input is the user's own dump (romfs.bin, IVFC level 3). Every SHBIN
is guarded by its SHA-256 in vertex_family.json; nothing is shipped here.

  python vertex_family.py --romfs romfs.bin --tool build_vertex_artifacts.exe --out mm3d_vertex.spvpack
  python vertex_family.py --romfs romfs.bin --tool build_vertex_artifacts.exe --check mm3d_vertex.spvpack
"""
import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

FACTS = Path(__file__).with_name("vertex_family.json")
NONE = 0xFFFFFFFF


def load_facts(path=FACTS):
    facts = json.loads(Path(path).read_text())
    if facts.get("format") != "triaevum_mm3d_vertex_family_v1":
        raise ValueError("unknown vertex family fact format")
    return facts


def romfs_files(stream, directory):
    """Yields (name, bytes) for the files directly in /directory of an IVFC RomFS."""
    head = stream.read(0x60)
    if len(head) < 0x60 or head[:4] != b"IVFC":
        raise ValueError("not an IVFC RomFS image")
    master_hash = struct.unpack_from("<I", head, 0x08)[0]
    block = 1 << struct.unpack_from("<I", head, 0x4C)[0]
    level3 = (0x60 + master_hash + block - 1) // block * block
    stream.seek(level3)
    header = struct.unpack("<10I", stream.read(40))
    if header[0] != 0x28:
        raise ValueError("unexpected RomFS level 3 header")
    dir_off, dir_len, file_off, file_len, data_off = header[3], header[4], header[7], header[8], header[9]
    stream.seek(level3 + dir_off)
    dirs = stream.read(dir_len)
    stream.seek(level3 + file_off)
    files = stream.read(file_len)

    def dir_entry(offset):
        _, sibling, child, first_file, _, name_len = struct.unpack_from("<6I", dirs, offset)
        return sibling, child, first_file, dirs[offset + 0x18:offset + 0x18 + name_len].decode("utf-16le")

    _, child, _, _ = dir_entry(0)
    target = None
    while child != NONE:
        sibling, _, first_file, name = dir_entry(child)
        if name == directory:
            target = first_file
            break
        child = sibling
    if target is None:
        raise ValueError(f"RomFS has no /{directory}")
    entry = target
    while entry != NONE:
        _, sibling, offset, size, _, name_len = struct.unpack_from("<IIQQII", files, entry)
        name = files[entry + 0x20:entry + 0x20 + name_len].decode("utf-16le")
        stream.seek(level3 + data_off + offset)
        yield name, stream.read(size)
        entry = sibling


def extract(romfs, facts, out_dir):
    wanted = facts["shbin"]
    found = {}
    with open(romfs, "rb") as stream:
        for name, data in romfs_files(stream, facts["romfs_directory"]):
            if name not in wanted:
                continue
            digest = hashlib.sha256(data).hexdigest()
            if digest != wanted[name]:
                raise ValueError(f"{name}: sha256 {digest} is not the supported revision's {wanted[name]}")
            (Path(out_dir) / name).write_bytes(data)
            found[name] = digest
    missing = sorted(set(wanted) - set(found))
    if missing:
        raise ValueError("RomFS lacks " + ", ".join(missing))
    return [Path(out_dir) / name for name in sorted(wanted)]


def summary(output):
    for line in reversed(output.splitlines()):
        if line.startswith("native_vertex_artifacts="):
            return {key: int(value) for key, value in (field.split("=") for field in line.split())}
    raise ValueError("build_vertex_artifacts printed no summary")


def run(tool, romfs, pack, check, facts=None):
    facts = facts or load_facts()
    with tempfile.TemporaryDirectory() as scratch:
        inputs = [str(path) for path in extract(romfs, facts, scratch)]
        env = dict(os.environ)
        env["PATH"] = str(Path(tool).parent) + os.pathsep + env.get("PATH", "")
        if check:
            env["TRIAEVUM_TITLE_PROGRAM_PACK"] = str(pack)
            command = [str(tool), "--check", "--translate", "--vertex-only", *inputs]
        else:
            env.pop("TRIAEVUM_TITLE_PROGRAM_PACK", None)
            command = [str(tool), str(pack), "--translate", "--vertex-only", *inputs]
        result = subprocess.run(command, env=env, capture_output=True, text=True)
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        if result.returncode != 0:
            raise RuntimeError(f"build_vertex_artifacts exited {result.returncode}")
        counts = summary(result.stdout)
        if counts != facts["expected"]:
            raise ValueError(f"family {counts} differs from {facts['expected']}")
    return hashlib.sha256(Path(pack).read_bytes()).hexdigest()


def main(argv=None):
    parser = argparse.ArgumentParser(description="MM3D title vertex-program family")
    parser.add_argument("--romfs", required=True)
    parser.add_argument("--tool", required=True)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--out")
    group.add_argument("--check")
    args = parser.parse_args(argv)
    digest = run(args.tool, args.romfs, args.check or args.out, bool(args.check))
    print(("mm3d_vertex_family checked" if args.check else "mm3d_vertex_family written") + f" sha256={digest}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, RuntimeError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(2)
