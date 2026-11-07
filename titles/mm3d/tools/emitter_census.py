"""Deterministic MM3D room material-role census; no decoded pixels or new lights.

Reports ALL used material texture slots, including non-primary semantic candidates
that the current emitter recipe cannot prove. Empty categories mean no proved
recipe match, not absence of magical effects/doorways from the game.
"""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct
import scene_emitters as mm


def material_roles(model):
    used = sorted({material for _, material in model.meshes})
    rows = []
    for material in used:
        if material >= len(model.materials):
            raise ValueError("mesh material out of range")
        indices, additive = model.materials[material]
        slots = []
        for slot, index in enumerate(indices):
            if index < 0:
                continue
            if index >= len(model.textures):
                raise ValueError("material texture out of range")
            name, width, height, fmt, kind, encoded = model.textures[index]
            role = mm.category(name, additive)
            excluded = bool(re.search(r"syoku|shoku|torch|taimatsu|rousoku|candle|water|mizu|shadow|kage", name.lower()))
            reason = ("excluded-fixture-or-non-emitter" if excluded else
                      "unknown-authored-role" if role is None else
                      "secondary-role-needs-combiner-UV-proof" if slot else
                      "missing-encoded-identity" if not encoded else "primary-semantic-candidate")
            slots.append(dict(slot=slot, texture=name, width=width, height=height,
                              format=fmt, kind=kind, category=role, reason=reason,
                              encodedHash=f"{mm.content_hash(encoded):016x}" if encoded else None))
        rows.append(dict(material=material, additive=additive, slots=slots,
                         reason="textured" if slots else "untextured-needs-authored-role-proof"))
    return rows


def setup_meshes(data):
    """Report ZSI-relative mesh pointers for base and alternate command lists.

    These are offline asset facts, not proof of the runtime's selected setup.
    """
    pending, visited, meshes = [16], set(), []
    while pending:
        start = pending.pop()
        if start in visited:
            continue
        if len(visited) >= 32:
            raise ValueError("excess alternate room headers")
        visited.add(start)
        mesh = None
        for at in range(start, start + 256, 8):
            if at < 16 or at + 8 > len(data):
                raise ValueError("truncated alternate room header")
            opcode, count = data[at:at+2]
            pointer = struct.unpack_from('<I', data, at+4)[0]
            if opcode == 0x0A:
                mesh = pointer
            if opcode == 0x18:
                table = 16 + pointer
                if count > 16 or table + count*4 > len(data):
                    raise ValueError("invalid alternate room table")
                pending.extend(16+p for p in struct.unpack_from('<'+'I'*count, data, table) if p)
            if opcode == 0x14:
                break
        else:
            raise ValueError("unterminated alternate room header")
        meshes.append(dict(header=start, mesh=mesh))
    return dict(headers=sorted(meshes, key=lambda m:m['header']),
                invariant=len({m['mesh'] for m in meshes})==1)


def census(code, files):
    ids = mm.scene_ids(code)
    rooms, rejected = [], []
    for name, packed in sorted(files, key=lambda item: item[0]):
        match = re.fullmatch(r"scenes/(.+)_(\d+)_info\.zsi", name)
        if not match or match[1] not in ids:
            continue
        try:
            data = mm.decompress(packed)
            models, at = [], 0
            while (at := data.find(b"cmb ", at)) >= 0:
                model = mm.Model(data, at)
                models.append(dict(offset=at, materials=material_roles(model),
                                   emitted=Counter(e['category'] for e in mm.model_emitters(model))))
                at += 4
            commands = []
            for at in range(16, min(len(data), 272)-7, 8):
                commands.append(data[at])
                if data[at] == 0x14:
                    break
            rooms.append(dict(source=name, scene=ids[match[1]], room=int(match[2]),
                              sha256=hashlib.sha256(data).hexdigest(), commands=commands,
                              setupMeshes=setup_meshes(data), models=models))
        except (ValueError, UnicodeError, struct.error) as error:
            rejected.append(dict(source=name, reason=str(error)))
    reasons, categories, emitted = Counter(), Counter(), Counter()
    for room in rooms:
        for model in room['models']:
            emitted.update(model['emitted'])
            for material in model['materials']:
                for slot in material['slots']:
                    reasons[slot['reason']] += 1
                    if slot['category']:
                        categories[slot['category']] += 1
    return dict(schema="mm3d.emitter-role-census", version=1, rooms=rooms, rejected=rejected,
                setupMeshVariants=[r['source'] for r in rooms if not r['setupMeshes']['invariant']],
                slotReasons=reasons, slotCategories=categories, unconsolidatedEmitters=emitted,
                limits=["No pixel analysis or blanket fullbright classification.",
                        "Secondary/atlas roles are unknown until combiner and UV ownership is proved.",
                        "Zero recipe matches do not prove semantic absence.",
                        "Header identity does not attest modified payloads or selected alternate setups."])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--code-bin', type=Path, required=True)
    parser.add_argument('--romfs', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args=parser.parse_args()
    result=census(args.code_bin.read_bytes(), mm._romfs_image_files(args.romfs, ('scenes',)))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2, sort_keys=True)+'\n', encoding='utf-8')
    print(json.dumps(dict(rooms=len(result['rooms']), rejected=len(result['rejected']),
                          slotReasons=result['slotReasons'], slotCategories=result['slotCategories'],
                          setupMeshVariants=result['setupMeshVariants'],
                          unconsolidatedEmitters=result['unconsolidatedEmitters']), sort_keys=True))

if __name__ == '__main__':
    main()
