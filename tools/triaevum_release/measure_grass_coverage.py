"""Measure apparent Grass contribution in fixed, explicitly selected terrain ROIs.

Requires synchronized framebuffer captures and an otherwise identical Grass-off
control. This is visible image contribution, not a geometric occupancy oracle.
Never use the capture run for throughput measurements.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


def measure_region(reference, candidate, background, box, threshold=1):
    if reference.shape != candidate.shape or reference.shape != background.shape:
        raise ValueError("framebuffer dimensions differ")
    x0, y0, x1, y1 = box
    if not (0 <= x0 < x1 <= 1 and 0 <= y0 < y1 <= 1) or threshold < 1:
        raise ValueError("invalid normalized terrain region or threshold")
    height, width = reference.shape[:2]
    area = (slice(round(y0*height), round(y1*height)), slice(round(x0*width), round(x1*width)))
    base = background[area].astype(np.int16)
    old = np.abs(reference[area].astype(np.int16)-base).max(axis=2) >= threshold
    new = np.abs(candidate[area].astype(np.int16)-base).max(axis=2) >= threshold
    if not old.size:
        raise ValueError("empty terrain region")
    # Empty local tiles expose gaps that a full-frame RGB mean can conceal.
    def empty_tiles(mask):
        tiles = [mask[y:y+8,x:x+8] for y in range(0,mask.shape[0],8) for x in range(0,mask.shape[1],8)]
        return sum(not tile.any() for tile in tiles)/len(tiles)
    return {"pixels": int(old.size), "reference_coverage": float(old.mean()),
            "candidate_coverage": float(new.mean()), "gain_points": float(new.mean()-old.mean())*100,
            "lost_reference_fraction": float((old & ~new).sum()/max(1,old.sum())),
            "reference_empty_tiles": empty_tiles(old), "candidate_empty_tiles": empty_tiles(new)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("background", type=Path)
    parser.add_argument("regions", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    rows = []
    for region in json.loads(args.regions.read_text(encoding="utf-8-sig")):
        name = f"framebuffer_{region['frame']:06d}.bmp"
        images = [np.asarray(Image.open(path/name).convert("RGB"))
                  for path in (args.reference,args.candidate,args.background)]
        rows.append({**region, **measure_region(*images,region["box"])})
    if not rows:
        raise ValueError("no terrain regions")
    report = {"reference": str(args.reference), "candidate": str(args.candidate),
              "background": str(args.background), "regions": rows,
              "mean_gain_points": float(np.mean([r["gain_points"] for r in rows]))}
    args.output.write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(json.dumps(report,indent=2))


if __name__ == "__main__":
    main()
