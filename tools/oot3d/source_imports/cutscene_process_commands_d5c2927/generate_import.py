#!/usr/bin/env python3
"""Lower the pinned Cutscene_ProcessCommands body to target-width C++."""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import re


EXPECTED_SOURCE_SHA256 = (
    "f0b0c12ce7fd3da17d04a71059e911c91930796de495925d4e55d8eb3131cf52"
)
FUNCTION_PATTERN = re.compile(
    r"void Cutscene_ProcessCommands"
    r"\(int play,int csCtx,const undefined1 \*script\)\s*\n\s*\{"
)
LITERAL_PATTERN = re.compile(
    r"extern\s+(?P<type>uintptr_t \*|uintptr_t|float|int|uint|undefined4)"
    r"\s+(?P<name>DAT_[0-9a-f]{8});"
)
POINTER_CAST_PATTERN = re.compile(
    r"\((?P<base>undefined1|undefined2|undefined4|uint|int|ushort|short|"
    r"char|float|byte)\s*(?P<stars>\*{1,2})\)"
    r"\(uintptr_t\)(?:\(u32\))?"
)


def literal_expression(type_name: str, index: int) -> str:
    word = f"literal.Words[{index}]"
    if type_name == "float":
        return f"std::bit_cast<float>({word})"
    if type_name == "int":
        return f"static_cast<std::int32_t>({word})"
    if type_name == "uintptr_t *":
        return f"GuestPtr<uint>({word})"
    return word


FLOAT_LITERAL_NAMES = {
    "DAT_002c6554",
    "DAT_002c6558",
    "DAT_002c6970",
    "DAT_002c6974",
    "DAT_002c6984",
    "DAT_002c7a5c",
}


def pointer_type(base: str, stars: str) -> str:
    result = base
    for _ in stars:
        result = f"GuestPtr<{result}>"
    return result


def lower(source: str) -> str:
    match = FUNCTION_PATTERN.search(source)
    if match is None:
        raise RuntimeError("Cutscene_ProcessCommands definition not found")

    literals = list(LITERAL_PATTERN.finditer(source[: match.start()]))
    if len(literals) != 69:
        raise RuntimeError(f"expected 69 literal declarations, found {len(literals)}")

    body = source[match.start() :]
    body = FUNCTION_PATTERN.sub(
        "void Cutscene_ProcessCommandsImpl(\n"
        "    std::uint32_t play, std::uint32_t csCtx,\n"
        "    GuestPtr<const undefined1> script) {",
        body,
        count=1,
    )

    replacements = {
        "  int *piVar3;": "  GuestPtr<int> piVar3;",
        "  undefined1 *puVar4;": "  GuestPtr<undefined1> puVar4;",
        "  undefined4 *puVar9;": "  GuestPtr<undefined4> puVar9;",
        "  uint *puVar15;": "  GuestPtr<uint> puVar15;",
        "  uint *puVar18;": "  GuestPtr<uint> puVar18;",
        "  uint *puVar19;": "  GuestPtr<uint> puVar19;",
        "  int local_74;\n"
        "  undefined4 local_70;\n"
        "  undefined4 local_6c;\n"
        "  int local_68;\n"
        "  int local_64;\n"
        "  uint local_60;":
        "  std::array<std::uint32_t, 3> local_74_vector{};\n"
        "  auto& local_74 = local_74_vector[0];\n"
        "  auto& local_70 = local_74_vector[1];\n"
        "  auto& local_6c = local_74_vector[2];\n"
        "  std::array<std::uint32_t, 3> local_68_vector{};\n"
        "  auto& local_68 = local_68_vector[0];\n"
        "  auto& local_64 = local_68_vector[1];\n"
        "  auto& local_60 = local_68_vector[2];",
        "                      (**(code **)(uintptr_t)(u32)*puVar9)(puVar9,puVar18);":
        "                      InvokeDynamicCallback(puVar9, puVar18);",
        "puVar9 != (undefined4 *)(uintptr_t)(u32)0x0":
        "static_cast<bool>(puVar9)",
    }
    for original, replacement in replacements.items():
        if original not in body:
            raise RuntimeError(f"expected source fragment missing: {original!r}")
        body = body.replace(original, replacement)

    generic_handler_label = "default_oot3d_dup3:"
    if body.count(generic_handler_label) != 1:
        raise RuntimeError(
            "expected exactly one generic command handler label"
        )
    body = body.replace(
        generic_handler_label,
        "default:\n" + generic_handler_label,
        1,
    )

    target_abi_replacements = {
        "FUN_003665fc(0xe,1);": "FUN_003665fc(0xe,1,1);",
        "FUN_003665fc(0xf,0);": "FUN_003665fc(0xf,0,0);",
    }
    for original, replacement in target_abi_replacements.items():
        if body.count(original) != 1:
            raise RuntimeError(
                f"expected exactly one target ABI call site: {original!r}"
            )
        body = body.replace(original, replacement, 1)

    def replace_pointer_cast(cast: re.Match[str]) -> str:
        return pointer_type(cast.group("base"), cast.group("stars"))

    body = POINTER_CAST_PATTERN.sub(replace_pointer_cast, body)

    # These six stores are target float words in stack-backed Vec3f values.
    for name in ("local_68", "local_64", "local_60", "local_74", "local_70", "local_6c"):
        body = re.sub(
            rf"({name}\s*=\s*)VectorSignedToFloat\((.*?)\);",
            rf"\1std::bit_cast<std::uint32_t>(VectorSignedToFloat(\2));",
            body,
            flags=re.DOTALL,
        )

    macro_lines = []
    for index, literal_match in enumerate(literals):
        expression = (
            f"std::bit_cast<float>(literal.Words[{index}])"
            if literal_match.group("name") in FLOAT_LITERAL_NAMES
            else literal_expression(literal_match.group("type"), index)
        )
        macro_lines.append(
            f"#define {literal_match.group('name')} "
            f"({expression})"
        )

    undef_lines = [
        f"#undef {literal_match.group('name')}"
        for literal_match in reversed(literals)
    ]
    prologue = """// Generated by generate_import.py from the immutable d5c2927 package.
// The source hash and target body identity are recorded in import_manifest.json.
#include "oot3d/cutscene_process_commands_owner.h"

#include <array>
#include <bit>
#include <cstdint>

namespace Oot3dSourceCutsceneProcessCommands {
namespace {

"""
    literal_bindings = (
        "const auto& literal = ActiveLiterals();\n"
        + "\n".join(macro_lines)
        + "\n\n"
    )
    # Bind literals inside the implementation so no process-global host state
    # is created from guest addresses.
    opening = body.index("{", body.index("Cutscene_ProcessCommandsImpl")) + 1
    indented_literals = literal_bindings.rstrip().replace("\n", "\n  ")
    body = body[:opening] + "\n  " + indented_literals + "\n\n" + body[opening:]

    epilogue = """

""" + "\n".join(undef_lines) + """

} // namespace

void Cutscene_ProcessCommands(
    std::uint32_t playAddress,
    std::uint32_t cutsceneContextAddress,
    std::uint32_t scriptAddress) {
    Cutscene_ProcessCommandsImpl(
        playAddress, cutsceneContextAddress,
        GuestPtr<const undefined1>(scriptAddress));
}

} // namespace Oot3dSourceCutsceneProcessCommands
"""
    return prologue + body.rstrip() + epilogue


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()

    source_bytes = args.source.read_bytes()
    digest = hashlib.sha256(source_bytes).hexdigest()
    if digest != EXPECTED_SOURCE_SHA256:
        raise RuntimeError(
            f"source hash mismatch: expected {EXPECTED_SOURCE_SHA256}, got {digest}"
        )
    output = lower(source_bytes.decode("utf-8"))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
