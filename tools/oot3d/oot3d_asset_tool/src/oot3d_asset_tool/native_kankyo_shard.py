from __future__ import annotations

import argparse
import hashlib
import json
import zipfile
from pathlib import Path

from .zar import ZarArchive


FORMAT = "oot3d_native_kankyo_shard_v1"
MANIFEST_RESOURCE = "oot3d/catalog/shards/oot3d-kankyo-native.json"
RESOURCE_PREFIX = "oot3d/native/kankyo"


def build_manifest(sources: list[Path]) -> dict[str, object]:
    records = []
    for source in sources:
        data = source.read_bytes()
        archive = ZarArchive.parse(data, str(source))
        type_counts: dict[str, int] = {}
        for file in archive.files:
            type_counts[file.type_name] = type_counts.get(file.type_name, 0) + 1
        records.append({
            "archive_name": source.name,
            "resource": f"{RESOURCE_PREFIX}/{source.name}",
            "source_path": str(source),
            "byte_length": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            "file_count": len(archive.files),
            "type_counts": type_counts,
            "files": [{"index": file.index, "name": file.name,
                       "type": file.type_name, "offset": file.offset,
                       "size": file.size} for file in archive.files],
        })
    return {"format": FORMAT, "status": "complete", "archive_count": len(records),
            "records": records}


def write_archive(path: Path, manifest: dict[str, object]) -> None:
    package = {"name": "OOT3D Native Kankyo", "author": "local", "version": "0.1.0",
               "description": "Unmodified native OOT3D kankyo ZAR sources.", "code_version": 1}
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        for name, value in (("manifest.json", package), (MANIFEST_RESOURCE, manifest)):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            output.writestr(info, json.dumps(value, indent=2) + "\n")
        for record in manifest["records"]:
            info = zipfile.ZipInfo(record["resource"], date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            output.writestr(info, Path(record["source_path"]).read_bytes())


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Package unmodified native OOT3D kankyo ZAR sources.")
    parser.add_argument("--source", type=Path, action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest-output", type=Path, required=True)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args(argv)
    for source in args.source:
        if not source.is_file():
            parser.error(f"native kankyo source not found: {source}")
    manifest = build_manifest(args.source)
    args.manifest_output.parent.mkdir(parents=True, exist_ok=True)
    args.manifest_output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
    write_archive(args.output, manifest)
    if args.verify:
        with zipfile.ZipFile(args.output) as archive:
            for record in manifest["records"]:
                payload = archive.read(record["resource"])
                if hashlib.sha256(payload).hexdigest() != record["sha256"]:
                    raise RuntimeError(f"archive payload hash mismatch: {record['archive_name']}")
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
