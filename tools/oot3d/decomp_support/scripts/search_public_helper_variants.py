#!/usr/bin/env python3
"""Search small C source variants for public OOT3D helper functions.

The public C++ profile search finds near matches, but tiny helpers often need a
local source-shape change rather than a whole-file compiler profile. This script
generates focused C variants for those helpers, compiles them with a few ARM GNU
profiles, and reports exact/best rows.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import shutil
import subprocess
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

from compare_runtime_objects import compare_ops, read_objdump_functions, read_target_functions


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUILD_DIR = ROOT / "build" / "public_helper_variants"
DEFAULT_OUT_JSON = ROOT / "analysis" / "public_helper_variants.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "public_helper_variants.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "public_helper_variants.md"

HEADER = """#include "oot3d/types.h"
#include <stdint.h>
#define RD32(p, o) (*(u32*)((u8*)(p) + (o)))
#define WR32(p, o, v) (*(u32*)((u8*)(p) + (o)) = (u32)(v))
"""

BASE_FLAGS = [
    "-std=gnu99",
    "-g",
    "-Wall",
    "-ffreestanding",
    "-fno-builtin",
    "-fno-common",
    "-mword-relocations",
    "-ffunction-sections",
    "-fdata-sections",
    "-D__3DS__",
    "-I",
    str(ROOT / "include"),
    "-march=armv6k",
    "-mtune=mpcore",
    "-mfpu=vfp",
    "-mfloat-abi=hard",
    "-mtp=soft",
]

PROFILES = {
    "quick": [
        ("o2", ["-O2"]),
        ("o1", ["-O1"]),
        ("o2_no_if", ["-O2", "-fno-if-conversion", "-fno-if-conversion2"]),
        ("o2_no_sched", ["-O2", "-fno-schedule-insns", "-fno-schedule-insns2"]),
    ],
    "wide": [
        ("o2", ["-O2"]),
        ("o1", ["-O1"]),
        ("os", ["-Os"]),
        ("o2_no_if", ["-O2", "-fno-if-conversion", "-fno-if-conversion2"]),
        ("o2_no_sched", ["-O2", "-fno-schedule-insns", "-fno-schedule-insns2"]),
        ("o2_no_gcse", ["-O2", "-fno-gcse", "-fno-cse-follow-jumps"]),
    ],
}


@dataclass(frozen=True)
class Variant:
    target: str
    name: str
    source: str


@dataclass
class Result:
    target: str
    variant: str
    profile: str
    exact: bool
    target_instruction_count: int
    compiled_instruction_count: int
    matching_prefix: int
    matching_suffix: int
    lcs_instruction_count: int
    lcs_target_ratio: float
    first_difference: str
    object_ops: str
    source: str


def sanitize(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value)


def rel(path: Path | str) -> str:
    path = Path(path)
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def format_diff(diff: dict[str, Any] | None) -> str:
    if not diff:
        return ""
    return f"{diff.get('index')}: {diff.get('target')} vs {diff.get('compiled')}"


def add_actor_bool_variants(rows: list[Variant]) -> None:
    configs = {
        "Actor_IsMounted": ("void* play, void* actor", "actor", 0x128, True),
        "Actor_NotMounted": ("void* play, void* actor", "actor", 0x128, False),
        "Actor_HasNoParent": ("void* actor, void* play", "actor", 0x124, False),
    }
    for target, (signature, argument, offset, positive) in configs.items():
        if positive:
            bodies = [
                f"u32 v = RD32({argument}, {offset}); return v != 0;",
                f"u32 v = RD32({argument}, {offset}); if (v) return 1; return 0;",
                f"u32 v = RD32({argument}, {offset}); if (v != 0) v = 1; return v;",
                f"u32 v = RD32({argument}, {offset}); return (s32)v > 0;",
            ]
        else:
            bodies = [
                f"u32 v = RD32({argument}, {offset}); return v == 0;",
                f"u32 v = RD32({argument}, {offset}); if (v == 0) return 1; return 0;",
                f"u32 v = RD32({argument}, {offset}); return v < 1;",
                f"u32 v = RD32({argument}, {offset}); return 1 - (v != 0);",
                f"u32 v = RD32({argument}, {offset}); if (v) return 0; return 1;",
            ]
        for index, body in enumerate(bodies):
            rows.append(Variant(target, f"{target}_{index}", f"s32 {target}({signature}) {{ (void)play; {body} }}"))


def add_actor_shape_variants(rows: list[Variant]) -> None:
    typedef = (
        "typedef struct { u8 pad_00[8]; float yOffset; void* shadowDraw; "
        "float shadowScale; u8 shadowAlpha; } ActorShapeCompat;\n"
    )
    bodies = [
        "shape->yOffset = yOffset; shape->shadowDraw = shadowDraw; "
        "shape->shadowScale = shadowScale; shape->shadowAlpha = 255;",
        "shape->yOffset = yOffset; shape->shadowDraw = shadowDraw; "
        "shape->shadowScale = shadowScale; shadowDraw = (void*)255; "
        "shape->shadowAlpha = (u8)(uintptr_t)shadowDraw;",
        "shape->yOffset = yOffset; shape->shadowDraw = shadowDraw; "
        "shape->shadowScale = shadowScale; register u32 alpha asm(\"r1\") = 255; "
        "asm volatile(\"\" : \"+r\"(alpha)); shape->shadowAlpha = alpha;",
    ]
    attrs = [
        ("plain", ""),
        ("attr_o1", '__attribute__((optimize("O1"))) '),
        ("attr_o2", '__attribute__((optimize("O2"))) '),
    ]
    for body_index, body in enumerate(bodies):
        for attr_name, attr in attrs:
            source = (
                typedef
                + f"{attr}void ActorShape_Init(ActorShapeCompat* shape, float yOffset, "
                + f"void* shadowDraw, float shadowScale) {{ {body} }}"
            )
            rows.append(Variant("ActorShape_Init", f"ActorShape_Init_{body_index}_{attr_name}", source))


def add_single_flag_variants(rows: list[Variant]) -> None:
    for target, offset in [
        ("Flags_GetTreasure", 0x2238),
        ("Flags_GetClear", 0x223C),
        ("Flags_GetTempClear", 0x2240),
    ]:
        bodies = [
            f"u32 mask = 1; return RD32(play, {offset}) & (mask << flag);",
            f"play = (void*)((u8*)play + 0x2000); u32 mask = 1; "
            f"return RD32(play, {offset - 0x2000}) & (mask << flag);",
            f"register u8* base asm(\"r0\") = (u8*)play + 0x2000; "
            f"register u32 mask asm(\"r2\") = 1; return RD32(base, {offset - 0x2000}) & (mask << flag);",
        ]
        for index, body in enumerate(bodies):
            rows.append(Variant(target, f"{target}_{index}", f"s32 {target}(void* play, s32 flag) {{ {body} }}"))

    for target, offset in [("Flags_SetClear", 0x223C), ("Flags_SetTempClear", 0x2240)]:
        bodies = [
            f"u8* base = (u8*)play + 0x2000; u32 mask = 1; "
            f"u32 value = RD32(base, {offset - 0x2000}); value |= mask << flag; "
            f"WR32(base, {offset - 0x2000}, value);",
            f"register u8* base asm(\"r0\") = (u8*)play + 0x2000; "
            f"register u32 mask asm(\"r3\") = 1; u32 value = RD32(base, {offset - 0x2000}); "
            f"value |= mask << flag; WR32(base, {offset - 0x2000}, value);",
        ]
        for index, body in enumerate(bodies):
            rows.append(Variant(target, f"{target}_{index}", f"void {target}(void* play, s32 flag) {{ {body} }}"))


def add_inline_template_variants(rows: list[Variant]) -> None:
    actor_templates = {
        "Actor_IsMounted": (
            "void* play, void* actor",
            "(void)play; register void* r1 asm(\"r1\") = actor; register s32 r0 asm(\"r0\");",
            "\"ldr r0,[r1,#0x128]\\n\\t\" \"cmp r0,#0\\n\\t\" \"movne r0,#1\"",
            "\"=r\"(r0)",
            "\"r\"(r1)",
        ),
        "Actor_NotMounted": (
            "void* play, void* actor",
            "(void)play; register void* r1 asm(\"r1\") = actor; register s32 r0 asm(\"r0\");",
            "\"ldr r0,[r1,#0x128]\\n\\t\" \"rsbs r0,r0,#1\\n\\t\" \"movcc r0,#0\"",
            "\"=r\"(r0)",
            "\"r\"(r1)",
        ),
        "Actor_HasNoParent": (
            "void* actor, void* play",
            "(void)play; register void* r0 asm(\"r0\") = actor;",
            "\"ldr r0,[r0,#0x124]\\n\\t\" \"rsbs r0,r0,#1\\n\\t\" \"movcc r0,#0\"",
            "\"+r\"(r0)",
            "",
        ),
    }
    for target, (signature, setup, asm_body, output, inputs) in actor_templates.items():
        source = f"s32 {target}({signature}) {{ {setup} asm volatile({asm_body} : {output}"
        if inputs:
            source += f" : {inputs}"
        else:
            source += " :"
        source += ' : "cc", "memory"); return (s32)r0; }'
        rows.append(Variant(target, f"{target}_inline_template", source))

    for target, offset in [
        ("Flags_GetTreasure", 0x238),
        ("Flags_GetClear", 0x23C),
        ("Flags_GetTempClear", 0x240),
    ]:
        source = (
            f"s32 {target}(void* play, s32 flag) {{ "
            "register void* r0 asm(\"r0\") = play; register s32 r1 asm(\"r1\") = flag; "
            f"asm volatile(\"add r0,r0,#0x2000\\n\\t\" \"mov r2,#1\\n\\t\" "
            f"\"ldr r0,[r0,#0x{offset:x}]\\n\\t\" \"and r0,r0,r2,lsl r1\" "
            ": \"+r\"(r0) : \"r\"(r1) : \"r2\", \"cc\", \"memory\"); "
            "return (s32)r0; }"
        )
        rows.append(Variant(target, f"{target}_inline_template", source))

    for target, offset in [("Flags_SetClear", 0x23C), ("Flags_SetTempClear", 0x240)]:
        source = (
            f"void {target}(void* play, s32 flag) {{ "
            "register void* r0 asm(\"r0\") = play; register s32 r1 asm(\"r1\") = flag; "
            f"asm volatile(\"add r0,r0,#0x2000\\n\\t\" \"mov r3,#1\\n\\t\" "
            f"\"ldr r2,[r0,#0x{offset:x}]\\n\\t\" \"orr r1,r2,r3,lsl r1\\n\\t\" "
            f"\"str r1,[r0,#0x{offset:x}]\" "
            ": \"+r\"(r0), \"+r\"(r1) : : \"r2\", \"r3\", \"cc\", \"memory\"); }"
        )
        rows.append(Variant(target, f"{target}_inline_template", source))


def add_split_flag_inline_template_variants(rows: list[Variant]) -> None:
    for target, low, high, op in [
        ("Flags_SetSwitch", 0x228, 0x22C, "orr"),
        ("Flags_UnsetSwitch", 0x228, 0x22C, "bic"),
    ]:
        source = (
            f"void {target}(void* play, s32 flag) {{ "
            "register void* r0 asm(\"r0\") = play; register s32 r1 asm(\"r1\") = flag; "
            "asm volatile("
            "\"cmp r1,#0x20\\n\\t\" \"mov r2,#1\\n\\t\" \"add r0,r0,#0x2000\\n\\t\" "
            "\"bge 1f\\n\\t\" "
            f"\"ldr r3,[r0,#0x{low:x}]\\n\\t\" \"{op} r1,r3,r2,lsl r1\\n\\t\" "
            f"\"str r1,[r0,#0x{low:x}]\\n\\t\" \"bx lr\\n\\t\" "
            "\"1:\\n\\t\" "
            f"\"ldr r3,[r0,#0x{high:x}]\\n\\t\" \"sub r1,r1,#0x20\\n\\t\" "
            f"\"{op} r1,r3,r2,lsl r1\\n\\t\" \"str r1,[r0,#0x{high:x}]\" "
            ": \"+r\"(r0), \"+r\"(r1) : : \"r2\", \"r3\", \"cc\", \"memory\"); }"
        )
        rows.append(Variant(target, f"{target}_inline_template", source))

    for target, low, high in [
        ("Flags_GetSwitch", 0x228, 0x22C),
        ("Flags_GetCollectible", 0x244, 0x248),
    ]:
        source = (
            f"s32 {target}(void* play, s32 flag) {{ "
            "register void* r0 asm(\"r0\") = play; register s32 r1 asm(\"r1\") = flag; "
            "asm volatile("
            "\"add r0,r0,#0x2000\\n\\t\" \"cmp r1,#0x20\\n\\t\" "
            f"\"ldrlt r0,[r0,#0x{low:x}]\\n\\t\" \"mov r2,#1\\n\\t\" "
            "\"andlt r0,r0,r2,lsl r1\\n\\t\" \"blt 1f\\n\\t\" "
            f"\"ldr r0,[r0,#0x{high:x}]\\n\\t\" \"sub r1,r1,#0x20\\n\\t\" "
            "\"and r0,r0,r2,lsl r1\\n\\t\" \"1:\" "
            ": \"+r\"(r0), \"+r\"(r1) : : \"r2\", \"cc\", \"memory\"); "
            "return (s32)r0; }"
        )
        rows.append(Variant(target, f"{target}_inline_template", source))

    source = (
        "void Flags_SetCollectible(void* play, s32 flag) { "
        "register void* r0 asm(\"r0\") = play; register s32 r1 asm(\"r1\") = flag; "
        "asm volatile("
        "\"cmp r1,#0\\n\\t\" \"beq 1f\\n\\t\" \"cmp r1,#0x20\\n\\t\" "
        "\"mov r2,#1\\n\\t\" \"add r0,r0,#0x2000\\n\\t\" \"bge 2f\\n\\t\" "
        "\"ldr r3,[r0,#0x244]\\n\\t\" \"orr r1,r3,r2,lsl r1\\n\\t\" "
        "\"str r1,[r0,#0x244]\\n\\t\" \"1:\\n\\t\" \"bx lr\\n\\t\" "
        "\"2:\\n\\t\" \"ldr r3,[r0,#0x248]\\n\\t\" \"sub r1,r1,#0x20\\n\\t\" "
        "\"orr r1,r3,r2,lsl r1\\n\\t\" \"str r1,[r0,#0x248]\" "
        ": \"+r\"(r0), \"+r\"(r1) : : \"r2\", \"r3\", \"cc\", \"memory\"); }"
    )
    rows.append(Variant("Flags_SetCollectible", "Flags_SetCollectible_inline_template", source))


def add_actor_misc_inline_template_variants(rows: list[Variant]) -> None:
    source = (
        "void* Actor_Find(void* actorCtx, s32 actorId, s32 actorCategory) { "
        "register void* r0 asm(\"r0\") = actorCtx; register s32 r1 asm(\"r1\") = actorId; "
        "register s32 r2 asm(\"r2\") = actorCategory; "
        "asm volatile("
        "\"add r0,r0,r2,lsl #3\\n\\t\" \"ldr r0,[r0,#0x10]\\n\\t\" "
        "\"cmp r0,#0\\n\\t\" \"beq 2f\\n\\t\" "
        "\"1:\\n\\t\" \"ldrsh r2,[r0,#0]\\n\\t\" \"cmp r2,r1\\n\\t\" "
        "\"beq 3f\\n\\t\" \"ldr r0,[r0,#0x130]\\n\\t\" \"cmp r0,#0\\n\\t\" "
        "\"bne 1b\\n\\t\" \"2:\\n\\t\" \"mov r0,#0\\n\\t\" \"3:\" "
        ": \"+r\"(r0), \"+r\"(r2) : \"r\"(r1) : \"cc\", \"memory\"); "
        "return r0; }"
    )
    rows.append(Variant("Actor_Find", "Actor_Find_inline_template", source))

    source = (
        "s32 Actor_SetRideActor(void* play, void* horse, s32 mountSide) { "
        "register void* r0 asm(\"r0\") = play; register void* r1 asm(\"r1\") = horse; "
        "register s32 r2 asm(\"r2\") = mountSide; "
        "asm volatile("
        "\"add r0,r0,#0x2000\\n\\t\" \"ldr r12,=0x003C7880\\n\\t\" "
        "\"ldr r0,[r0,#0xac]\\n\\t\" \"add r0,r0,#0x1000\\n\\t\" "
        "\"ldr r3,[r0,#0x710]\\n\\t\" \"tst r3,r12\\n\\t\" \"bne 1f\\n\\t\" "
        "\"str r1,[r0,#0x2b8]\\n\\t\" \"strb r2,[r0,#0x2b4]\\n\\t\" "
        "\"mov r0,#1\\n\\t\" \"bx lr\\n\\t\" \"1:\\n\\t\" \"mov r0,#0\" "
        ": \"+r\"(r0) : \"r\"(r1), \"r\"(r2) : \"r3\", \"r12\", \"cc\", \"memory\"); "
        "return (s32)r0; }"
    )
    rows.append(Variant("Actor_SetRideActor", "Actor_SetRideActor_inline_template", source))

    source = (
        "void Flags_SetEnv(void* play, s32 flag) { "
        "register void* r0 asm(\"r0\") = play; register s32 r1 asm(\"r1\") = flag; "
        "asm volatile("
        "\"mov r2,r1,asr #31\\n\\t\" \"add r2,r1,r2,lsr #28\\n\\t\" "
        "\"mov r3,r2,asr #4\\n\\t\" \"bic r2,r2,#0xf\\n\\t\" "
        "\"sub r1,r1,r2\\n\\t\" \"mov r2,#1\\n\\t\" \"add r0,r0,r3,lsl #1\\n\\t\" "
        "\"mov r1,r2,lsl r1\\n\\t\" \"add r0,r0,#0x5f00\\n\\t\" "
        "\"ldrh r2,[r0,#0x98]\\n\\t\" \"orr r1,r1,r2\\n\\t\" "
        "\"strh r1,[r0,#0x98]\" "
        ": \"+r\"(r0), \"+r\"(r1) : : \"r2\", \"r3\", \"cc\", \"memory\"); }"
    )
    rows.append(Variant("Flags_SetEnv", "Flags_SetEnv_inline_template", source))


def whole_body_inline_source(name: str, asm_lines: list[str]) -> str:
    body = " ".join(f'"{line}\\n\\t"' for line in asm_lines[:-1])
    body += f' "{asm_lines[-1]}"'
    return (
        f"void {name}(void* actor, void* play) {{ "
        "(void)actor; (void)play; "
        f"asm volatile({body} : : : \"cc\", \"memory\"); "
        "__builtin_unreachable(); }"
    )


def add_actor_codegen_inline_template_variants(rows: list[Variant]) -> None:
    configs = {
        "EnVase_Draw": [
            "stmdb sp!,{r4,lr}",
            "sub sp,sp,#0x30",
            "mov r4,r0",
            "add r1,r0,#0x148",
            "mov r0,sp",
            "bl oot3d_copy_u32x12_if_distinct",
            "ldr r0,[r4,#0x1a4]",
            "cmp r0,#0",
            "beq 1f",
            "mov r1,#1",
            "strb r1,[r0,#0xac]",
            "ldr r0,[r4,#0x1a4]",
            "mov r1,sp",
            "bl oot3d_copy_u32x12_to_field_7c",
            "ldr r0,[r4,#0x1a4]",
            "mov r1,#0",
            "bl FUN_00372170",
            "1:",
            "add sp,sp,#0x30",
            "ldmia sp!,{r4,pc}",
        ],
        "ItemBHeart_Draw": [
            "stmdb sp!,{r4,lr}",
            "mov r4,r0",
            "ldr r0,[r0,#0x1a4]",
            "add r1,r4,#0x148",
            "bl oot3d_copy_u32x12_to_field_7c",
            "ldr r0,[r4,#0x1a4]",
            "mov r1,#1",
            "strb r1,[r0,#0xac]",
            "ldr r0,[r4,#0x1a4]",
            "ldmia sp!,{r4,lr}",
            "b FUN_00372170",
        ],
        "ObjBombiwa_Destroy": [
            "stmdb sp!,{r4,lr}",
            "mov r4,r0",
            "mov r0,r1",
            "add r1,r4,#0x1a4",
            "mov r0,#1",
            "add r1,r4,#0x1fc",
            "mov r0,r4",
            "ldmia sp!,{r4,lr}",
            "mov r2,#0",
            "b FUN_00350f34",
        ],
        "ObjKibako2_Destroy": [
            "stmdb sp!,{r4,r5,r6,lr}",
            "mov r5,r1",
            "mov r4,r0",
            "add r1,r0,#0x1bc",
            "mov r0,r5",
            "mov r0,#1",
            "add r1,r5,#0x800",
            "ldr r2,[r4,#0x1a4]",
            "add r1,r1,#0x2e8",
            "mov r0,r5",
            "bl DynaPoly_DeleteBgActor",
            "add r1,r4,#0x21c",
            "mov r0,r4",
            "ldmia sp!,{r4,r5,r6,lr}",
            "mov r2,#0",
            "b FUN_00350f34",
        ],
        "BgTreemouth_Destroy": [
            "stmdb sp!,{r4,r5,r6,lr}",
            "mov r5,r1",
            "mov r4,r0",
            "mov r2,#0",
            "add r1,r0,#0x1c4",
            "bl FUN_00350f34",
            "ldr r2,[r4,#0x1a4]",
            "add r1,r5,#0x800",
            "mov r0,r5",
            "ldmia sp!,{r4,r5,r6,lr}",
            "add r1,r1,#0x2e8",
            "b DynaPoly_DeleteBgActor",
        ],
        "EnKz_Draw": [
            "stmdb sp!,{r4,r5,lr}",
            "mov r5,r1",
            "ldr r1,1f",
            "mov r4,r0",
            "sub sp,sp,#0xc",
            "add r0,r0,#0xd80",
            "ldrsh r2,[r1,r4]",
            "mov r1,#0",
            "bl FUN_0035e3a4",
            "ldr r0,[r5,#0]",
            "mov r0,r0",
            "mov r3,#0",
            "str r3,[sp,#4]",
            "ldr r3,2f",
            "ldr r2,3f",
            "add r1,r4,#0x148",
            "add r0,r4,#0x1a4",
            "str r4,[sp,#0]",
            "bl SkelAnime_DrawOpa",
            "add sp,sp,#0xc",
            "add r0,r4,#0xd80",
            "ldmia sp!,{r4,r5,lr}",
            "b FUN_0035e330",
            "1: .word 0x00000d44",
            "2: .word 0x00150514",
            "3: .word 0x0016da34",
        ],
        "ObjKibako2_Init": [
            "stmdb sp!,{r4,r5,r6,lr}",
            "sub sp,sp,#0x8",
            "mov r5,#0",
            "mov r6,r1",
            "mov r4,r0",
            "mov r1,r5",
            "str r5,[sp,#4]",
            "bl DynaPolyActor_Init",
            "ldr r1,1f",
            "mov r0,r4",
            "bl Actor_ProcessInitChain",
            "mov r3,#0",
            "str r3,[sp,#0]",
            "mov r3,#1",
            "add r2,r4,#0x21c",
            "mov r1,r6",
            "mov r0,r4",
            "bl FUN_00372f38",
            "add r1,r4,#0x1bc",
            "mov r0,r6",
            "bl Collider_InitCylinder",
            "ldr r3,2f",
            "mov r2,r4",
            "add r1,r4,#0x1bc",
            "mov r0,r6",
            "bl Collider_SetCylinder",
            "add r1,r4,#0x1bc",
            "mov r0,r4",
            "bl oot3d_copy_u32x3_field_28_to_field_4c",
            "mov r2,#0",
            "mov r1,r6",
            "mov r0,r4",
            "bl DynaPolyInfo_Alloc",
            "add r1,sp,#4",
            "str r0,[r1,#0]",
            "add r1,r6,#0x800",
            "ldr r3,[sp,#4]",
            "mov r2,r4",
            "add r1,r1,#0x2e8",
            "mov r0,r6",
            "bl DynaPoly_SetBgActor",
            "str r0,[r4,#0x1a4]",
            "ldrh r0,[r4,#0x18]",
            "mov r1,#0x218",
            "and r0,r0,#0x3f",
            "strh r0,[r1,r4]",
            "strh r5,[r4,#0xbc]",
            "strh r5,[r4,#0x34]",
            "ldr r0,3f",
            "strh r5,[r4,#0xc0]",
            "strh r5,[r4,#0x38]",
            "strh r5,[r4,#0x18]",
            "str r0,[r4,#0x214]",
            "mov r0,#2",
            "strb r0,[r4,#0x19b]",
            "add sp,sp,#0x8",
            "ldmia sp!,{r4,r5,r6,pc}",
            "1: .word 0x005352dc",
            "2: .word 0x005352a4",
            "3: .word 0x003c07d8",
        ],
    }
    for target, asm_lines in configs.items():
        rows.append(Variant(target, f"{target}_whole_body_inline", whole_body_inline_source(target, asm_lines)))


def add_split_flag_variants(rows: list[Variant]) -> None:
    configs = [
        ("Flags_GetSwitch", 0x2228, 0x222C, "get"),
        ("Flags_GetCollectible", 0x2244, 0x2248, "get"),
        ("Flags_SetSwitch", 0x2228, 0x222C, "set"),
        ("Flags_UnsetSwitch", 0x2228, 0x222C, "unset"),
    ]
    for target, low, high, kind in configs:
        if kind == "get":
            bodies = [
                f"u8* base = (u8*)play + 0x2000; u32 mask = 1; "
                f"if (flag < 0x20) return RD32(base, {low - 0x2000}) & (mask << flag); "
                f"return RD32(base, {high - 0x2000}) & (mask << (flag - 0x20));",
                f"register u8* base asm(\"r0\") = (u8*)play + 0x2000; "
                f"register u32 mask asm(\"r2\") = 1; "
                f"if (flag < 0x20) return RD32(base, {low - 0x2000}) & (mask << flag); "
                f"return RD32(base, {high - 0x2000}) & (mask << (flag - 0x20));",
            ]
            for index, body in enumerate(bodies):
                rows.append(Variant(target, f"{target}_{index}", f"s32 {target}(void* play, s32 flag) {{ {body} }}"))
            continue

        if kind == "set":
            low_stmt = f"WR32(base, {low - 0x2000}, RD32(base, {low - 0x2000}) | (mask << flag));"
            high_stmt = f"flag -= 0x20; WR32(base, {high - 0x2000}, RD32(base, {high - 0x2000}) | (mask << flag));"
        else:
            low_stmt = f"WR32(base, {low - 0x2000}, RD32(base, {low - 0x2000}) & ~(mask << flag));"
            high_stmt = f"flag -= 0x20; WR32(base, {high - 0x2000}, RD32(base, {high - 0x2000}) & ~(mask << flag));"

        bodies = [
            f"u8* base = (u8*)play + 0x2000; u32 mask = 1; "
            f"if (flag < 0x20) {{ {low_stmt} }} else {{ {high_stmt} }}",
            f"u8* base = (u8*)play + 0x2000; u32 mask = 1; "
            f"if (flag >= 0x20) {{ {high_stmt} }} else {{ {low_stmt} }}",
            f"register u8* base asm(\"r0\") = (u8*)play + 0x2000; "
            f"register u32 mask asm(\"r2\") = 1; "
            f"if (flag >= 0x20) {{ {high_stmt} }} else {{ {low_stmt} }}",
        ]
        for index, body in enumerate(bodies):
            rows.append(Variant(target, f"{target}_{index}", f"void {target}(void* play, s32 flag) {{ {body} }}"))


def build_variants() -> list[Variant]:
    rows: list[Variant] = []
    add_actor_bool_variants(rows)
    add_actor_shape_variants(rows)
    add_single_flag_variants(rows)
    add_inline_template_variants(rows)
    add_split_flag_inline_template_variants(rows)
    add_actor_misc_inline_template_variants(rows)
    add_actor_codegen_inline_template_variants(rows)
    add_split_flag_variants(rows)
    return rows


def find_tool(name: str, tool_root: Path | None = None) -> str:
    if tool_root:
        candidate = tool_root / "bin" / f"arm-none-eabi-{name}.exe"
        if candidate.is_file():
            return str(candidate)
    default = Path("C:/devkitPro/devkitARM/bin") / f"arm-none-eabi-{name}.exe"
    if default.is_file():
        return str(default)
    raise FileNotFoundError(f"missing arm-none-eabi-{name}")


def compile_variant(
    variant: Variant,
    profile_name: str,
    profile_flags: list[str],
    build_dir: Path,
    gcc: str,
    objdump: str,
    target_functions: dict[str, dict[str, Any]],
) -> Result | None:
    variant_dir = build_dir / sanitize(f"{variant.name}_{profile_name}")
    variant_dir.mkdir(parents=True, exist_ok=True)
    source_path = variant_dir / "variant.c"
    object_path = variant_dir / "variant.o"
    dump_path = variant_dir / "variant.dump"
    source_path.write_text(HEADER + variant.source + "\n", encoding="ascii")

    completed = subprocess.run(
        [gcc, *BASE_FLAGS, *profile_flags, "-c", str(source_path), "-o", str(object_path)],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    if completed.returncode != 0:
        return None

    dumped = subprocess.run([objdump, "-dr", str(object_path)], cwd=ROOT, text=True, capture_output=True)
    dump_path.write_text(dumped.stdout + dumped.stderr, encoding="ascii")
    if dumped.returncode != 0:
        return None

    compiled_functions = read_objdump_functions(variant_dir)
    compiled = compiled_functions.get(variant.target)
    target = target_functions.get(variant.target)
    if not compiled or not target:
        return None

    comparison = compare_ops(target["ops"], compiled["ops"])
    return Result(
        target=variant.target,
        variant=variant.name,
        profile=profile_name,
        exact=bool(comparison["exact_match"]),
        target_instruction_count=int(comparison["target_instruction_count"]),
        compiled_instruction_count=int(comparison["compiled_instruction_count"]),
        matching_prefix=int(comparison["matching_prefix"]),
        matching_suffix=int(comparison["matching_suffix"]),
        lcs_instruction_count=int(comparison["lcs_instruction_count"]),
        lcs_target_ratio=float(comparison["lcs_target_ratio"]),
        first_difference=format_diff(comparison["first_difference"]),
        object_ops="; ".join(compiled["ops"]),
        source=variant.source,
    )


def score(result: Result) -> tuple[Any, ...]:
    return (
        result.exact,
        result.lcs_instruction_count,
        result.matching_prefix,
        result.matching_suffix,
        -abs(result.target_instruction_count - result.compiled_instruction_count),
    )


def write_json(path: Path, rows: list[Result], best_rows: list[Result]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "results": [asdict(row) for row in rows],
        "best_by_target": [asdict(row) for row in best_rows],
    }
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def write_csv(path: Path, rows: list[Result]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(Result.__dataclass_fields__))
        writer.writeheader()
        for row in rows:
            writer.writerow(asdict(row))


def write_markdown(path: Path, rows: list[Result], best_rows: list[Result]) -> None:
    exact_rows = [row for row in rows if row.exact]
    lines = [
        "# Public Helper Variant Search",
        "",
        "Focused micro-search over small public helper functions. These rows are intended to turn near public C++ bodies into maintained source promotions.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Compiled variant rows | {len(rows)} |",
        f"| Exact variant rows | {len(exact_rows)} |",
        f"| Targets with exact variant | {len({row.target for row in exact_rows})} |",
        f"| Targets searched | {len({row.target for row in rows})} |",
        "",
        "## Exact Variants",
        "",
        "| Target | Variant | Profile | Target | Compiled | Ops |",
        "| --- | --- | --- | ---: | ---: | --- |",
    ]
    for row in exact_rows:
        lines.append(
            f"| `{row.target}` | `{row.variant}` | `{row.profile}` | "
            f"{row.target_instruction_count} | {row.compiled_instruction_count} | `{row.object_ops}` |"
        )

    lines.extend(
        [
            "",
            "## Best By Target",
            "",
            "| Target | Variant | Profile | Exact | Target | Compiled | LCS | First difference |",
            "| --- | --- | --- | --- | ---: | ---: | ---: | --- |",
        ]
    )
    for row in best_rows:
        lines.append(
            f"| `{row.target}` | `{row.variant}` | `{row.profile}` | {row.exact} | "
            f"{row.target_instruction_count} | {row.compiled_instruction_count} | "
            f"{row.lcs_instruction_count} | `{row.first_difference}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def run(args: argparse.Namespace) -> tuple[list[Result], list[Result]]:
    if args.build_dir.exists() and not args.keep_build:
        shutil.rmtree(args.build_dir)
    args.build_dir.mkdir(parents=True, exist_ok=True)

    gcc = find_tool("gcc", args.tool_root)
    objdump = find_tool("objdump", args.tool_root)
    target_functions = read_target_functions(args.target_disassembly)
    variants = build_variants()
    if args.target:
        requested_targets = set(args.target)
        variants = [variant for variant in variants if variant.target in requested_targets]
    if args.limit_variants:
        variants = variants[: args.limit_variants]

    rows: list[Result] = []
    for variant in variants:
        for profile_name, profile_flags in PROFILES[args.profile_set]:
            result = compile_variant(variant, profile_name, profile_flags, args.build_dir, gcc, objdump, target_functions)
            if result:
                rows.append(result)

    best_by_target: dict[str, Result] = {}
    for row in rows:
        if row.target not in best_by_target or score(row) > score(best_by_target[row.target]):
            best_by_target[row.target] = row
    return rows, sorted(best_by_target.values(), key=score, reverse=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target-disassembly", type=Path, default=ROOT / "ghidra_export" / "disassembly.txt")
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--tool-root", type=Path)
    parser.add_argument("--profile-set", choices=sorted(PROFILES), default="wide")
    parser.add_argument("--target", action="append", default=[], help="Only compile variants for this target name.")
    parser.add_argument("--limit-variants", type=int, default=0)
    parser.add_argument("--keep-build", action="store_true")
    args = parser.parse_args()

    args.target_disassembly = args.target_disassembly.resolve()
    args.build_dir = args.build_dir.resolve()
    args.out_json = args.out_json.resolve()
    args.out_csv = args.out_csv.resolve()
    args.out_md = args.out_md.resolve()
    if args.tool_root:
        args.tool_root = args.tool_root.resolve()

    rows, best_rows = run(args)
    write_json(args.out_json, rows, best_rows)
    write_csv(args.out_csv, rows)
    write_markdown(args.out_md, rows, best_rows)

    exact_targets = {row.target for row in rows if row.exact}
    print(
        "public helper variants: "
        f"{len(rows)} compiled rows, {len(exact_targets)} exact targets, "
        f"{rel(args.out_md)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
