#!/usr/bin/env python3
"""Refuse an A32 registry that lacks a block entry for an MM3D guest patch site.

Every dekui/Restoration guest patch site in the source tree (the instruction
contract table titles/mm3d/dekui/core_test_sites.h) must start a registry
block, and so must the instruction after it. A registry generated before the
integration cut (a32_generated_v10/v11) lacks the button-emission boundaries
0x134DE4/0x134DE8 and the game dies at startup; configure calls this check so
such a build is refused instead (docs/MM3D_PRODUCT_QUALIFICATION.md, registry
rule).

    check_registry_boundaries.py --sites core_test_sites.h --registry registry.cpp \
        [--lighting-sites mm3d_lighting_hook_sites.h]

With --lighting-sites, every MM3D lighting/composition observer hook site
(titles/mm3d/adapter/mm3d_lighting_hook_sites.h) must also start a block in the
shard block tables beside registry.cpp; the runtime refuses to start otherwise
("MM3D lighting: wrong image or missing observer block").

Exit 0 when every boundary is an entry, 1 (listing the missing ones) otherwise.
"""

import argparse
import re
import sys
from pathlib import Path

SITE = re.compile(r'\{0x([0-9a-fA-F]{8})U,0x[0-9a-fA-F]{8}U,0x[0-9a-fA-F]{8}U,"')
LIGHTING_SITE = re.compile(r"Mm3dLightingHookSite\{0x([0-9A-Fa-f]{8})U")
BLOCK = re.compile(r"\{0x([0-9A-Fa-f]{8})U, kShard[0-9]+Ops")
ENTRY = re.compile(r'\{0x([0-9A-Fa-f]{8})U,\s*0x[0-9A-Fa-f]{8}U,\s*"')


def boundaries(sites_text: str) -> list[int]:
    pcs = {int(pc, 16) for pc in SITE.findall(sites_text)}
    return sorted(pcs | {pc + 4 for pc in pcs})


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--sites", type=Path, required=True)
    parser.add_argument("--registry", type=Path, required=True)
    parser.add_argument("--lighting-sites", type=Path)
    args = parser.parse_args()
    required = boundaries(args.sites.read_text(encoding="utf-8"))
    if not required:
        print(f"no guest patch sites found in {args.sites}", file=sys.stderr)
        return 1
    registry = args.registry.read_text(encoding="utf-8")
    entries = {int(pc, 16) for pc in ENTRY.findall(registry)}
    missing = [pc for pc in required if pc not in entries]
    if missing:
        print(f"A32 registry {args.registry} lacks {len(missing)} MM3D guest patch "
              "boundaries: " + ", ".join(f"0x{pc:08X}" for pc in missing) +
              ". A tree with the integration cut needs registry a32_generated_overlay "
              "(v10/v11 predate the dekui boundaries).", file=sys.stderr)
        return 1
    print(f"A32 registry has all {len(required)} MM3D guest patch boundaries")
    if args.lighting_sites is None:
        return 0
    hooks = sorted({int(pc, 16) for pc in LIGHTING_SITE.findall(args.lighting_sites.read_text(encoding="utf-8"))})
    if not hooks:
        print(f"no lighting hook sites found in {args.lighting_sites}", file=sys.stderr)
        return 1
    blocks = set()
    for shard in sorted(args.registry.parent.glob("shard_*.cpp")):
        blocks.update(int(pc, 16) for pc in BLOCK.findall(shard.read_text(encoding="utf-8", errors="replace")))
    absent = [pc for pc in hooks if pc not in blocks]
    if absent:
        print(f"A32 registry {args.registry.parent} lacks {len(absent)} of {len(hooks)} MM3D lighting hook blocks: "
              + ", ".join(f"0x{pc:08X}" for pc in absent[:12]) + (" ..." if len(absent) > 12 else "")
              + ". The MM3D runtime needs registry a32_generated_overlay_r18 (rule D8).", file=sys.stderr)
        return 1
    print(f"A32 registry starts a block at all {len(hooks)} MM3D lighting hook sites")
    return 0


if __name__ == "__main__":
    sys.exit(main())
