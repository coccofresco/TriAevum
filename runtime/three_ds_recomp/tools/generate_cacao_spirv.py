#!/usr/bin/env python3
"""Generate the exact SPIR-V headers consumed by FidelityFX CACAO v1.2."""

import argparse
import pathlib
import re
import subprocess
import shutil
import struct


# FidelityFX v1.2 ffx_cacao_impl.cpp TEXTURES and load-counter descriptors.
# HLSL float/float2/float4 expresses arithmetic precision, not storage format.
# Leaving these unannotated makes DXC emit R32f/Rg32f/Rgba32f instead.
STORAGE_FORMATS = {
    "g_ClearLoadCounter_LoadCounter": "r32ui",
    "g_EdgeSensitiveBlur_Output": "rg8",
    "g_SSAOOutput": "rg8",
    "g_ApplyOutput": "r32f",
    **{f"g_PrepareDepthsAndMips_OutMip{i}": "r16f" for i in range(4)},
    "g_PrepareDepthsOut": "r16f",
    "g_PrepareNormals_NormalOut": "rgba8snorm",
    "g_ImportanceOut": "r8",
    "g_ImportanceAOut": "r8",
    "g_ImportanceBOut": "r8",
    "g_ImportanceBLoadCounter": "r32ui",
    "g_BilateralUpscaleOutput": "r32f",
}
SPIRV_IMAGE_FORMATS = {"r32ui": 33, "r32f": 3, "rg8": 13, "r8": 15, "r16f": 9, "rgba8snorm": 5}


def verify_storage_formats(binary: bytes) -> None:
    words = struct.unpack(f"<{len(binary) // 4}I", binary)
    if len(words) < 5 or words[0] != 0x07230203:
        raise ValueError("Not a SPIR-V module")
    names, pointers, images, variables = {}, {}, {}, []
    offset = 5
    while offset < len(words):
        count, opcode = words[offset] >> 16, words[offset] & 0xFFFF
        if count == 0 or offset + count > len(words):
            raise ValueError("Invalid SPIR-V instruction")
        args = words[offset + 1:offset + count]
        if opcode == 5:  # OpName
            names[args[0]] = struct.pack(f"<{len(args) - 1}I", *args[1:]).split(b"\0", 1)[0].decode()
        elif opcode == 25 and args[6] == 2:  # OpTypeImage, storage image
            images[args[0]] = args[7]
        elif opcode == 32:  # OpTypePointer
            pointers[args[0]] = args[2]
        elif opcode == 59:  # OpVariable
            variables.append((args[0], args[1]))
        offset += count
    checked = 0
    for pointer, variable in variables:
        image = pointers.get(pointer)
        if image not in images:
            continue
        name = names.get(variable)
        expected = SPIRV_IMAGE_FORMATS.get(STORAGE_FORMATS.get(name))
        if expected is None or images[image] != expected:
            raise ValueError(f"CACAO storage image format mismatch: {name}: {images[image]} != {expected}")
        checked += 1
    if checked == 0:
        raise ValueError("CACAO shader contains no verified storage image")


def annotate_storage_formats(source: str) -> str:
    seen = set()

    def annotate(match):
        name = match.group(1)
        if name not in STORAGE_FORMATS or name in seen:
            raise ValueError(f"Unexpected CACAO storage binding: {name}")
        seen.add(name)
        return f'[[vk::image_format("{STORAGE_FORMATS[name]}")]]\n' + match.group(0)

    result = re.sub(r"^RWTexture\w+<[^>]+>\s+(\w+)\s*:[^;]+;", annotate, source, flags=re.MULTILINE)
    if seen != STORAGE_FORMATS.keys():
        raise ValueError(f"Missing CACAO storage bindings: {STORAGE_FORMATS.keys() - seen}")
    # Modern DXC requires the optional Texture.Load offset to be a literal.
    # Folding it into integer texel coordinates preserves the exact lookup.
    for texture in ("g_DepthIn", "g_BilateralUpscaleDepth"):
        old_load = f"{texture}.Load(int3(coord, 0), offset)"
        if result.count(old_load) != 1:
            raise ValueError(f"Unexpected CACAO depth-load helper: {texture}")
        result = result.replace(old_load, f"{texture}.Load(int3(coord + offset, 0))")
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--dxc", required=True, type=pathlib.Path)
    args = parser.parse_args()

    impl = (args.source / "ffx_cacao_impl.cpp").read_text(encoding="latin-1")
    names = sorted(set(re.findall(
        r'PrecompiledShadersSPIRV/CACAO([A-Za-z0-9]+)_(?:16|32)\.h', impl)))
    if not names:
        raise RuntimeError("No CACAO SPIR-V shader declarations found")
    args.output.mkdir(parents=True, exist_ok=True)
    # Never modify the fetched donor. Local quoted includes resolve against
    # this generated source overlay before the original include directory.
    overlay = args.output.parent / "source"
    overlay.mkdir(parents=True, exist_ok=True)
    hlsl = overlay / "ffx_cacao.hlsl"
    shutil.copyfile(args.source / "ffx_cacao.hlsl", hlsl)
    bindings = (args.source / "ffx_cacao_bindings.hlsl").read_text(encoding="utf-8-sig")
    (overlay / "ffx_cacao_bindings.hlsl").write_text(annotate_storage_formats(bindings), encoding="utf-8")
    common = ["-Wno-conversion", "-Werror=unknown-attributes", "-Werror=ignored-attributes", "-spirv", "-T", "cs_6_2",
              "-fspv-target-env=vulkan1.1", "-fvk-s-shift", "0", "0",
              "-fvk-b-shift", "10", "0", "-fvk-t-shift", "20", "0",
              "-fvk-u-shift", "30", "0", "-I", str(args.source)]
    for bits in (16, 32):
        for name in names:
            output = args.output / f"CACAO{name}_{bits}.h"
            binary = output.with_suffix(".spv")
            command = [str(args.dxc), *common]
            if bits == 16:
                command.append("-enable-16bit-types")
            command += ["-Fo", str(binary), "-Fh", str(output), "-Vn", f"CS{name}SPIRV{bits}",
                        "-E", f"FFX_CACAO_{name}", str(hlsl)]
            subprocess.run(command, check=True)
            verify_storage_formats(binary.read_bytes())
    (args.output.parent / "cacao_spirv.stamp").write_text(
        f"generated {len(names) * 2} shaders\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
