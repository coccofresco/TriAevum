"""Extract a deterministic hot-block profile from a native runtime report."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


FORMAT = "oot3d_a32_hot_block_profile_v1"


def extract_profile(source: dict[str, object], max_blocks: int) -> dict[str, object]:
    if max_blocks <= 0:
        raise ValueError("max_blocks must be positive")
    if source.get("schema") != "oot3d_native_a32_vulkan_runtime_v1":
        raise ValueError("input is not a native A32 Vulkan runtime report")
    profile = source.get("a32_block_profile")
    if not isinstance(profile, dict) or not profile.get("enabled"):
        raise ValueError("input does not contain an enabled A32 block profile")

    source_blocks = profile.get("blocks")
    if not isinstance(source_blocks, list):
        raise ValueError("A32 block profile has no block list")
    blocks = source_blocks[:max_blocks]
    selected_samples = sum(int(block["samples"]) for block in blocks)
    total_samples = int(profile["total_samples"])
    if total_samples <= 0:
        raise ValueError("A32 block profile has no samples")
    return {
        "format": FORMAT,
        "source_authority": source.get("authority"),
        "captured_frames": int(source["frames"]),
        "sample_rate_denominator": int(profile["sample_rate_denominator"]),
        "block_entries": int(profile["block_entries"]),
        "total_samples": total_samples,
        "selected_samples": selected_samples,
        "selected_coverage": selected_samples / total_samples,
        "blocks": [
            {"pc": int(block["pc"]), "samples": int(block["samples"])}
            for block in blocks
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-blocks", type=int, default=1000)
    args = parser.parse_args()
    source = json.loads(args.input.read_text(encoding="utf-8"))
    document = extract_profile(source, args.max_blocks)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
