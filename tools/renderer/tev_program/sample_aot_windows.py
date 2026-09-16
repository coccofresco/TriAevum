"""Bounded, intrusive user-mode x64 sampler. Diagnostic observations, NOT FPS.

Samples the thread with the largest cycle delta in each interval. This excludes
idle threads but is not equivalent to an ETW CPU profile or exclusive CPU time.
Only the explicitly supplied process is inspected. Every suspension is balanced.
"""
import argparse
import bisect
from collections import Counter
import ctypes as c
from ctypes import wintypes as w
import json
import hashlib
from pathlib import Path
import re
import time


def read_map(path):
    text = path.read_text(errors="replace")
    base = re.search(r"Preferred load address is ([0-9a-fA-F]+)", text)
    if not base:
        raise ValueError("Link map has no preferred load address")
    entries = {}
    for line in text.splitlines():
        match = re.match(r"\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s", line)
        if match:
            entries.setdefault(int(match[2], 16) - int(base[1], 16), match[1])
    return sorted(entries.items())


def classify_symbol(symbol):
    symbol = symbol or ""
    if "ReadFast@" in symbol:
        return "memory_read_helper"
    if "WriteFast@" in symbol:
        return "memory_write_helper"
    if "Oot3dAotEnterBlock" in symbol:
        return "block_entry_helper"
    if any(token in symbol for token in ("F32", "Vfp", "RoundFinite", "RoundTo", "DecodeFinite", "ShiftRightJam")):
        return "scalar_float_helper"
    if "ExecuteOot3dWholeAot" in symbol:
        return "dispatch"
    if "Execute_" in symbol:
        return "translated_body_including_inlined_helpers"
    return "other_or_unresolved"


def sha256(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--module", type=Path, required=True)
    parser.add_argument("--map", type=Path)
    args = parser.parse_args()
    if c.sizeof(c.c_void_p) != 8 or not 0 < args.seconds <= 120:
        parser.error("Requires x64 Python and a bounded duration <=120 seconds")
    k = c.WinDLL("kernel32", use_last_error=True)
    def api(name, restype, *types):
        fn = getattr(k, name)
        fn.restype, fn.argtypes = restype, types
        return fn
    snapshot = api("CreateToolhelp32Snapshot", w.HANDLE, w.DWORD, w.DWORD)
    close = api("CloseHandle", w.BOOL, w.HANDLE)
    open_thread = api("OpenThread", w.HANDLE, w.DWORD, w.BOOL, w.DWORD)
    cycles = api("QueryThreadCycleTime", w.BOOL, w.HANDLE, c.POINTER(c.c_ulonglong))
    suspend = api("SuspendThread", w.DWORD, w.HANDLE)
    resume = api("ResumeThread", w.DWORD, w.HANDLE)
    context = api("GetThreadContext", w.BOOL, w.HANDLE, c.c_void_p)
    class Thread(c.Structure):
        _fields_ = [("size", w.DWORD), ("usage", w.DWORD), ("tid", w.DWORD),
                    ("pid", w.DWORD), ("base", w.LONG), ("delta", w.LONG), ("flags", w.DWORD)]
    class Module(c.Structure):
        _fields_ = [("size", w.DWORD), ("id", w.DWORD), ("pid", w.DWORD),
                    ("global_usage", w.DWORD), ("usage", w.DWORD), ("base", c.c_void_p),
                    ("length", w.DWORD), ("handle", w.HMODULE),
                    ("name", w.WCHAR * 256), ("path", w.WCHAR * 260)]
    def enumerate_snapshot(flag, typ, first_name, next_name):
        handle = snapshot(flag, args.pid)
        if handle == c.c_void_p(-1).value:
            raise c.WinError(c.get_last_error())
        first = api(first_name, w.BOOL, w.HANDLE, c.POINTER(typ))
        following = api(next_name, w.BOOL, w.HANDLE, c.POINTER(typ))
        try:
            item = typ(); item.size = c.sizeof(item)
            valid = first(handle, c.byref(item))
            while valid:
                yield typ.from_buffer_copy(item)
                valid = following(handle, c.byref(item))
        finally:
            close(handle)
    modules = list(enumerate_snapshot(8, Module, "Module32FirstW", "Module32NextW"))
    if not any(Path(m.path).resolve() == args.module.resolve() for m in modules):
        raise RuntimeError("Requested module is not loaded in the supplied process")
    entries = read_map(args.map) if args.map else []
    if args.map and not entries:
        raise ValueError("Empty symbol map")
    starts = [entry[0] for entry in entries]
    handles, previous, hits, tids = {}, {}, Counter(), Counter()
    errors = Counter()
    raw = c.create_string_buffer(1232 + 16)
    aligned = (c.addressof(raw) + 15) & ~15
    started = time.monotonic()
    try:
        for thread in enumerate_snapshot(4, Thread, "Thread32First", "Thread32Next"):
            if thread.pid == args.pid:
                handle = open_thread(0x0040 | 0x0008 | 0x0002, False, thread.tid)
                if handle:
                    handles[thread.tid] = handle
        while time.monotonic() - started < args.seconds:
            candidates = []
            for tid, handle in handles.items():
                value = c.c_ulonglong()
                if cycles(handle, c.byref(value)):
                    delta = value.value - previous.get(tid, value.value)
                    previous[tid] = value.value
                    if delta:
                        candidates.append((delta, tid, handle))
            if candidates:
                _, tid, handle = max(candidates)
                if suspend(handle) != 0xFFFFFFFF:
                    try:
                        c.c_uint32.from_address(aligned + 48).value = 0x100001
                        if context(handle, aligned):
                            hits[c.c_uint64.from_address(aligned + 248).value] += 1
                            tids[tid] += 1
                        else:
                            errors["context"] += 1
                    finally:
                        if resume(handle) == 0xFFFFFFFF:
                            raise c.WinError(c.get_last_error())
                else:
                    errors["suspend"] += 1
            time.sleep(0.004)
    finally:
        for handle in handles.values():
            close(handle)
    groups, categories, observations = Counter(), Counter(), []
    for address, count in hits.items():
        module = next((m for m in modules if m.base <= address < m.base + m.length), None)
        name, offset, symbol = "unknown", address, None
        if module:
            name, offset = module.path, address - module.base
            if Path(name).resolve() == args.module.resolve() and entries:
                index = bisect.bisect_right(starts, offset) - 1
                if 0 <= index < len(entries) - 1:
                    symbol = entries[index][1]
        groups[(name, symbol)] += count
        if module and Path(name).resolve() == args.module.resolve():
            categories[classify_symbol(symbol)] += count
        observations.append({"module": name, "rva": hex(offset), "symbol": symbol, "count": count})
    result = {"method": "intrusive busiest-cycle-delta thread RIP observations; not exclusive CPU time",
              "seconds": time.monotonic() - started, "samples": sum(hits.values()),
              "module_sha256": sha256(args.module),
              "map_sha256": sha256(args.map) if args.map else None,
              "target_module_samples": sum(categories.values()),
              "target_module_categories": dict(categories),
              "threads": dict(tids), "errors": dict(errors),
              "groups": [{"module": m, "symbol": s, "count": n} for (m, s), n in groups.most_common()],
              "observations": observations}
    args.output.write_text(json.dumps(result, indent=2))
    print(json.dumps({k: result[k] for k in ("samples", "threads", "errors")}))


if __name__ == "__main__":
    main()
