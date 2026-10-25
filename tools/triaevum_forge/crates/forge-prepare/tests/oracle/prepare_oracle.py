# Copyright TriAevum contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Runs one Python Forge operation for the forge-prepare oracle tests.

Usage: prepare_oracle.py REPO_ROOT REQUEST_JSON RESPONSE_JSON. The response
is {"ok": result} or {"error": str(exception), "type": class name}.
"""

import contextlib
import hashlib
import io
import json
import sys
from pathlib import Path


def synthetic_manifest(exheader, code, romfs):
    """test_forge.ForgeTests.build_process_manifest."""
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    return {
        "format": "oot3d_native_process_manifest_v1",
        "source": {
            "exheader_path": str(exheader.resolve()),
            "exheader_sha256": digest(exheader),
            "code_bin_path": str(code.resolve()),
            "code_bin_sha256": digest(code),
            "romfs_image_path": str(romfs.resolve()),
        },
        "process": {
            "entrypoint": 0x100000,
            "linear_heap": {"base_address": 0x14000000, "size": 0x4000000},
            "system_regions": [
                {"name": "ctr_vram", "address": 0x1F000000, "mapped_size": 0x600000}
            ],
        },
    }


def manifest(request):
    from tools.oot3d.native_a32_runtime.build_process_manifest import build_manifest

    value = build_manifest(Path(request["exheader"]), Path(request["code"]), Path(request["romfs"]))
    Path(request["output"]).write_bytes((json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8"))
    return {"written": request["output"]}


def prepare(request):
    import forge

    output_root = Path(request["output_root"]).expanduser().resolve()
    cache = forge.HashCache(output_root / ".hash-cache.json", enabled=not request.get("rehash", False))
    verified, _ = forge.verify_sources(
        request["recipe"],
        code_path=Path(request["code"]),
        exheader_path=Path(request["exheader"]),
        romfs_path=Path(request["romfs"]),
        cache=cache,
    )
    verified_mod = None
    if request.get("mod"):
        path = Path(request["mod"]["path"]).resolve()
        verified_mod = forge.VerifiedInput(
            request["mod"]["kind"], path, path.stat().st_size, cache.digest(path)
        )
    builder = synthetic_manifest if request.get("builder") == "synthetic" else None
    return forge.prepare_content(
        request["recipe"],
        verified,
        output_root=output_root,
        verified_mod=verified_mod,
        process_manifest_builder=builder,
    )


def make_legacy(request):
    """The first half of test_forge.ForgeTests.prepare_legacy_installation:
    an index and manifest written with absolute paths, as before
    manifest_relative_v1."""
    from common import atomic_write_json, load_json_object, sha256_file
    from installation_context import resolve_reference

    title = Path(request["title"])
    manifest_path = title / "process-manifest.json"
    value = load_json_object(manifest_path)
    for field in ("code_bin_path", "exheader_path", "romfs_image_path"):
        value["source"][field] = str(resolve_reference(value["source"][field], title))
    value.pop("source_path_scopes")
    atomic_write_json(manifest_path, value)
    index = load_json_object(title / "content.tap")
    index.pop("path_mode")
    for group in ("inputs", "optional_inputs"):
        for descriptor in index[group].values():
            descriptor["path"] = str(resolve_reference(descriptor["path"], title))
            descriptor.pop("path_scope")
    index["process_manifest"] = {
        "path": str(manifest_path),
        "bytes": manifest_path.stat().st_size,
        "sha256": sha256_file(manifest_path),
    }
    atomic_write_json(title / "content.tap", index)
    return {"title": str(title)}


def package(request):
    import forge

    return forge.package_private_module(
        Path(request["prepared"]),
        Path(request["native"]),
        title_aot_image=Path(request["title_aot"]) if request.get("title_aot") else None,
        target_triple=request["target"],
        translator_identity=request["translator"],
        active_title_state=Path(request["active"]) if request.get("active") else None,
    )


def activate(request):
    import forge

    return forge.activate_prepared_title(
        Path(request["prepared"]), active_title_state=Path(request["active"])
    )


def load(request):
    import forge

    prepared = forge.load_prepared_content(
        Path(request["prepared"]), required_inputs=tuple(request["required"])
    )
    return {
        "directory": str(prepared.directory),
        "inputs": {kind: item.as_json() for kind, item in prepared.inputs.items()},
    }


def tam(request):
    import tam_builder

    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        status = tam_builder.main(request["argv"])
    return {"status": status, "stdout": json.loads(output.getvalue())}


def title_path_length(request):
    import forge

    forge.check_title_path_length(Path(request["output_root"]), request["recipes"], windows=True)
    return {}


OPERATIONS = {
    "manifest": manifest,
    "prepare": prepare,
    "make_legacy": make_legacy,
    "package": package,
    "activate": activate,
    "load": load,
    "tam": tam,
    "title_path_length": title_path_length,
}


def main(repo_root, request_path, response_path):
    repo = Path(repo_root)
    sys.path.insert(0, str(repo))
    sys.path.insert(0, str(repo / "tools" / "triaevum_release"))
    request = json.loads(Path(request_path).read_text(encoding="utf-8"))
    try:
        response = {"ok": OPERATIONS[request["op"]](request)}
    except Exception as exc:  # the test compares messages
        response = {"error": str(exc), "type": type(exc).__name__}
    Path(response_path).write_text(json.dumps(response, default=str), encoding="utf-8")


if __name__ == "__main__":
    main(*sys.argv[1:])
