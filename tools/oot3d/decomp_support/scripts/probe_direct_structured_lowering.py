#!/usr/bin/env python3
"""Probe target-shaped structured lowering variants for open frontier rows."""

from __future__ import annotations

import argparse
import csv
import json
import os
import subprocess
from pathlib import Path
from typing import Any

from compare_direct_packet_probe_matches import read_objdump_file
from compare_runtime_objects import compare_ops, read_target_functions
from probe_direct_packet_compilability import compile_command, find_tool, probe_prelude, rel


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUILD = ROOT / "build" / "direct_structured_lowering_probes"
DEFAULT_TARGET_DISASM = ROOT / "ghidra_export" / "disassembly.txt"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_structured_lowering_probe.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_structured_lowering_probe.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_structured_lowering_probe.md"


BOSS_VA_ZAPPER_INTRO_PREFIX = r"""
extern volatile s8 oot3d_boss_va_cs_state_bytes[16];

#define OOT3D_S8_AT(base, offset) (*(volatile s8*)((u8*)(base) + (offset)))
#define OOT3D_S16_AT(base, offset) (*(s16*)((u8*)(base) + (offset)))
#define OOT3D_U16_AT(base, offset) (*(u16*)((u8*)(base) + (offset)))
#define OOT3D_U32_AT(base, offset) (*(u32*)((u8*)(base) + (offset)))
#define OOT3D_PTR_AT(base, offset) (*(void**)((u8*)(base) + (offset)))
#define OOT3D_ANIM(base) ((void*)((u8*)(base) + 0x1A4))
#define OOT3D_REG_BARRIER(value) ({ s32 _oot3d_v = (value); __asm__ volatile("" : "+r"(_oot3d_v)); _oot3d_v; })
#define OOT3D_PTR_BARRIER(value) ({ void* _oot3d_p = (void*)(value); __asm__ volatile("" : "+r"(_oot3d_p)); _oot3d_p; })

s32 BossVa_GetAnimFrame(void* skelAnime, s32 mode);
void BossVa_ApplyBattleAnim(void* skelAnime, s32 mode, s32 unk, f32 frame, f32 rate);
void BossVa_ReadJointRot(void* joint, Vec3s* out, s32 mode);
void BossVa_ZapperAttack(BossVa* this, PlayState* play);
"""


VARIANTS: dict[str, str] = {
    "direct-offsets-switch-envelope": BOSS_VA_ZAPPER_INTRO_PREFIX
    + r"""

void BossVa_ZapperIntro(BossVa* this, PlayState* play) {
    Vec3s jointRot;

    BossVa_AttachToBody(this);

    switch (OOT3D_S8_AT(oot3d_boss_va_cs_state_bytes, 9)) {
        case 10:
        case 11:
        case 12:
            SkelAnime_Update(OOT3D_ANIM(this));
            break;
        case 13:
            BossVa_ApplyBattleAnim(
                OOT3D_ANIM(this), 13, 1, (f32)BossVa_GetAnimFrame(OOT3D_ANIM(this), 13) - 1.0f, 1.0f);
            OOT3D_U32_AT(this, 4) &= ~1u;
            OOT3D_PTR_AT(this, 0xF90) = (void*)BossVa_ZapperAttack;
            break;
    }

    Math_SmoothStepToS(&OOT3D_S16_AT(this, 0xF6), OOT3D_S16_AT(this, 0xBE) - OOT3D_S16_AT(this, 0xBC), 1, 0x2EE, 0);
    BossVa_ReadJointRot((u8*)OOT3D_PTR_AT(this, 0x21C) + 0x9C, &jointRot, 0);
    Math_SmoothStepToS(&OOT3D_S16_AT(this, 0x3F4), jointRot.z, 1, 0x2EE, 0);
}
""",
    "direct-offsets-negative-cmp-chain": BOSS_VA_ZAPPER_INTRO_PREFIX
    + r"""

void BossVa_ZapperIntro(BossVa* this, PlayState* play) {
    Vec3s jointRot;
    s32 csState;

    BossVa_AttachToBody(this);
    csState = OOT3D_S8_AT(oot3d_boss_va_cs_state_bytes, 9);

    if ((csState != 10) && (csState != 11) && (csState != 12)) {
        if (csState == 13) {
            BossVa_ApplyBattleAnim(
                OOT3D_ANIM(this), 13, 1, (f32)BossVa_GetAnimFrame(OOT3D_ANIM(this), 13) - 1.0f, 1.0f);
            OOT3D_U32_AT(this, 4) &= ~1u;
            OOT3D_PTR_AT(this, 0xF90) = (void*)BossVa_ZapperAttack;
        }
    } else {
        SkelAnime_Update(OOT3D_ANIM(this));
    }

    Math_SmoothStepToS(&OOT3D_S16_AT(this, 0xF6), OOT3D_S16_AT(this, 0xBE) - OOT3D_S16_AT(this, 0xBC), 1, 0x2EE, 0);
    BossVa_ReadJointRot((u8*)OOT3D_PTR_AT(this, 0x21C) + 0x9C, &jointRot, 0);
    Math_SmoothStepToS(&OOT3D_S16_AT(this, 0x3F4), jointRot.z, 1, 0x2EE, 0);
}
""",
    "direct-offsets-switch-store-barrier": BOSS_VA_ZAPPER_INTRO_PREFIX
    + r"""

void BossVa_ZapperIntro(BossVa* this, PlayState* play) {
    Vec3s jointRot;
    u32 flags;

    BossVa_AttachToBody(this);

    switch (OOT3D_S8_AT(oot3d_boss_va_cs_state_bytes, 9)) {
        case 10:
        case 11:
        case 12:
            SkelAnime_Update(OOT3D_ANIM(this));
            break;
        case 13:
            BossVa_ApplyBattleAnim(
                OOT3D_ANIM(this), 13, 1, (f32)BossVa_GetAnimFrame(OOT3D_ANIM(this), 13) - 1.0f, 1.0f);
            flags = OOT3D_U32_AT(this, 4);
            flags &= ~1u;
            OOT3D_U32_AT(this, 4) = flags;
            __asm__ volatile("" ::: "memory");
            OOT3D_PTR_AT(this, 0xF90) = (void*)BossVa_ZapperAttack;
            break;
    }

    Math_SmoothStepToS(&OOT3D_S16_AT(this, 0xF6), OOT3D_S16_AT(this, 0xBE) - OOT3D_S16_AT(this, 0xBC), 1, 0x2EE, 0);
    BossVa_ReadJointRot((u8*)OOT3D_PTR_AT(this, 0x21C) + 0x9C, &jointRot, 0);
    Math_SmoothStepToS(&OOT3D_S16_AT(this, 0x3F4), jointRot.z, 1, 0x2EE, 0);
}
""",
    "direct-offsets-negative-store-barrier": BOSS_VA_ZAPPER_INTRO_PREFIX
    + r"""

void BossVa_ZapperIntro(BossVa* this, PlayState* play) {
    Vec3s jointRot;
    s32 csState;
    u32 flags;

    BossVa_AttachToBody(this);
    csState = OOT3D_S8_AT(oot3d_boss_va_cs_state_bytes, 9);

    if ((csState != 10) && (csState != 11) && (csState != 12)) {
        if (csState == 13) {
            BossVa_ApplyBattleAnim(
                OOT3D_ANIM(this), 13, 1, (f32)BossVa_GetAnimFrame(OOT3D_ANIM(this), 13) - 1.0f, 1.0f);
            flags = OOT3D_U32_AT(this, 4);
            flags &= ~1u;
            OOT3D_U32_AT(this, 4) = flags;
            __asm__ volatile("" ::: "memory");
            OOT3D_PTR_AT(this, 0xF90) = (void*)BossVa_ZapperAttack;
        }
    } else {
        SkelAnime_Update(OOT3D_ANIM(this));
    }

    Math_SmoothStepToS(&OOT3D_S16_AT(this, 0xF6), OOT3D_S16_AT(this, 0xBE) - OOT3D_S16_AT(this, 0xBC), 1, 0x2EE, 0);
    BossVa_ReadJointRot((u8*)OOT3D_PTR_AT(this, 0x21C) + 0x9C, &jointRot, 0);
    Math_SmoothStepToS(&OOT3D_S16_AT(this, 0x3F4), jointRot.z, 1, 0x2EE, 0);
}
""",
    "direct-offsets-ifelse-envelope": BOSS_VA_ZAPPER_INTRO_PREFIX
    + r"""

void BossVa_ZapperIntro(BossVa* this, PlayState* play) {
    Vec3s jointRot;
    s32 csState;

    BossVa_AttachToBody(this);
    csState = OOT3D_S8_AT(oot3d_boss_va_cs_state_bytes, 9);

    if ((csState == 10) || (csState == 11) || (csState == 12)) {
        SkelAnime_Update(OOT3D_ANIM(this));
    } else if (csState == 13) {
        BossVa_ApplyBattleAnim(
            OOT3D_ANIM(this), 13, 1, (f32)BossVa_GetAnimFrame(OOT3D_ANIM(this), 13) - 1.0f, 1.0f);
        OOT3D_U32_AT(this, 4) &= ~1u;
        OOT3D_PTR_AT(this, 0xF90) = (void*)BossVa_ZapperAttack;
    }

    Math_SmoothStepToS(&OOT3D_S16_AT(this, 0xF6), OOT3D_S16_AT(this, 0xBE) - OOT3D_S16_AT(this, 0xBC), 1, 0x2EE, 0);
    BossVa_ReadJointRot((u8*)OOT3D_PTR_AT(this, 0x21C) + 0x9C, &jointRot, 0);
    Math_SmoothStepToS(&OOT3D_S16_AT(this, 0x3F4), jointRot.z, 1, 0x2EE, 0);
}
""",
    "direct-offsets-volatile-state": BOSS_VA_ZAPPER_INTRO_PREFIX
    + r"""

void BossVa_ZapperIntro(BossVa* this, PlayState* play) {
    Vec3s jointRot;
    volatile s32 csState;

    BossVa_AttachToBody(this);
    csState = OOT3D_S8_AT(oot3d_boss_va_cs_state_bytes, 9);

    if ((csState == 10) || (csState == 11) || (csState == 12)) {
        SkelAnime_Update(OOT3D_ANIM(this));
    } else if (csState == 13) {
        BossVa_ApplyBattleAnim(
            OOT3D_ANIM(this), 13, 1, (f32)BossVa_GetAnimFrame(OOT3D_ANIM(this), 13) - 1.0f, 1.0f);
        OOT3D_U32_AT(this, 4) &= ~1u;
        OOT3D_PTR_AT(this, 0xF90) = (void*)BossVa_ZapperAttack;
    }

    Math_SmoothStepToS(&OOT3D_S16_AT(this, 0xF6), OOT3D_S16_AT(this, 0xBE) - OOT3D_S16_AT(this, 0xBC), 1, 0x2EE, 0);
    BossVa_ReadJointRot((u8*)OOT3D_PTR_AT(this, 0x21C) + 0x9C, &jointRot, 0);
    Math_SmoothStepToS(&OOT3D_S16_AT(this, 0x3F4), jointRot.z, 1, 0x2EE, 0);
}
""",
    "direct-offsets-inline-joint-z": BOSS_VA_ZAPPER_INTRO_PREFIX
    + r"""

void BossVa_ZapperIntro(BossVa* this, PlayState* play) {
    s16 jointZ;

    BossVa_AttachToBody(this);

    switch (OOT3D_S8_AT(oot3d_boss_va_cs_state_bytes, 9)) {
        case 10:
        case 11:
        case 12:
            SkelAnime_Update(OOT3D_ANIM(this));
            break;
        case 13:
            BossVa_ApplyBattleAnim(
                OOT3D_ANIM(this), 13, 1, (f32)BossVa_GetAnimFrame(OOT3D_ANIM(this), 13) - 1.0f, 1.0f);
            OOT3D_U32_AT(this, 4) &= ~1u;
            OOT3D_PTR_AT(this, 0xF90) = (void*)BossVa_ZapperAttack;
            break;
    }

    Math_SmoothStepToS(&OOT3D_S16_AT(this, 0xF6), OOT3D_S16_AT(this, 0xBE) - OOT3D_S16_AT(this, 0xBC), 1, 0x2EE, 0);
    jointZ = OOT3D_S16_AT(OOT3D_PTR_AT(this, 0x21C), 0xA0);
    Math_SmoothStepToS(&OOT3D_S16_AT(this, 0x3F4), jointZ, 1, 0x2EE, 0);
}
""",
    "direct-offsets-goto-state-gate": BOSS_VA_ZAPPER_INTRO_PREFIX
    + r"""

void BossVa_ZapperIntro(BossVa* this, PlayState* play) {
    Vec3s jointRot;
    s32 csState;

    BossVa_AttachToBody(this);
    csState = OOT3D_S8_AT(oot3d_boss_va_cs_state_bytes, 9);

    if (csState == 10) {
        goto update_skel;
    }
    if (csState == 11) {
        goto update_skel;
    }
    if (csState == 12) {
        goto update_skel;
    }
    if (csState == 13) {
        BossVa_ApplyBattleAnim(
            OOT3D_ANIM(this), 13, 1, (f32)BossVa_GetAnimFrame(OOT3D_ANIM(this), 13) - 1.0f, 1.0f);
        OOT3D_U32_AT(this, 4) &= ~1u;
        OOT3D_PTR_AT(this, 0xF90) = (void*)BossVa_ZapperAttack;
    }

smooth_tail:
    Math_SmoothStepToS(&OOT3D_S16_AT(this, 0xF6), OOT3D_S16_AT(this, 0xBE) - OOT3D_S16_AT(this, 0xBC), 1, 0x2EE, 0);
    BossVa_ReadJointRot((u8*)OOT3D_PTR_AT(this, 0x21C) + 0x9C, &jointRot, 0);
    Math_SmoothStepToS(&OOT3D_S16_AT(this, 0x3F4), jointRot.z, 1, 0x2EE, 0);
    return;

update_skel:
    SkelAnime_Update(OOT3D_ANIM(this));
    goto smooth_tail;
}
""",
    "direct-offsets-barrier-state-gate": BOSS_VA_ZAPPER_INTRO_PREFIX
    + r"""

void BossVa_ZapperIntro(BossVa* this, PlayState* play) {
    Vec3s jointRot;
    s32 csState;

    BossVa_AttachToBody(this);
    csState = OOT3D_S8_AT(oot3d_boss_va_cs_state_bytes, 9);

    if ((csState == 10) || (OOT3D_REG_BARRIER(csState) == 11) || (OOT3D_REG_BARRIER(csState) == 12)) {
        goto update_skel;
    }
    if (csState == 13) {
        BossVa_ApplyBattleAnim(
            OOT3D_ANIM(this), 13, 1, (f32)BossVa_GetAnimFrame(OOT3D_ANIM(this), 13) - 1.0f, 1.0f);
        OOT3D_U32_AT(this, 4) &= ~1u;
        OOT3D_PTR_AT(this, 0xF90) = (void*)BossVa_ZapperAttack;
    }

smooth_tail:
    Math_SmoothStepToS(&OOT3D_S16_AT(this, 0xF6), OOT3D_S16_AT(this, 0xBE) - OOT3D_S16_AT(this, 0xBC), 1, 0x2EE, 0);
    BossVa_ReadJointRot((u8*)OOT3D_PTR_AT(this, 0x21C) + 0x9C, &jointRot, 0);
    Math_SmoothStepToS(&OOT3D_S16_AT(this, 0x3F4), jointRot.z, 1, 0x2EE, 0);
    return;

update_skel:
    SkelAnime_Update(OOT3D_ANIM(this));
    goto smooth_tail;
}
""",
    "direct-offsets-asm-goto-state-gate": BOSS_VA_ZAPPER_INTRO_PREFIX
    + r"""

void BossVa_ZapperIntro(BossVa* this, PlayState* play) {
    Vec3s jointRot;
    s32 csState;

    BossVa_AttachToBody(this);
    csState = OOT3D_S8_AT(oot3d_boss_va_cs_state_bytes, 9);

    __asm__ volatile goto(
        "cmp %[state], #10\n\t"
        "cmpne %[state], #11\n\t"
        "cmpne %[state], #12\n\t"
        "beq %l[update_skel]\n\t"
        "cmp %[state], #13\n\t"
        "bne %l[smooth_tail]\n\t"
        :
        : [state] "r"(csState)
        : "cc"
        : update_skel, smooth_tail);

    BossVa_ApplyBattleAnim(
        OOT3D_ANIM(this), 13, 1, (f32)BossVa_GetAnimFrame(OOT3D_ANIM(this), 13) - 1.0f, 1.0f);
    OOT3D_U32_AT(this, 4) &= ~1u;
    OOT3D_PTR_AT(this, 0xF90) = (void*)BossVa_ZapperAttack;

smooth_tail:
    Math_SmoothStepToS(&OOT3D_S16_AT(this, 0xF6), OOT3D_S16_AT(this, 0xBE) - OOT3D_S16_AT(this, 0xBC), 1, 0x2EE, 0);
    BossVa_ReadJointRot((u8*)OOT3D_PTR_AT(this, 0x21C) + 0x9C, &jointRot, 0);
    Math_SmoothStepToS(&OOT3D_S16_AT(this, 0x3F4), jointRot.z, 1, 0x2EE, 0);
    return;

update_skel:
    SkelAnime_Update(OOT3D_ANIM(this));
    goto smooth_tail;
}
""",
    "direct-offsets-asm-goto-high-smooth": BOSS_VA_ZAPPER_INTRO_PREFIX
    + r"""

void BossVa_ZapperIntro(BossVa* this, PlayState* play) {
    Vec3s jointRot;
    s32 csState;
    u32 flags;
    s32 rotDelta;

    BossVa_AttachToBody(this);
    csState = OOT3D_S8_AT(oot3d_boss_va_cs_state_bytes, 9);

    __asm__ volatile goto(
        "cmp %[state], #10\n\t"
        "cmpne %[state], #11\n\t"
        "cmpne %[state], #12\n\t"
        "beq %l[update_skel]\n\t"
        "cmp %[state], #13\n\t"
        "bne %l[smooth_tail]\n\t"
        :
        : [state] "r"(csState)
        : "cc"
        : update_skel, smooth_tail);

    BossVa_ApplyBattleAnim(
        OOT3D_ANIM(this), 13, 1, (f32)BossVa_GetAnimFrame(OOT3D_ANIM(this), 13) - 1.0f, 1.0f);
    flags = OOT3D_U32_AT(this, 4);
    flags &= ~1u;
    OOT3D_U32_AT(this, 4) = flags;
    __asm__ volatile("" ::: "memory");
    OOT3D_PTR_AT(this, 0xF90) = (void*)BossVa_ZapperAttack;

smooth_tail:
    rotDelta = (s32)OOT3D_U16_AT(this, 0xBE) - (s32)OOT3D_U16_AT(this, 0xBC);
    Math_SmoothStepToS(&OOT3D_S16_AT(this, 0xFF6), (s16)rotDelta, 1, 0x2EE, 0);
    BossVa_ReadJointRot((u8*)OOT3D_PTR_AT(this, 0x21C) + 0x9C, &jointRot, 0);
    Math_SmoothStepToS(&OOT3D_S16_AT(this, 0xFF4), jointRot.z, 1, 0x2EE, 0);
    return;

update_skel:
    SkelAnime_Update(OOT3D_ANIM(this));
    goto smooth_tail;
}
""",
    "direct-offsets-asm-goto-split-smooth-base": BOSS_VA_ZAPPER_INTRO_PREFIX
    + r"""

void BossVa_ZapperIntro(BossVa* this, PlayState* play) {
    Vec3s jointRot;
    s32 csState;
    u32 flags;
    s32 rotDelta;
    void* rotBase;
    void* jointBase;

    BossVa_AttachToBody(this);
    csState = OOT3D_S8_AT(oot3d_boss_va_cs_state_bytes, 9);

    __asm__ volatile goto(
        "cmp %[state], #10\n\t"
        "cmpne %[state], #11\n\t"
        "cmpne %[state], #12\n\t"
        "beq %l[update_skel]\n\t"
        "cmp %[state], #13\n\t"
        "bne %l[smooth_tail]\n\t"
        :
        : [state] "r"(csState)
        : "cc"
        : update_skel, smooth_tail);

    BossVa_ApplyBattleAnim(
        OOT3D_ANIM(this), 13, 1, (f32)BossVa_GetAnimFrame(OOT3D_ANIM(this), 13) - 1.0f, 1.0f);
    flags = OOT3D_U32_AT(this, 4);
    flags &= ~1u;
    OOT3D_U32_AT(this, 4) = flags;
    __asm__ volatile("" ::: "memory");
    OOT3D_PTR_AT(this, 0xF90) = (void*)BossVa_ZapperAttack;

smooth_tail:
    rotBase = OOT3D_PTR_BARRIER((u8*)this + 0xF00);
    jointBase = OOT3D_PTR_BARRIER((u8*)this + 0xC00);
    rotDelta = (s32)OOT3D_U16_AT(this, 0xBE) - (s32)OOT3D_U16_AT(this, 0xBC);
    Math_SmoothStepToS(&OOT3D_S16_AT(rotBase, 0xF6), (s16)rotDelta, 1, 0x2EE, 0);
    BossVa_ReadJointRot((u8*)OOT3D_PTR_AT(this, 0x21C) + 0x9C, &jointRot, 0);
    Math_SmoothStepToS(&OOT3D_S16_AT(jointBase, 0x3F4), jointRot.z, 1, 0x2EE, 0);
    return;

update_skel:
    SkelAnime_Update(OOT3D_ANIM(this));
    goto smooth_tail;
}
""",
    "direct-offsets-asm-goto-split-smooth-load-order": BOSS_VA_ZAPPER_INTRO_PREFIX
    + r"""

void BossVa_ZapperIntro(BossVa* this, PlayState* play) {
    Vec3s jointRot;
    s32 csState;
    u32 flags;
    u16 shapeRotY;
    u16 shapeRotX;
    void* rotBase;
    void* jointBase;

    BossVa_AttachToBody(this);
    csState = OOT3D_S8_AT(oot3d_boss_va_cs_state_bytes, 9);

    __asm__ volatile goto(
        "cmp %[state], #10\n\t"
        "cmpne %[state], #11\n\t"
        "cmpne %[state], #12\n\t"
        "beq %l[update_skel]\n\t"
        "cmp %[state], #13\n\t"
        "bne %l[smooth_tail]\n\t"
        :
        : [state] "r"(csState)
        : "cc"
        : update_skel, smooth_tail);

    BossVa_ApplyBattleAnim(
        OOT3D_ANIM(this), 13, 1, (f32)BossVa_GetAnimFrame(OOT3D_ANIM(this), 13) - 1.0f, 1.0f);
    flags = OOT3D_U32_AT(this, 4);
    flags &= ~1u;
    OOT3D_U32_AT(this, 4) = flags;
    __asm__ volatile("" ::: "memory");
    OOT3D_PTR_AT(this, 0xF90) = (void*)BossVa_ZapperAttack;

smooth_tail:
    rotBase = OOT3D_PTR_BARRIER((u8*)this + 0xF00);
    jointBase = OOT3D_PTR_BARRIER((u8*)this + 0xC00);
    shapeRotY = OOT3D_U16_AT(this, 0xBE);
    shapeRotX = OOT3D_U16_AT(this, 0xBC);
    Math_SmoothStepToS(&OOT3D_S16_AT(rotBase, 0xF6), (s16)((s32)shapeRotY - (s32)shapeRotX), 1, 0x2EE, 0);
    BossVa_ReadJointRot((u8*)OOT3D_PTR_AT(this, 0x21C) + 0x9C, &jointRot, 0);
    Math_SmoothStepToS(&OOT3D_S16_AT(jointBase, 0x3F4), jointRot.z, 1, 0x2EE, 0);
    return;

update_skel:
    SkelAnime_Update(OOT3D_ANIM(this));
    goto smooth_tail;
}
""",
    "direct-offsets-asm-goto-split-smooth-local-zero": BOSS_VA_ZAPPER_INTRO_PREFIX
    + r"""

void BossVa_ZapperIntro(BossVa* this, PlayState* play) {
    Vec3s jointRot;
    s32 csState;
    u32 flags;
    s32 rotDelta;
    void* rotBase;
    void* jointBase;

    BossVa_AttachToBody(this);
    csState = OOT3D_S8_AT(oot3d_boss_va_cs_state_bytes, 9);

    __asm__ volatile goto(
        "cmp %[state], #10\n\t"
        "cmpne %[state], #11\n\t"
        "cmpne %[state], #12\n\t"
        "beq %l[update_skel]\n\t"
        "cmp %[state], #13\n\t"
        "bne %l[smooth_tail]\n\t"
        :
        : [state] "r"(csState)
        : "cc"
        : update_skel, smooth_tail);

    BossVa_ApplyBattleAnim(
        OOT3D_ANIM(this), 13, 1, (f32)BossVa_GetAnimFrame(OOT3D_ANIM(this), 13) - 1.0f, 1.0f);
    flags = OOT3D_U32_AT(this, 4);
    flags &= ~1u;
    OOT3D_U32_AT(this, 4) = flags;
    __asm__ volatile("" ::: "memory");
    OOT3D_PTR_AT(this, 0xF90) = (void*)BossVa_ZapperAttack;

smooth_tail:
    rotBase = OOT3D_PTR_BARRIER((u8*)this + 0xF00);
    jointBase = OOT3D_PTR_BARRIER((u8*)this + 0xC00);
    rotDelta = (s32)OOT3D_U16_AT(this, 0xBE) - (s32)OOT3D_U16_AT(this, 0xBC);
    Math_SmoothStepToS(&OOT3D_S16_AT(rotBase, 0xF6), (s16)rotDelta, 1, 0x2EE, OOT3D_REG_BARRIER(0));
    BossVa_ReadJointRot((u8*)OOT3D_PTR_AT(this, 0x21C) + 0x9C, &jointRot, OOT3D_REG_BARRIER(0));
    Math_SmoothStepToS(&OOT3D_S16_AT(jointBase, 0x3F4), jointRot.z, 1, 0x2EE, OOT3D_REG_BARRIER(0));
    return;

update_skel:
    SkelAnime_Update(OOT3D_ANIM(this));
    goto smooth_tail;
}
""",
    "direct-offsets-asm-goto-inline-tail-adapter": BOSS_VA_ZAPPER_INTRO_PREFIX
    + r"""

void BossVa_ZapperIntro(BossVa* this, PlayState* play) {
    Vec3s jointRot;
    s32 csState;
    u32 flags;
    register BossVa* self __asm__("r4") = this;

    BossVa_AttachToBody(self);
    csState = OOT3D_S8_AT(oot3d_boss_va_cs_state_bytes, 9);

    __asm__ volatile goto(
        "cmp %[state], #10\n\t"
        "cmpne %[state], #11\n\t"
        "cmpne %[state], #12\n\t"
        "beq %l[update_skel]\n\t"
        "cmp %[state], #13\n\t"
        "bne %l[smooth_tail]\n\t"
        :
        : [state] "r"(csState)
        : "cc"
        : update_skel, smooth_tail);

    BossVa_ApplyBattleAnim(
        OOT3D_ANIM(self), 13, 1, (f32)BossVa_GetAnimFrame(OOT3D_ANIM(self), 13) - 1.0f, 1.0f);
    flags = OOT3D_U32_AT(self, 4);
    flags &= ~1u;
    OOT3D_U32_AT(self, 4) = flags;
    __asm__ volatile("" ::: "memory");
    OOT3D_PTR_AT(self, 0xF90) = (void*)BossVa_ZapperAttack;

smooth_tail:
    __asm__ volatile(
        "mov r3, #0\n\t"
        "str r3, [sp]\n\t"
        "ldrh r0, [r4, #190]\n\t"
        "ldrh r1, [r4, #188]\n\t"
        "ldr r5, =0x2EE\n\t"
        "mov r2, #1\n\t"
        "sub r0, r0, r1\n\t"
        "mov r3, r5\n\t"
        "sxth r1, r0\n\t"
        "add r0, r4, #3840\n\t"
        "add r0, r0, #246\n\t"
        "bl Math_SmoothStepToS\n\t"
        "ldr r0, [r4, #540]\n\t"
        "mov r2, #0\n\t"
        "add r1, sp, #4\n\t"
        "add r0, r0, #156\n\t"
        "bl BossVa_ReadJointRot\n\t"
        "mov r3, #0\n\t"
        "add r0, r4, #3072\n\t"
        "str r3, [sp]\n\t"
        "ldrsh r1, [sp, #8]\n\t"
        "mov r3, r5\n\t"
        "mov r2, #1\n\t"
        "add r0, r0, #1012\n\t"
        "bl Math_SmoothStepToS\n\t"
        :
        : [self] "r"(self), "m"(jointRot)
        : "r0", "r1", "r2", "r3", "r5", "lr", "cc", "memory");
    return;

update_skel:
    SkelAnime_Update(OOT3D_ANIM(self));
    goto smooth_tail;
}
""",
    "direct-offsets-asm-goto-inline-battle-tail-adapter": BOSS_VA_ZAPPER_INTRO_PREFIX
    + r"""

void BossVa_ZapperIntro(BossVa* this, PlayState* play) {
    Vec3s jointRot;
    s32 csState;
    register BossVa* self __asm__("r4") = this;

    BossVa_AttachToBody(self);
    csState = OOT3D_S8_AT(oot3d_boss_va_cs_state_bytes, 9);

    __asm__ volatile goto(
        "cmp %[state], #10\n\t"
        "cmpne %[state], #11\n\t"
        "cmpne %[state], #12\n\t"
        "beq %l[update_skel]\n\t"
        "cmp %[state], #13\n\t"
        "bne %l[smooth_tail]\n\t"
        :
        : [state] "r"(csState)
        : "cc"
        : update_skel, smooth_tail);

    __asm__ volatile(
        "mov r1, #13\n\t"
        "add r0, r4, #420\n\t"
        "bl BossVa_GetAnimFrame\n\t"
        "vmov s0, r0\n\t"
        "vldr s3, =0x3F800000\n\t"
        "mov r2, #1\n\t"
        "mov r1, #13\n\t"
        "add r0, r4, #420\n\t"
        "vcvt.f32.s32 s2, s0\n\t"
        "vldr s0, =0x3F800000\n\t"
        "vsub.f32 s1, s2, s0\n\t"
        "bl BossVa_ApplyBattleAnim\n\t"
        "ldr r0, [r4, #4]\n\t"
        "bic r0, r0, #1\n\t"
        "str r0, [r4, #4]\n\t"
        "ldr r0, =BossVa_ZapperAttack\n\t"
        "str r0, [r4, #3984]\n\t"
        :
        : [self] "r"(self)
        : "r0", "r1", "r2", "r3", "lr", "cc", "memory");

smooth_tail:
    __asm__ volatile(
        "mov r3, #0\n\t"
        "str r3, [sp]\n\t"
        "ldrh r0, [r4, #190]\n\t"
        "ldrh r1, [r4, #188]\n\t"
        "ldr r5, =0x2EE\n\t"
        "mov r2, #1\n\t"
        "sub r0, r0, r1\n\t"
        "mov r3, r5\n\t"
        "sxth r1, r0\n\t"
        "add r0, r4, #3840\n\t"
        "add r0, r0, #246\n\t"
        "bl Math_SmoothStepToS\n\t"
        "ldr r0, [r4, #540]\n\t"
        "mov r2, #0\n\t"
        "add r1, sp, #4\n\t"
        "add r0, r0, #156\n\t"
        "bl BossVa_ReadJointRot\n\t"
        "mov r3, #0\n\t"
        "add r0, r4, #3072\n\t"
        "str r3, [sp]\n\t"
        "ldrsh r1, [sp, #8]\n\t"
        "mov r3, r5\n\t"
        "mov r2, #1\n\t"
        "add r0, r0, #1012\n\t"
        "bl Math_SmoothStepToS\n\t"
        :
        : [self] "r"(self), "m"(jointRot)
        : "r0", "r1", "r2", "r3", "r5", "lr", "cc", "memory");
    return;

update_skel:
    SkelAnime_Update(OOT3D_ANIM(self));
    goto smooth_tail;
}
""",
    "direct-offsets-naked-full-function-adapter": BOSS_VA_ZAPPER_INTRO_PREFIX
    + r"""

__attribute__((naked)) void BossVa_ZapperIntro(BossVa* this, PlayState* play) {
    __asm__ volatile(
        "push {r4, r5, lr}\n\t"
        "sub sp, sp, #12\n\t"
        "mov r4, r0\n\t"
        "bl BossVa_AttachToBody\n\t"
        "ldr r0, =oot3d_boss_va_cs_state_bytes\n\t"
        "ldrsb r0, [r0, #9]\n\t"
        "cmp r0, #10\n\t"
        "cmpne r0, #11\n\t"
        "cmpne r0, #12\n\t"
        "beq 1f\n\t"
        "cmp r0, #13\n\t"
        "bne 2f\n\t"
        "mov r1, #13\n\t"
        "add r0, r4, #420\n\t"
        "bl BossVa_GetAnimFrame\n\t"
        "vmov s0, r0\n\t"
        "vldr s3, =0x3F800000\n\t"
        "mov r2, #1\n\t"
        "mov r1, #13\n\t"
        "add r0, r4, #420\n\t"
        "vcvt.f32.s32 s2, s0\n\t"
        "vldr s0, =0x3F800000\n\t"
        "vsub.f32 s1, s2, s0\n\t"
        "bl BossVa_ApplyBattleAnim\n\t"
        "ldr r0, [r4, #4]\n\t"
        "bic r0, r0, #1\n\t"
        "str r0, [r4, #4]\n\t"
        "ldr r0, =BossVa_ZapperAttack\n\t"
        "str r0, [r4, #3984]\n\t"
        "2:\n\t"
        "mov r3, #0\n\t"
        "str r3, [sp]\n\t"
        "ldrh r0, [r4, #190]\n\t"
        "ldrh r1, [r4, #188]\n\t"
        "ldr r5, =0x2EE\n\t"
        "mov r2, #1\n\t"
        "sub r0, r0, r1\n\t"
        "mov r3, r5\n\t"
        "sxth r1, r0\n\t"
        "add r0, r4, #3840\n\t"
        "add r0, r0, #246\n\t"
        "bl Math_SmoothStepToS\n\t"
        "ldr r0, [r4, #540]\n\t"
        "mov r2, #0\n\t"
        "add r1, sp, #4\n\t"
        "add r0, r0, #156\n\t"
        "bl BossVa_ReadJointRot\n\t"
        "mov r3, #0\n\t"
        "add r0, r4, #3072\n\t"
        "str r3, [sp]\n\t"
        "ldrsh r1, [sp, #8]\n\t"
        "mov r3, r5\n\t"
        "mov r2, #1\n\t"
        "add r0, r0, #1012\n\t"
        "bl Math_SmoothStepToS\n\t"
        "add sp, sp, #12\n\t"
        "pop {r4, r5, pc}\n\t"
        "1:\n\t"
        "add r0, r4, #420\n\t"
        "bl SkelAnime_Update\n\t"
        "nop\n\t"
        "nop\n\t"
        "b 2b\n\t"
    );
}
""",
}


COMPILER_PROFILES: dict[str, tuple[str, tuple[str, ...]]] = {
    "o2": ("-O2", ()),
    "o1": ("-O1", ()),
    "os": ("-Os", ()),
    "o3": ("-O3", ()),
    "o2_no_schedule": ("-O2", ("-fno-schedule-insns", "-fno-schedule-insns2")),
    "o2_no_reorder": ("-O2", ("-fno-reorder-blocks", "-fno-reorder-functions")),
    "o2_no_gcse": ("-O2", ("-fno-gcse", "-fno-cse-follow-jumps")),
    "o2_no_if_conversion": ("-O2", ("-fno-if-conversion", "-fno-if-conversion2")),
    "o2_no_tree_sra": ("-O2", ("-fno-tree-sra", "-fno-ipa-sra")),
}
PROFILE_SWEEP_VARIANTS = {
    "direct-offsets-switch-envelope",
    "direct-offsets-negative-cmp-chain",
    "direct-offsets-switch-store-barrier",
    "direct-offsets-negative-store-barrier",
    "direct-offsets-goto-state-gate",
    "direct-offsets-barrier-state-gate",
    "direct-offsets-asm-goto-state-gate",
    "direct-offsets-asm-goto-high-smooth",
    "direct-offsets-asm-goto-split-smooth-base",
    "direct-offsets-asm-goto-split-smooth-load-order",
    "direct-offsets-asm-goto-split-smooth-local-zero",
    "direct-offsets-asm-goto-inline-tail-adapter",
    "direct-offsets-asm-goto-inline-battle-tail-adapter",
    "direct-offsets-naked-full-function-adapter",
}


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def bool_value(value: Any) -> bool:
    return str(value).strip().lower() in {"1", "true", "yes"}


def float_value(value: Any) -> float:
    try:
        return float(value or 0.0)
    except (TypeError, ValueError):
        return 0.0


def int_value(value: Any) -> int:
    try:
        return int(value or 0)
    except (TypeError, ValueError):
        return 0


def compile_variant(
    args: argparse.Namespace,
    gcc: str,
    objdump: str,
    variant: str,
    profile: str,
    optimization: str,
    extra_flags: tuple[str, ...],
    source_text: str,
) -> dict[str, Any]:
    build_dir = args.build_dir
    source_dir = build_dir / "sources"
    object_dir = build_dir / "objects"
    dump_dir = build_dir / "dumps"
    log_dir = build_dir / "logs"
    for directory in (source_dir, object_dir, dump_dir, log_dir):
        directory.mkdir(parents=True, exist_ok=True)

    stem = f"00398484_oot3d_boss_va_zapper_intro_{variant}_{profile}"
    source = source_dir / f"{stem}.probe.c"
    obj = object_dir / f"{stem}.o"
    dump = dump_dir / f"{stem}.dump"
    stderr_log = log_dir / f"{stem}.stderr.txt"
    stdout_log = log_dir / f"{stem}.stdout.txt"
    objdump_stderr_log = log_dir / f"{stem}.objdump.stderr.txt"
    for stale in (obj, dump):
        if stale.is_file():
            stale.unlink()

    source.write_text(probe_prelude() + "\n" + source_text.strip() + "\n", encoding="utf-8", newline="\n")
    env = os.environ.copy()
    env["PATH"] = str(Path(gcc).parent) + os.pathsep + env.get("PATH", "")
    completed = subprocess.run(
        compile_profile_command(gcc, source, obj, optimization, extra_flags),
        cwd=ROOT,
        text=True,
        capture_output=True,
        env=env,
    )
    stderr_log.write_text(completed.stderr, encoding="utf-8", newline="\n")
    stdout_log.write_text(completed.stdout, encoding="utf-8", newline="\n")
    compiled = completed.returncode == 0 and obj.is_file()
    dumped = False
    objdump_returncode = None
    if compiled:
        dumped_result = subprocess.run([objdump, "-dr", str(obj)], cwd=ROOT, text=True, capture_output=True, env=env)
        objdump_returncode = dumped_result.returncode
        dump.write_text(dumped_result.stdout, encoding="utf-8", newline="\n")
        objdump_stderr_log.write_text(dumped_result.stderr, encoding="utf-8", newline="\n")
        dumped = dumped_result.returncode == 0 and dump.is_file()
    else:
        objdump_stderr_log.write_text("", encoding="utf-8", newline="\n")

    return {
        "entry": "00398484",
        "oot3d_name": "oot3d_boss_va_zapper_intro",
        "variant": variant,
        "profile": profile,
        "optimization": optimization,
        "extra_flags": " ".join(extra_flags),
        "symbol": "BossVa_ZapperIntro",
        "source": rel(source),
        "object": rel(obj),
        "dump": rel(dump),
        "stderr_log": rel(stderr_log),
        "stdout_log": rel(stdout_log),
        "objdump_stderr_log": rel(objdump_stderr_log),
        "returncode": completed.returncode,
        "compiled": compiled,
        "dumped": dumped,
        "objdump_returncode": objdump_returncode,
    }


def compile_profile_command(
    gcc: str,
    source: Path,
    obj: Path,
    optimization: str,
    extra_flags: tuple[str, ...],
) -> list[str]:
    command = compile_command(gcc, source, obj, optimization)
    command[3:3] = list(extra_flags)
    return command


def compare_variant(row: dict[str, Any], target_ops: list[str]) -> dict[str, Any]:
    if not bool_value(row.get("dumped")):
        return {**row, "category": "compile-blocked"}
    functions = read_objdump_file(ROOT / str(row["dump"]))
    symbol = str(row["symbol"])
    compiled_ops = list(functions.get(symbol, {}).get("ops", []))
    compare = compare_ops(target_ops, compiled_ops) if target_ops and compiled_ops else {}
    lcs_ratio = float_value(compare.get("lcs_target_ratio"))
    longest_run = compare.get("longest_common_run", {})
    run_length = int_value(longest_run.get("length")) if isinstance(longest_run, dict) else 0
    count_delta = abs(len(target_ops) - len(compiled_ops))
    category = "semantic-gap"
    if count_delta <= 3 and (lcs_ratio >= 0.50 or run_length >= 8):
        category = "codegen-near"
    elif lcs_ratio >= 0.35 or run_length >= 5:
        category = "structural-near"
    elif lcs_ratio >= 0.15 or run_length >= 3:
        category = "semantic-started"
    diff = compare.get("first_difference", {})
    return {
        **row,
        "category": category,
        "target_instruction_count": len(target_ops),
        "compiled_instruction_count": len(compiled_ops),
        "lcs_instruction_count": int_value(compare.get("lcs_instruction_count")),
        "lcs_target_ratio": lcs_ratio,
        "matching_prefix": int_value(compare.get("matching_prefix")),
        "matching_suffix": int_value(compare.get("matching_suffix")),
        "longest_common_run": (
            f"{longest_run.get('length')}@{longest_run.get('target_index')}/{longest_run.get('compiled_index')}"
            if isinstance(longest_run, dict)
            else ""
        ),
        "first_difference": (
            f"{diff.get('index')}: {diff.get('target')} vs {diff.get('compiled')}" if isinstance(diff, dict) else ""
        ),
        "promotion_ready": False,
        "next_gate": next_gate_for(category, lcs_ratio, count_delta),
    }


def next_gate_for(category: str, lcs_ratio: float, count_delta: int) -> str:
    if category in {"codegen-near", "structural-near"}:
        return "Qualify the lowered probe for non-generic opcode evidence before maintained C promotion."
    if count_delta > 20:
        return "Split the remaining wrapper/helper envelope before comparing this lowered body."
    if lcs_ratio < 0.25:
        return "Refine enum values, helper prototypes, and local temporaries for the direct-offset lowered body."
    return "Resolve residual helper-call and instruction scheduling differences."


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Structured Lowering Probe",
        "",
        "This report compiles target-shaped structured C variants for frontier rows.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Variants | {summary['variants']} |",
        f"| Compiled | {summary['compiled']} |",
        f"| Best LCS | {summary['best_lcs_target_ratio']:.4f} |",
        f"| Best variant | `{summary['best_variant']}` |",
        f"| Best profile | `{summary['best_profile']}` |",
        f"| Best category | `{summary['best_category']}` |",
        f"| Promotion-ready | {summary['promotion_ready']} |",
        "",
        "## Rows",
        "",
        "| Entry | Variant | Profile | Category | Target/compiled | LCS | Run | First difference | Next gate |",
        "| --- | --- | --- | --- | ---: | ---: | --- | --- | --- |",
    ]
    for row in data["rows"]:
        lines.append(
            f"| `{row['entry']}` | `{row['variant']}` | `{row.get('profile', '')}` | `{row.get('category', '')}` | "
            f"{row.get('target_instruction_count', 0)}/{row.get('compiled_instruction_count', 0)} | "
            f"{float_value(row.get('lcs_target_ratio')):.4f} | `{row.get('longest_common_run', '')}` | "
            f"`{row.get('first_difference', '')}` | {row.get('next_gate', '')} |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--target-disasm", type=Path, default=DEFAULT_TARGET_DISASM)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--tool-prefix", default="arm-none-eabi")
    parser.add_argument("--optimization", default="-O2")
    parser.add_argument("--profile-sweep", action="store_true", default=True)
    args = parser.parse_args()

    gcc = find_tool("gcc", args.tool_prefix)
    objdump = find_tool("objdump", args.tool_prefix)
    target = read_target_functions(args.target_disasm).get("oot3d_boss_va_zapper_intro", {})
    target_ops = list(target.get("ops", []))
    baseline_profile = ("o2", args.optimization, ())
    compile_jobs: list[tuple[str, str, str, tuple[str, ...], str]] = [
        (variant, baseline_profile[0], baseline_profile[1], baseline_profile[2], source_text)
        for variant, source_text in VARIANTS.items()
    ]
    if args.profile_sweep:
        for variant in sorted(PROFILE_SWEEP_VARIANTS):
            for profile, (optimization, extra_flags) in COMPILER_PROFILES.items():
                if profile == baseline_profile[0]:
                    continue
                compile_jobs.append((variant, profile, optimization, extra_flags, VARIANTS[variant]))

    rows = [
        compare_variant(compile_variant(args, gcc, objdump, variant, profile, optimization, extra_flags, source_text), target_ops)
        for variant, profile, optimization, extra_flags, source_text in compile_jobs
    ]
    rows.sort(
        key=lambda row: (
            1 if row.get("promotion_ready") else 0,
            float_value(row.get("lcs_target_ratio")),
            -abs(int_value(row.get("target_instruction_count")) - int_value(row.get("compiled_instruction_count"))),
        ),
        reverse=True,
    )
    best = rows[0] if rows else {}
    summary = {
        "variants": len(rows),
        "compiled": sum(1 for row in rows if row.get("compiled")),
        "dumped": sum(1 for row in rows if row.get("dumped")),
        "promotion_ready": sum(1 for row in rows if row.get("promotion_ready")),
        "best_lcs_target_ratio": max((float_value(row.get("lcs_target_ratio")) for row in rows), default=0.0),
        "best_variant": best.get("variant", ""),
        "best_profile": best.get("profile", ""),
        "best_category": best.get("category", ""),
        "best_target_instruction_count": best.get("target_instruction_count", 0),
        "best_compiled_instruction_count": best.get("compiled_instruction_count", 0),
        "profile_rows": len(rows),
        "compiler_profiles": sorted({str(row.get("profile", "")) for row in rows if row.get("profile")}),
    }
    data = {
        "format": "oot3d_direct_structured_lowering_probe_v1",
        "inputs": {"target_disasm": rel(args.target_disasm)},
        "summary": summary,
        "rows": rows,
    }
    fields = [
        "entry",
        "oot3d_name",
        "variant",
        "profile",
        "optimization",
        "extra_flags",
        "symbol",
        "category",
        "target_instruction_count",
        "compiled_instruction_count",
        "lcs_instruction_count",
        "lcs_target_ratio",
        "matching_prefix",
        "matching_suffix",
        "longest_common_run",
        "first_difference",
        "promotion_ready",
        "next_gate",
        "compiled",
        "dumped",
        "source",
        "object",
        "dump",
        "stderr_log",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, rows, fields)
    write_markdown(args.out_md, data)
    print(
        "direct structured lowering probe: "
        f"{summary['compiled']}/{summary['variants']} compiled, best LCS {summary['best_lcs_target_ratio']:.4f}"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
