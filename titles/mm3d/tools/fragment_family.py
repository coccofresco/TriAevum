"""MM3D title fragment-family pack (titles/mm3d/shaders/mm3d_native_fragment.spvpack).

OoT3D's fragment families are all title-independent built-ins
(runtime/.../pica_native_fragment.spvpack, pica_uber_fragment, native lighting
uber, temporal, toon, caster), produced by build_fragment_artifacts from the
parametric TEV generator with no title input. MM3D shares exactly those
families, so its title fragment family (appended by InstallTitleFragmentPack)
holds no programs: it is the empty SPVK pack (header only, 0 entries). This
tool writes it and --check verifies a pack is exactly that, so a stray
duplicate of a shared family (which would be prepared twice) is refused.
"""
import argparse
import hashlib
import struct
import sys
from pathlib import Path

MAGIC, VERSION, HEADER = 0x4B565053, 1, 16


def empty_pack():
    return struct.pack("<4I", MAGIC, VERSION, 0, HEADER)


def check(data):
    if len(data) < HEADER:
        raise ValueError("pack truncated")
    magic, version, count, size = struct.unpack_from("<4I", data)
    if magic != MAGIC or version != VERSION or size != len(data):
        raise ValueError("not a SPIR-V pack of this version")
    if count != 0:
        raise ValueError(f"MM3D title fragment family must be empty (shared families), found {count} entries")


def main(argv=None):
    parser = argparse.ArgumentParser(description="MM3D title fragment-family pack")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--out")
    group.add_argument("--check")
    args = parser.parse_args(argv)
    if args.out:
        Path(args.out).write_bytes(empty_pack())
    path = Path(args.out or args.check)
    check(path.read_bytes())
    print(f"mm3d_fragment_family {'written' if args.out else 'checked'} sha256={hashlib.sha256(path.read_bytes()).hexdigest()}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(2)
