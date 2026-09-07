"""Measure native BMP pixels and optional source-sample attribution."""

import argparse
import hashlib
import json
import struct
from pathlib import Path

try:
    from .common import atomic_write_json
except ImportError:
    from common import atomic_write_json


def pixel_identity(path: Path) -> tuple[tuple[int, int], str]:
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError("Invalid BMP: " + str(path))
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height, planes, depth, compression = struct.unpack_from("<iiHHI", data, 18)
    if width <= 0 or height == 0 or planes != 1 or depth not in (24, 32) or compression != 0:
        raise ValueError("Unsupported native BMP layout")
    stride = ((width * depth + 31) // 32) * 4
    if offset < 54 or offset + stride * abs(height) > len(data):
        raise ValueError("Truncated BMP pixel data")
    digest = hashlib.sha256()
    rows = range(abs(height)) if height < 0 else reversed(range(height))
    for row in rows:
        pixels = data[offset + stride * row:offset + stride * row + width * (depth // 8)]
        if depth == 32:
            pixels = b"".join(pixels[i:i + 3] for i in range(0, len(pixels), 4))
        digest.update(pixels)
    return (width, abs(height)), digest.hexdigest()


def analyze(paths: list[Path]) -> dict:
    if len(paths) < 2:
        raise ValueError("At least two sequential captures are required")
    images = [pixel_identity(path) for path in paths]
    if len({size for size, _ in images}) != 1:
        raise ValueError("Sequence dimensions changed")
    duplicates = [index for index in range(1, len(images)) if images[index][1] == images[index - 1][1]]
    attribution = []
    groups = {}
    for path, (dimensions, digest) in zip(paths, images):
        sidecar = path.with_name(path.name + ".json")
        if not sidecar.is_file():
            continue
        metadata = json.loads(sidecar.read_text(encoding="utf-8"))
        if (metadata.get("format") != "triaevum_framebuffer_sample_v1"
                or (metadata.get("width"), metadata.get("height")) != dimensions):
            raise ValueError("Capture metadata does not match framebuffer")
        sample = metadata.get("temporal_sample")
        if sample is None:
            continue
        attribution.append({"file": path.name, **sample})
        if sample["history_reset"] or sample["previous_source"] == sample["current_source"]:
            continue
        multiplier = sample["multiplier"]
        if multiplier not in (2, 3) or not 0 <= sample["ordinal"] < multiplier:
            continue
        key = (sample["continuity_epoch"], sample["previous_source"], sample["current_source"], multiplier)
        groups.setdefault(key, {}).setdefault(sample["ordinal"], set()).add(digest)
    complete = [(key, samples) for key, samples in groups.items() if len(samples) == key[3]]
    temporal = {"attributed_frames": len(attribution), "source_intervals": len(groups),
                "complete_intervals": len(complete),
                "complete_intervals_with_distinct_pixels_per_ordinal": sum(
                    len(set.union(*samples.values())) >= key[3] and
                    all(samples[a].isdisjoint(samples[b]) for a in samples for b in samples if a != b)
                    for key, samples in complete),
                "samples": attribution}
    return {"format": "triaevum_frame_pixel_sequence_v1", "frames": len(images),
            "dimensions": images[0][0], "unique_pixel_images": len({digest for _, digest in images}),
            "identical_adjacent_pairs": len(duplicates), "adjacent_pairs": len(images) - 1,
            "duplicate_indices": duplicates,
            "files": [{"name": path.name, "pixel_sha256": digest} for path, (_, digest) in zip(paths, images)],
            "temporal_attribution": temporal,
            "scope": "captured_pixels_and_optional_sample_identity_not_full_interpolation_correctness_or_performance"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--pattern", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = analyze(sorted(args.directory.glob(args.pattern)))
    atomic_write_json(args.output, result)
    summary = {key: value for key, value in result.items() if key != "files"}
    summary["temporal_attribution"] = {key: value for key, value in
        result["temporal_attribution"].items() if key != "samples"}
    print(summary)
