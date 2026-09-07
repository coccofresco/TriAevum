#!/usr/bin/env python3
"""Extract the revision-pinned OOT3D CSAB curve evaluators."""

from __future__ import annotations

import argparse
import hashlib
import pathlib


EXPECTED_SOURCE_SHA256 = (
    "9cfaffc60ca92e56e8d989b94480cd1f5bd2a2bdd7100292de334ac9e0983791"
)
HELPER_START = "static const float OOT3D_CSAB_ZERO"
HELPER_END = "/*\n * Ghidra: FUN_0030487c"
S16_START = "/*\n * Ghidra: FUN_003084e8"
F32_START = "/*\n * Ghidra: FUN_003087a4"
F32_END = "/*\n * Ghidra: FUN_0048ba98"


def checked_slice(source: str, start_token: str, end_token: str) -> str:
    start = source.find(start_token)
    end = source.find(end_token, start + len(start_token))
    if start < 0 or end < 0 or end <= start:
        raise RuntimeError(
            f"maintained CSAB source boundary not found: "
            f"{start_token!r} -> {end_token!r}"
        )
    if source.find(start_token, start + 1) >= 0:
        raise RuntimeError(f"ambiguous CSAB start token: {start_token!r}")
    return source[start:end].rstrip()


def make_header() -> str:
    return """#pragma once

#include <cstdint>

namespace Oot3dSourceCsabCurve {

using u8 = std::uint8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using u32 = std::uint32_t;

struct Oot3dAnimationCurveState {
    const void* curve;
    u8 loop;
};

struct Oot3dAnimationCurveHeader {
    u8 interpolationType;
    u8 unk_01[3];
    s32 keyCount;
    u32 unk_08;
    s32 duration;
};

struct Oot3dAnimationCurveF32Key {
    s32 frame;
    float value;
    float tangentIn;
    float tangentOut;
};

struct Oot3dAnimationCurveF32LinearKey {
    s32 frame;
    float value;
};

struct Oot3dAnimationCurveS16Key {
    s16 frame;
    s16 value;
    s16 tangentIn;
    s16 tangentOut;
};

static_assert(sizeof(Oot3dAnimationCurveHeader) == 0x10);
static_assert(sizeof(Oot3dAnimationCurveF32Key) == 0x10);
static_assert(sizeof(Oot3dAnimationCurveF32LinearKey) == 0x08);
static_assert(sizeof(Oot3dAnimationCurveS16Key) == 0x08);

float Oot3d_EvalAnimationCurveS16(
    const Oot3dAnimationCurveState* state, float frame);
float Oot3d_EvalAnimationCurveF32(
    const Oot3dAnimationCurveState* state, float frame);

} // namespace Oot3dSourceCsabCurve
"""


def make_source(source: str) -> str:
    helpers = checked_slice(source, HELPER_START, HELPER_END)
    s16 = checked_slice(source, S16_START, F32_START)
    f32 = checked_slice(source, F32_START, F32_END)
    # Target callsites 0x003086FC and 0x0030871C both branch to
    # Math_TanF @ 0x003555D8. The pinned semantic source still labels this
    # dependency as sinf, so keep the target-proven correction local to the
    # consumer import until the producer adopts it.
    tan_call = "return sinf(angle);"
    if helpers.count(tan_call) != 1:
        raise RuntimeError("expected one stale CSAB sinf dependency")
    helpers = helpers.replace(tan_call, "return tanf(angle);")
    stale_comment = "sin(raw * pi/65536)"
    if s16.count(stale_comment) != 1:
        raise RuntimeError("expected one stale CSAB tangent comment")
    s16 = s16.replace(stale_comment, "tan(raw * pi/65536)")
    return f"""#include "oot3d/csab_curve_eval.h"

#include <cmath>
#include <cstddef>

namespace Oot3dSourceCsabCurve {{

{helpers}

{s16}

{f32}

#undef OOT3D_CSAB_INLINE

}} // namespace Oot3dSourceCsabCurve
"""


def write_text(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=pathlib.Path)
    parser.add_argument("--header", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()

    source_bytes = args.source.read_bytes()
    digest = hashlib.sha256(source_bytes).hexdigest()
    if digest != EXPECTED_SOURCE_SHA256:
        raise RuntimeError(
            f"source hash mismatch: expected {EXPECTED_SOURCE_SHA256}, "
            f"got {digest}"
        )
    source = source_bytes.decode("utf-8")
    write_text(args.header, make_header())
    write_text(args.output, make_source(source))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
