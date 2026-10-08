# Ported from mm3d-dekui tools/mm3d/restore_action_flips.py and
# tools/mm3d/stop_map_slides.py (tgacquin/mm3d-dekui).
# SPDX-License-Identifier: GPL-2.0-or-later
"""Grezzo GAR v2 archives and MFL layouts, as dekui's setup edits them.

No game data: these functions read and rewrite the user's own archives.
"""
import struct


def u16(b, at):
    return struct.unpack_from('<H', b, at)[0]


def u32(b, at):
    return struct.unpack_from('<I', b, at)[0]


def string(b, at):
    if not 0 <= at < len(b):
        raise ValueError('Invalid string offset')
    return b[at:b.index(b'\0', at)].decode('ascii')


def entries(b):
    """{name: (record offset, data-offset slot, payload)} of a GAR v2."""
    if b[:4] != b'GAR\x02' or u32(b, 4) != len(b):
        raise ValueError('Unsupported GAR header')
    count, table, offsets = u16(b, 10), u32(b, 16), u32(b, 20)
    result = {}
    for i in range(count):
        record, address = table + i * 12, offsets + i * 4
        size, start = u32(b, record), u32(b, address)
        name = string(b, u32(b, record + 8))
        if start + size > len(b) or name in result:
            raise ValueError('Invalid or duplicate GAR entry')
        result[name] = (record, address, b[start:start + size])
    return result


def layout(b):
    """An MFL v4 layout's widget names and {animation: (bytes, [(track offset, widget)])}."""
    if b[:4] != b'MFL ' or u32(b, 4) != 4:
        raise ValueError('Unsupported MFL header')
    node_count, panes = u16(b, 10), u16(b, 14)
    players, anim_count = u16(b, 16), u16(b, 18)
    start, strings = u32(b, 24), u32(b, 28)
    names, pos = [], strings
    for _ in range(1 + panes + node_count + players + anim_count):
        name = string(b, pos)
        names.append(name)
        pos += len(name) + 1
    widgets = names[1 + panes:1 + panes + node_count]
    anim_names = names[1 + panes + node_count + players:]
    animations, pos = {}, start
    for name in anim_names:
        begin = pos
        tracks, rate = u16(b, pos), u16(b, pos + 2)
        if rate != 30 or name in animations:
            raise ValueError('Unsupported animation header')
        pos += 8
        targets = []
        for _ in range(tracks):
            target, keys = u16(b, pos), u32(b, pos + 4)
            if target >= node_count or keys > 1024:
                raise ValueError('Invalid animation track')
            targets.append((pos - begin, widgets[target]))
            pos += 12 + keys * 12
            if pos > strings:
                raise ValueError('Animation exceeds section')
        animations[name] = (b[begin:pos], targets)
    if pos != strings:
        raise ValueError('Unexpected animation section padding')
    return widgets, animations


def _restore_layout(original, modified):
    _, native = layout(original)
    widgets, current = layout(modified)
    replacements = {}
    for name in ('action_text_display', 'action_text_vanish'):
        data, targets = native[name]
        if not targets:
            raise ValueError('Original animation has no tracks')
        mapped = bytearray(data)
        for at, target in targets:
            if widgets.count(target) != 1:
                raise ValueError('Animation target missing or ambiguous: ' + target)
            struct.pack_into('<H', mapped, at, widgets.index(target))
        old = current[name][0]
        if u16(old, 0) and old != mapped:
            raise ValueError('Existing custom animation; refusing to replace ' + name)
        replacements[name] = bytes(mapped)
    data = b''.join(replacements.get(name, value[0]) for name, value in current.items())
    start, strings = u32(modified, 24), u32(modified, 28)
    result = bytearray(modified[:start] + data + modified[strings:])
    struct.pack_into('<I', result, 28, start + len(data))
    layout(result)
    return bytes(result)


def restore_actions(original, modified):
    """Restoration's Joker.Main.Play.gar with the native A/B prompt tracks of
    the user's own archive; other entries keep their offsets and bytes."""
    originals, installed = entries(original), entries(modified)
    result = bytearray(modified)
    for name in ('AActionIcon.mfl', 'BActionIcon.mfl'):
        record, address, current = installed[name]
        fixed = _restore_layout(originals[name][2], current)
        if fixed == current:
            continue
        result.extend(b'\0' * (-len(result) % 8))
        struct.pack_into('<I', result, address, len(result))
        struct.pack_into('<I', result, record, len(fixed))
        result.extend(fixed)
    struct.pack_into('<I', result, 4, len(result))
    actual = entries(result)
    for name, (_, _, payload) in installed.items():
        if name not in ('AActionIcon.mfl', 'BActionIcon.mfl') and actual[name][2] != payload:
            raise ValueError('Unrelated GAR entry changed')
    return bytes(result)


def stop_map_slides(data):
    """Hold the map's entrance/exit tracks at their open pose."""
    result = bytearray(data)
    _, address, resource = entries(data)['MenuMapDungeon.mfl']
    widgets, animations = layout(resource)
    base = u32(data, address)
    pos = u32(resource, 24)
    changed = 0
    for name, (raw, targets) in animations.items():
        p = 8
        for _ in targets:
            obj, prop, count, kind, duration = struct.unpack_from('<HHHHI', raw, p)
            p += 12
            offsets = list(range(p, p + (duration + 1) * 4, 4) if kind == 2
                           else range(p + 8, p + count * 12, 12))
            value = None
            if name in ('in_menu_map_anim', 'out_menu_map_anim') and prop <= 2:
                value = 202.0 if widgets[obj] == 'btn_return_l' and prop == 1 else 0.0
            if name in ('in_bb_map_area', 'out_bb_map_area'):
                if prop == 0:
                    value = 256.0 if widgets[obj] == 'bb_map_area_right' else 0.0
                elif prop == 18:
                    value = 1.0
            if value is not None:
                for offset in offsets:
                    at = base + pos + offset
                    packed = struct.pack('<f', value)
                    changed += result[at:at + 4] != packed
                    result[at:at + 4] = packed
            p += (duration + 1) * 4 if kind == 2 else count * 12
        pos += len(raw)
    return bytes(result), changed
