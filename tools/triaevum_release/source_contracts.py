"""Required public source units shared by source creation and release auditing."""

import json
from pathlib import PurePosixPath

try:
    from .common import normalize_relative_path
except ImportError:
    from common import normalize_relative_path


def source_contract_errors(paths, read_bytes, policy):
    paths = set(paths)
    required = set(policy.get("source_archive_required_paths", []))
    manifests = policy.get("source_archive_required_manifests", [])
    required.update(manifests)
    for relative in sorted(required - paths):
        yield f"required public source missing: {relative}"
    for relative in manifests:
        if relative not in paths:
            continue
        try:
            manifest = json.loads(read_bytes(relative))
            if not isinstance(manifest, dict) or manifest.get("format") != "triaevum_public_source_unit_v1":
                raise ValueError("unsupported source unit format")
            files = manifest.get("files")
            if not isinstance(files, list) or not files:
                raise ValueError("source unit has no file inventory")
            seen = set()
            for entry in files:
                path = normalize_relative_path(entry["path"])
                if path in seen:
                    raise ValueError(f"duplicate file: {path}")
                seen.add(path)
                full = (PurePosixPath(relative).parent / path).as_posix()
                if full not in paths:
                    yield f"required public source missing: {full} (declared by {relative})"
        except (ValueError, KeyError, TypeError, OSError) as error:
            yield f"invalid public source inventory {relative}: {error}"
