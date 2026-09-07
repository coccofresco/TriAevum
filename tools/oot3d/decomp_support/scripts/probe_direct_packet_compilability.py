#!/usr/bin/env python3
"""Compile-probe materialized direct conversion packets in batch.

The direct packets are generated from N64 source extracts plus adapter rewrites.
They are not maintained build sources yet.  This probe creates throwaway
translation units with a permissive N64-shaped prelude, compiles them with the
same ARM toolchain, and reports the next mechanical blockers to making each
packet compilable C.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import shutil
import subprocess
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MATERIALIZED = ROOT / "analysis" / "direct_conversion_materialized"
DEFAULT_BUILD = ROOT / "build" / "direct_packet_compile_probe"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_packet_compile_probe.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_packet_compile_probe.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_packet_compile_probe.md"

ERROR_RE = re.compile(r":(?:\d+:){1,2}\s+error:\s+(?P<message>.*)")
WARNING_RE = re.compile(r":(?:\d+:){1,2}\s+warning:\s+(?P<message>.*)")
UNKNOWN_TYPE_RE = re.compile(r"unknown type name '([^']+)'")
UNDECLARED_RE = re.compile(r"'([^']+)' undeclared")
NO_MEMBER_RE = re.compile(r"'([^']+)' has no member named '([^']+)'")
MEMBER_ON_NONSTRUCT_RE = re.compile(r"request for member '([^']+)' in something not a structure")
INCOMPLETE_RE = re.compile(r"invalid use of incomplete typedef '([^']+)'")
GLOBAL_ASSET_RE = re.compile(
    r"\b(gPlayerAnim_[A-Za-z0-9_]+|g[A-Za-z0-9_]*(?:Tex|TLUT|DL|Vtx|Data|Icon|Name|Blob|Pal|Palette|Anim)(?:_[A-Z0-9_]+)?)\b"
)
STATIC_GLOBAL_RE = re.compile(r"\b(s[A-Z][A-Za-z0-9_]+)\b")
ALL_CAPS_TOKEN_RE = re.compile(r"\b[A-Z][A-Z0-9_]{2,}\b")
MIXED_CONSTANT_RE = re.compile(r"\b(PLAYER_ANIMGROUP_[A-Za-z0-9_]+)\b")
R_SCALAR_ASSIGN_RE = re.compile(r"\b(R_[A-Z0-9_]+)\s*=")
R_INDEXED_USE_RE = re.compile(r"\b(R_[A-Z0-9_]+)\s*\(")
PAUSE_REG_LANE_RE = re.compile(r"\boot3d_pause_reg_([a-z])_lane\s*\(")
PROBE_DISPLAY_LIST_POINTERS = {"POLY_OPA_DISP", "POLY_XLU_DISP", "OVERLAY_DISP"}
PROBE_PRELUDE_VARIABLES = {
    "R_TEXTBOX_X",
    "R_TEXTBOX_Y",
    "R_TEXTBOX_X_TARGET",
    "R_TEXTBOX_Y_TARGET",
    "R_TEXTBOX_WIDTH",
    "R_TEXTBOX_HEIGHT",
    "R_TEXTBOX_TEXWIDTH",
    "R_TEXTBOX_TEXHEIGHT",
    "R_TEXTBOX_END_YPOS",
}
MELEE_OFFSET_SYMBOLS = {
    "sMeleeWeaponBaseOffsetFromLeftHand0",
    "sMeleeWeaponBaseOffsetFromLeftHand1",
    "sMeleeWeaponBaseOffsetFromLeftHand2",
}

DIRECT_FIELD_REPLACEMENTS = {
    "OOT3D_DIRECT_FIELD(this, OOT3D_ACTOR_OFFSET_BG_CHECK_FLAGS)": "this->actor.bgCheckFlags",
    "OOT3D_DIRECT_FIELD(this, OOT3D_ACTOR_OFFSET_FLOOR_BG_ID)": "this->actor.floorBgId",
    "OOT3D_DIRECT_FIELD(this, OOT3D_ACTOR_OFFSET_FLOOR_HEIGHT)": "this->actor.floorHeight",
    "OOT3D_DIRECT_FIELD(this, OOT3D_ACTOR_OFFSET_FLOOR_POLY)": "this->actor.floorPoly",
    "OOT3D_DIRECT_FIELD(this, OOT3D_ACTOR_OFFSET_FLAGS)": "this->actor.flags",
    "OOT3D_DIRECT_FIELD(this, OOT3D_ACTOR_OFFSET_SHAPE_ROT_X)": "this->actor.shape.rot.x",
    "OOT3D_DIRECT_FIELD(this, OOT3D_ACTOR_OFFSET_SHAPE_ROT_Y)": "this->actor.shape.rot.y",
    "OOT3D_DIRECT_FIELD(this, OOT3D_ACTOR_OFFSET_SPEED)": "this->actor.speed",
    "OOT3D_DIRECT_FIELD(this, OOT3D_ACTOR_OFFSET_SPEED_XZ)": "this->actor.speedXZ",
    "OOT3D_DIRECT_FIELD(this, OOT3D_ACTOR_OFFSET_WALL_BG_ID)": "this->actor.wallBgId",
    "OOT3D_DIRECT_FIELD(this, OOT3D_ACTOR_OFFSET_WALL_POLY)": "this->actor.wallPoly",
    "OOT3D_DIRECT_FIELD(this, OOT3D_ACTOR_OFFSET_WALL_YAW)": "this->actor.wallYaw",
    "OOT3D_DIRECT_FIELD(this, OOT3D_BOSS_VA_OFFSET_BURST)": "this->burst",
    "OOT3D_DIRECT_FIELD(this, OOT3D_BOSS_VA_OFFSET_BURST_POS)": "this->burstPos",
    "OOT3D_DIRECT_FIELD(this, OOT3D_BOSS_VA_OFFSET_HEAD_ROT_Y)": "this->headRotY",
    "OOT3D_DIRECT_FIELD(this, OOT3D_BOSS_VA_OFFSET_HEAD_ROT_Z)": "this->headRotZ",
    "OOT3D_DIRECT_FIELD(this, OOT3D_BOSS_VA_OFFSET_IS_DEAD)": "this->isDead",
    "OOT3D_DIRECT_FIELD(this, OOT3D_BOSS_VA_OFFSET_JOINT_TABLE_PTR)": "this->skelAnime.jointTable",
    "OOT3D_DIRECT_FIELD(this, OOT3D_BOSS_VA_OFFSET_LIGHTNING_COLLIDER)": "this->colliderLightning",
    "OOT3D_DIRECT_FIELD(this, OOT3D_BOSS_VA_OFFSET_ROT_E4)": "this->unk_1E4",
    "OOT3D_DIRECT_FIELD(this, OOT3D_BOSS_VA_OFFSET_ROT_E6)": "this->unk_1E6",
    "OOT3D_DIRECT_FIELD(this, OOT3D_BOSS_VA_OFFSET_ROT_E8_TARGET)": "this->unk_1E8",
    "OOT3D_DIRECT_FIELD(this, OOT3D_BOSS_VA_OFFSET_ROT_EA)": "this->unk_1EA",
    "OOT3D_DIRECT_FIELD(this, OOT3D_BOSS_VA_OFFSET_ROT_EC)": "this->unk_1EC",
    "OOT3D_DIRECT_FIELD(this, OOT3D_BOSS_VA_OFFSET_ROT_EE_TARGET)": "this->unk_1EE",
    "OOT3D_DIRECT_FIELD(this, OOT3D_BOSS_VA_OFFSET_ROT_F0)": "this->unk_1F0",
    "OOT3D_DIRECT_FIELD(this, OOT3D_BOSS_VA_OFFSET_ROT_F2)": "this->unk_1F2",
    "OOT3D_DIRECT_FIELD(this, OOT3D_BOSS_VA_OFFSET_ROT_F4_TARGET)": "this->unk_1F4",
    "OOT3D_DIRECT_FIELD(this, OOT3D_BOSS_VA_OFFSET_TIMER2)": "this->timer2",
    "OOT3D_DIRECT_FIELD(this, OOT3D_BOSS_VA_OFFSET_ZAP_HEAD_POS)": "this->zapHeadPos",
    "OOT3D_DIRECT_FIELD(this, OOT3D_BOSS_VA_OFFSET_ZAP_NECK_POS)": "this->zapNeckPos",
    "OOT3D_DIRECT_FIELD(this, oot3d_boss_va_skel_anime)": "this->skelAnime",
    "OOT3D_DIRECT_FIELD(this, OOT3D_PLAYER_OFFSET_ACTION_FUNC)": "this->actionFunc",
    "OOT3D_DIRECT_FIELD(this, OOT3D_PLAYER_OFFSET_AGE_PROPERTIES)": "this->ageProperties",
    "OOT3D_DIRECT_FIELD(this, OOT3D_PLAYER_OFFSET_CURRENT_BOOTS)": "this->currentBoots",
    "OOT3D_DIRECT_FIELD(this, OOT3D_PLAYER_OFFSET_DIST_TO_WALL)": "this->distToInteractWall",
    "OOT3D_DIRECT_FIELD(this, OOT3D_PLAYER_OFFSET_FLOOR_PROPERTY)": "this->floorProperty",
    "OOT3D_DIRECT_FIELD(this, OOT3D_PLAYER_OFFSET_FLOOR_SFX_OFFSET)": "this->floorSfxOffset",
    "OOT3D_DIRECT_FIELD(this, OOT3D_PLAYER_OFFSET_FLOOR_TYPE_TIMER)": "this->floorTypeTimer",
    "OOT3D_DIRECT_FIELD(this, OOT3D_PLAYER_OFFSET_LEDGE_CLIMB_TYPE)": "this->ledgeClimbType",
    "OOT3D_DIRECT_FIELD(this, OOT3D_PLAYER_OFFSET_LEDGE_DELAY_TIMER)": "this->ledgeClimbDelayTimer",
    "OOT3D_DIRECT_FIELD(this, OOT3D_PLAYER_OFFSET_LEDGE_SIDE)": "this->ledgeSide",
    "OOT3D_DIRECT_FIELD(this, OOT3D_PLAYER_OFFSET_LEDGE_YAW_TARGET)": "this->ledgeYawTarget",
    "OOT3D_DIRECT_FIELD(this, OOT3D_PLAYER_OFFSET_PREV_FLOOR_SFX_OFFSET)": "this->prevFloorSfxOffset",
    "OOT3D_DIRECT_FIELD(this, OOT3D_PLAYER_OFFSET_PREV_FLOOR_TYPE)": "this->prevFloorType",
    "OOT3D_DIRECT_FIELD(this, OOT3D_PLAYER_OFFSET_SKEL_ANIME)": "this->skelAnime",
    "OOT3D_DIRECT_FIELD(this, OOT3D_PLAYER_OFFSET_SPEED_XZ)": "this->speedXZ",
    "OOT3D_DIRECT_FIELD(this, OOT3D_PLAYER_OFFSET_STATE_FLAGS1)": "this->stateFlags1",
    "OOT3D_DIRECT_FIELD(this, OOT3D_PLAYER_OFFSET_STATE_FLAGS2)": "this->stateFlags2",
    "OOT3D_DIRECT_FIELD(this, OOT3D_PLAYER_OFFSET_STATE_FLAGS3)": "this->stateFlags3",
    "OOT3D_DIRECT_FIELD(this, OOT3D_PLAYER_OFFSET_WALL_SPEED_LIMIT)": "this->wallSpeedLimit",
    "OOT3D_DIRECT_FIELD(this, OOT3D_PLAYER_OFFSET_YAW)": "this->yaw",
    "OOT3D_DIRECT_FIELD(this, OOT3D_PLAYER_OFFSET_Y_DIST_TO_LEDGE)": "this->yDistToLedge",
    "OOT3D_DIRECT_FIELD(play, OOT3D_PLAY_OFFSET_COLCHK_CTX)": "play->colChkCtx",
    "OOT3D_DIRECT_FIELD(play, OOT3D_PLAY_OFFSET_COLCTX)": "play->colCtx",
    "OOT3D_DIRECT_FIELD(play, OOT3D_PLAY_OFFSET_PLAYER_ACTOR)": "play->playerActor",
    "OOT3D_DIRECT_FIELD(play, OOT3D_PLAY_OFFSET_SCENE_ID)": "play->sceneId",
    "OOT3D_DIRECT_FIELD(msgCtx, OOT3D_MESSAGE_OFFSET_TEXTBOX_MODE)": "msgCtx->msgMode",
    "OOT3D_DIRECT_FIELD(msgCtx, OOT3D_MESSAGE_OFFSET_TEXTBOX_STATE)": "msgCtx->stateTimer",
}


@dataclass(frozen=True)
class Packet:
    entry: str
    name: str
    lane: str
    domain: str
    source: str
    output: Path
    rewrites: int
    adapters: int
    shape_anchors: int


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def int_value(value: Any) -> int:
    try:
        return int(value or 0)
    except (TypeError, ValueError):
        return 0


def lane_domains(index_path: Path) -> dict[str, dict[str, str]]:
    index = read_json(index_path, {})
    lanes = index.get("lanes", []) if isinstance(index, dict) else []
    by_slug: dict[str, dict[str, str]] = {}
    for lane in lanes:
        slug = str(lane.get("slug", ""))
        if not slug:
            continue
        by_slug[slug] = {
            "lane": str(lane.get("lane", "")),
            "domain": str(lane.get("domain", "")),
        }
    return by_slug


def discover_packets(materialized: Path, only_status: str = "ok") -> list[Packet]:
    domains = lane_domains(materialized / "index.json")
    packets: list[Packet] = []
    for report_path in sorted(materialized.glob("*/*/rewrite_report.json")):
        report = read_json(report_path, {})
        if not isinstance(report, dict):
            continue
        if only_status and report.get("status") != only_status:
            continue
        output = ROOT / str(report.get("output", ""))
        if not output.is_file():
            continue
        slug = report_path.parent.parent.name
        lane = domains.get(slug, {})
        packets.append(
            Packet(
                entry=str(report.get("entry", "")).lower(),
                name=str(report.get("name", "")),
                lane=lane.get("lane", ""),
                domain=lane.get("domain", "other"),
                source=str(report.get("source", "")),
                output=output,
                rewrites=int_value(report.get("rewrite_count")),
                adapters=int_value(report.get("adapter_count")),
                shape_anchors=len(report.get("shape_anchors", []) or []),
            )
        )
    return packets


def simple_oot3d_defines() -> list[str]:
    lines: list[str] = []
    seen: set[str] = set()
    define_re = re.compile(r"^\s*#\s*define\s+(OOT3D_[A-Za-z0-9_]+)\s+(.+?)\s*$")
    for header in sorted((ROOT / "include" / "oot3d").glob("*.h")):
        for raw in header.read_text(encoding="utf-8", errors="replace").splitlines():
            match = define_re.match(raw)
            if not match:
                continue
            name, value = match.groups()
            if name in seen:
                continue
            if "\\" in value:
                continue
            if not re.fullmatch(r"[A-Za-z0-9_()|&~+\-*/<>\s.xXa-fA-FuUL]+", value):
                continue
            lines.append(f"#ifndef {name}\n#define {name} {value}\n#endif")
            seen.add(name)
    return lines


def probe_prelude() -> str:
    define_lines = simple_oot3d_defines()
    return "\n".join(
        [
            "/* Generated compile probe prelude. Do not use as maintained source. */",
            '#include "oot3d/types.h"',
            "#include <stddef.h>",
            "#include <stdbool.h>",
            "",
            "typedef float f32;",
            "typedef double f64;",
            "",
            "typedef struct Vec3f { f32 x; f32 y; f32 z; } Vec3f;",
            "typedef struct Vec3s { s16 x; s16 y; s16 z; } Vec3s;",
            "typedef struct MtxF { f32 mf[4][4]; } MtxF;",
            "typedef struct CollisionPoly { Vec3s normal; s16 dist; } CollisionPoly;",
            "typedef struct ButtonState { u32 button; } ButtonState;",
            "typedef struct ActorShape { Vec3s rot; Vec3f feetPos[2]; f32 yOffset; } ActorShape;",
            "typedef struct ActorWorld { Vec3f pos; Vec3s rot; } ActorWorld;",
            "typedef struct ActorFocus { Vec3f pos; Vec3s rot; } ActorFocus;",
            "typedef struct ActorHome { Vec3f pos; Vec3s rot; } ActorHome;",
            "typedef struct ActorColChkInfo { s32 atHitBacklash; s32 damage; } ActorColChkInfo;",
            "typedef struct Actor {",
            "    s16 id;",
            "    s8 category;",
            "    s16 params;",
            "    u32 flags;",
            "    ActorWorld world;",
            "    ActorHome home;",
            "    ActorShape shape;",
            "    ActorFocus focus;",
            "    Vec3f velocity;",
            "    Vec3f scale;",
            "    f32 speed;",
            "    f32 speedXZ;",
            "    struct Actor* parent;",
            "    struct Actor* child;",
            "    struct Actor* next;",
            "    CollisionPoly* floorPoly;",
            "    CollisionPoly* wallPoly;",
            "    s8 floorBgId;",
            "    s8 wallBgId;",
            "    s16 wallYaw;",
            "    f32 floorHeight;",
            "    f32 depthInWater;",
            "    u16 bgCheckFlags;",
            "    u8 onCeiling;",
            "    s16 yawTowardsPlayer;",
            "    f32 gravity;",
            "    Vec3f projectedPos;",
            "    Vec3f prevPos;",
            "    ActorColChkInfo colChkInfo;",
            "    u16 textId;",
            "    s8 objectSlot;",
            "    void* draw;",
            "} Actor;",
            "typedef struct SkelAnime { f32 curFrame; f32 playSpeed; Vec3s* jointTable; Vec3s* morphTable; s32 limbCount; f32 morphWeight; s32 movementFlags; f32 endFrame; f32 animLength; void* animation; } SkelAnime;",
            "typedef struct ColliderBase { u32 atFlags; u32 acFlags; u32 ocFlags1; s32 colMaterial; Actor* at; Actor* ac; Actor* oc; } ColliderBase;",
            "typedef struct ColliderProbe { ColliderBase base; } ColliderProbe;",
            "typedef ColliderProbe ColliderQuad;",
            "typedef struct Input { ButtonState press; ButtonState cur; struct { s8 stick_x; s8 stick_y; } rel; } Input;",
            "typedef struct BottleSwingInfo {",
            "    void* missAnimation;",
            "    void* catchAnimation;",
            "    s16 itemId;",
            "    s16 actionParam;",
            "    s16 firstActiveFrame;",
            "    s16 numActiveFrames;",
            "    f32 radius;",
            "} BottleSwingInfo;",
            "typedef struct BottleCatchInfo { s16 actorId; s16 itemId; s16 itemAction; u16 textId; } BottleCatchInfo;",
            "typedef struct BottleDropInfo { s16 actorId; s16 params; s16 actorParams; } BottleDropInfo;",
            "typedef struct AnimSfxEntry { s32 sfxId; s32 frame; } AnimSfxEntry;",
            "typedef struct MeleeWeaponInfo { Vec3f tip; Vec3f base; Vec3f posA; Vec3f posB; s32 active; } MeleeWeaponInfo;",
            "typedef MeleeWeaponInfo WeaponInfo;",
            "typedef struct BossMoGrabPosRot { Vec3f pos; Vec3s rot; } BossMoGrabPosRot;",
            "typedef struct Camera { Vec3f eye; Vec3f at; Vec3f eyeNext; } Camera;",
            "typedef struct Color_RGB8 { u8 r; u8 g; u8 b; } Color_RGB8;",
            "typedef struct GetItemEntry { s16 gi; s16 itemId; s16 objectId; } GetItemEntry;",
            "typedef struct CsContext { s32 state; s32 curFrame; void* playerCue; } CsContext;",
            "typedef void Gfx;",
            "typedef struct FallImpactInfo { s32 damage; s32 rumbleStrength; s32 rumbleDuration; s32 rumbleDecreaseRate; s32 sfxId; } FallImpactInfo;",
            "typedef struct DynaPolyActor { Actor actor; } DynaPolyActor;",
            "typedef struct BowSlingshotStringData { Gfx* dList; Vec3f pos; } BowSlingshotStringData;",
            "typedef struct BunnyEarKinematics { Vec3s rot; Vec3s angVel; Vec3s angAccel; } BunnyEarKinematics;",
            "typedef struct struct_80854190 { void* unk_04; void* unk_08; s16 unk_0C; s16 unk_0D; } struct_80854190;",
            "typedef struct BossVa {",
            "    Actor actor;",
            "    SkelAnime skelAnime;",
            "    s16 unk_1B0;",
            "    Vec3f unk_1D8;",
            "    s16 unk_1E4; s16 unk_1E6; s16 unk_1E8; s16 unk_1EA;",
            "    s16 unk_1EC; s16 unk_1EE; s16 unk_1F0; s16 unk_1F2; s16 unk_1F4;",
            "    s16 headRotY;",
            "    s16 headRotZ;",
            "    Vec3s headRot;",
            "    f32 x;",
            "    f32 z;",
            "    Vec3f armTip;",
            "    Vec3f zapNeckPos;",
            "    Vec3f zapHeadPos;",
            "    Vec3f burstPos;",
            "    ColliderProbe colliderLightning;",
            "    s16 timer2;",
            "    u8 burst;",
            "    u8 isDead;",
            "    u8 onCeiling;",
            "} BossVa;",
            "typedef struct EnBoom { Actor actor; Actor* moveTo; s32 returnTimer; } EnBoom;",
            "typedef struct BossMoEffect { Vec3f pos; Vec3f velocity; f32 scale; } BossMoEffect;",
            "typedef struct BossMo {",
            "    Actor actor;",
            "    struct BossMo* otherTent;",
            "    struct BossMo* tent2;",
            "    struct BossMo* tent3;",
            "    void* actionFunc;",
            "    void* tentCollider;",
            "    Vec3f tentPos[20];",
            "    Vec3f unk_478;",
            "    Vec3f basePos;",
            "    Vec3f targetPos;",
            "    Vec3f eyePos;",
            "    f32 drawActorScale;",
            "    f32 tentMaxAngle;",
            "    f32 tentSpeed;",
            "    s16 tentRippleTimer;",
            "    s16 timer;",
            "    s16 subCamId;",
            "    u8 tentState;",
            "    s32 work[256];",
            "    f32 fwork[256];",
            "    s16 timers[64];",
            "    s16 sfxTimer;",
            "    s16 xSwing;",
            "    s16 zSwing;",
            "    Vec3f tentTipPos;",
            "    Vec3s tentRot[32];",
            "    s32 csState;",
            "    s32 drawActor;",
            "    s16 baseBubblesTimer;",
            "    f32 tentRippleSize;",
            "    f32 baseAlpha;",
            "    f32 waterLevelMod;",
            "    Vec3f tentStretch[32];",
            "    s16 attackAngleMod;",
            "    s16 playerHitTimer;",
            "    s32 playerToLeft;",
            "    BossMoGrabPosRot grabPosRot;",
            "    s16 mashCounter;",
            "    Vec3f subCamEye;",
            "    Vec3f subCamAt;",
            "    s16 subCamYaw;",
            "    f32 subCamYawRate;",
            "    f32 tentPulse;",
            "    s16 meltIndex;",
            "    f32 cutScale;",
            "    s16 cutIndex;",
            "    f32 waterLevel;",
            "    s32 noBubbles;",
            "    f32 flattenRate;",
            "    s16 tentSpawnPos;",
            "    s16 hitCount;",
            "} BossMo;",
            "typedef struct PlayerAgeProperties {",
            "    f32 unk_08;",
            "    f32 ceilingCheckHeight; f32 unk_0C; f32 unk_14; f32 unk_18; f32 unk_1C; f32 wallCheckRadius;",
            "    s16 unk_92;",
            "} PlayerAgeProperties;",
            "typedef struct PlayerWaterState {",
            "    s32 inWater;",
            "    s32 bottleCatchType;",
            "    s32 startedTextbox;",
            "    s32 actionVar1;",
            "    s32 actionVar2;",
            "    s32 facingUpSlope;",
            "    s32 csDelayTimer;",
            "    s32 isLakeHyliaCs;",
            "    s32 playedLandingSfx;",
            "    s32 appearTimer;",
            "    s32 fallDamageStunTimer;",
            "    s32 bonked;",
            "} PlayerWaterState;",
            "typedef struct Player {",
            "    Actor actor;",
            "    SkelAnime skelAnime;",
            "    SkelAnime upperSkelAnime;",
            "    MeleeWeaponInfo meleeWeaponInfo[4];",
            "    u32 stateFlags1;",
            "    u32 stateFlags2;",
            "    u8 stateFlags3;",
            "    s8 currentBoots;",
            "    void* actionFunc;",
            "    void* upperActionFunc;",
            "    PlayerAgeProperties* ageProperties;",
            "    void* giObjectSegment;",
            "    void* giObjectLoadQueue;",
            "    Actor* interactRangeActor;",
            "    Actor* heldActor;",
            "    Actor* heldItemAction;",
            "    s16 yaw;",
            "    s16 ledgeYawTarget;",
            "    s8 ledgeClimbType;",
            "    s8 ledgeClimbDelayTimer;",
            "    s8 ledgeDelayTimer;",
            "    s8 ledgeSide;",
            "    f32 wallSpeedLimit;",
            "    f32 yDistToLedge;",
            "    f32 distToInteractWall;",
            "    f32 speedXZ;",
            "    f32 unk_880;",
            "    s32 floorProperty;",
            "    s32 prevFloorType;",
            "    s32 floorTypeTimer;",
            "    s16 floorPitch;",
            "    s16 floorPitchAlt;",
            "    s32 floorSfxOffset;",
            "    s32 prevFloorSfxOffset;",
            "    s32 unk_A84;",
            "    s32 unk_862;",
            "    s32 unk_845;",
            "    s32 unk_868;",
            "    s32 unk_864;",
            "    s32 modelAnimType;",
            "    s32 heldItemActionParam;",
            "    s32 leftHandType;",
            "    s32 rightHandType;",
            "    s32 sheathType;",
            "    s32 currentShield;",
            "    s32 itemAction;",
            "    Vec3f leftHandPos;",
            "    Gfx** leftHandDLists;",
            "    Gfx** rightHandDLists;",
            "    Gfx** sheathDLists;",
            "    Gfx** waistDLists;",
            "    PlayerWaterState av1;",
            "    PlayerWaterState av2;",
            "    s16 turnRate;",
            "    s16 unk_89C;",
            "    u8 unk_6AD;",
            "    u8 unk_6AE_rotFlags;",
            "    s16 unk_6C2;",
            "    f32 unk_6C4;",
            "    f32 unk_85C;",
            "    f32 unk_858;",
            "    f32 unk_860;",
            "    f32 unk_834;",
            "    s16 unk_87C;",
            "    Vec3s upperLimbRot;",
            "    Vec3s headLimbRot;",
            "    s16 upperLimbYawSecondary;",
            "    s32 meleeWeaponAnimation;",
            "    s32 meleeWeaponEffectIndex;",
            "    ColliderProbe meleeWeaponQuads[4];",
            "    Vec3f bodyPartsPos[32];",
            "    void* blendTable;",
            "    s32 giObjectLoading;",
            "    Actor* naviActor;",
            "    s16 naviTextId;",
            "    s32 meleeWeaponState;",
            "    s32 fallDistance;",
            "    s16 knockbackRot;",
            "    f32 knockbackSpeed;",
            "    s32 knockbackType;",
            "    s16 exchangeItemId;",
            "    Actor* talkActor;",
            "    Actor* focusActor;",
            "    ColliderProbe cylinder;",
            "    u8 unk_A87;",
            "    s32 currentTunic;",
            "    s16 controlStickDirections[16];",
            "    s16 controlStickDataIndex;",
            "    s16 getItemId;",
            "    void (*afterPutAwayFunc)(void*, void*);",
            "    s32 bodyShockTimer;",
            "    s32 csAction;",
            "    Vec3s unk_3BC;",
            "    MtxF mf_9E0;",
            "    MtxF shieldMf;",
            "    ColliderQuad shieldQuad;",
            "    Vec3f unk_3C8;",
            "} Player;",
            "typedef struct MessageContext {",
            "    void* play;",
            "    u8 msgMode;",
            "    u8 state;",
            "    u16 textId;",
            "    s32 msgLength;",
            "    s32 textboxEndType;",
            "    s32 stateTimer;",
            "    s32 choiceIndex;",
            "    s32 textBoxType;",
            "    s32 textBoxPos;",
            "    s32 textBoxProperties;",
            "    s32 textDrawPos;",
            "    s32 decodedTextLen;",
            "    s32 textUnskippable;",
            "    s32 ocarinaMode;",
            "    Actor* talkActor;",
            "    s32 msgBufPos;",
            "    s32 ocarinaAction;",
            "    s32 unk_E3F2;",
            "    s32 lastOcarinaButtonIndex;",
            "} MessageContext;",
            "typedef struct InterfaceContext { s32 prevHudVisibilityMode; s32 info; s16 unk_1FA; s16 unk_1FC; } InterfaceContext;",
            "typedef struct VtxInner { s16 ob[3]; u16 flag; s16 tc[2]; u8 cn[4]; } VtxInner;",
            "typedef struct Vtx { VtxInner v; } Vtx;",
            "typedef struct PauseContext {",
            "    s16 pageIndex;",
            "    s16 alpha;",
            "    s16 state;",
            "    s16 mainState;",
            "    s16 debugState;",
            "    s16 cursorSpecialPos;",
            "    s16 infoPanelOffsetY;",
            "    s16 cursorPoint[5];",
            "    s16 cursorX[5];",
            "    s16 cursorY[5];",
            "    Vtx cursorVtx[64];",
            "    s16 cursorSlot[5];",
            "    s16 namedItem;",
            "    s16 nameColorSet;",
            "    void* nameSegment;",
            "    s16 nameDisplayTimer;",
            "    Vtx uiOverlayVtx[128];",
            "} PauseContext;",
            "typedef struct GameState { Input input[4]; void* gfxCtx; } GameState;",
            "typedef struct PlayState {",
            "    GameState state;",
            "    s32 gameplayFrames;",
            "    s16 sceneId;",
            "    struct { struct { Actor* head; } actorLists[16]; s8 unk_02; s8 curRoom; } actorCtx;",
            "    void* colCtx;",
            "    void* colChkCtx;",
            "    Actor* playerActor;",
            "    Input input[4];",
            "    void* specialEffects;",
            "    PauseContext pauseCtx;",
            "    MessageContext msgCtx;",
            "    InterfaceContext interfaceCtx;",
            "    CsContext csCtx;",
            "    s16 activeCamId;",
            "    s32 shootingGalleryStatus;",
            "    s32 transitionTrigger;",
            "    s32 transitionType;",
            "    s32 nextEntranceIndex;",
            "    struct { struct { s32 environmentType; s32 num; } curRoom; } roomCtx;",
            "    struct { s32 state; } gameOverCtx;",
            "    MtxF viewProjectionMtxF;",
            "    struct { struct { void* segment; } slots[256]; } objectCtx;",
            "    s32 (*grabPlayer)(struct PlayState*, Player*);",
            "    s32 (*damagePlayer)(struct PlayState*, s32);",
            "} PlayState;",
            "static Player gProbePlayer;",
            "static Actor gProbeActor;",
            "static BossVa gProbeBody;",
            "static Input gProbeControlInput;",
            "static Input* sControlInput = &gProbeControlInput;",
            "#ifndef GET_PLAYER",
            "#define GET_PLAYER(play) (&gProbePlayer)",
            "#endif",
            "#ifndef GET_BODY",
            "#define GET_BODY(this_) (&gProbeBody)",
            "#endif",
            "#ifndef oot3d_boss_va_body_actor",
            "#define oot3d_boss_va_body_actor(this_) (&gProbeBody)",
            "#endif",
            "#ifndef FALLTHROUGH",
            "#define FALLTHROUGH ((void)0)",
            "#endif",
            "#ifndef ABS",
            "#define ABS(x) ((x) < 0 ? -(x) : (x))",
            "#endif",
            "#ifndef ARRAY_COUNT",
            "#define ARRAY_COUNT(x) ((s32)(sizeof(x) / sizeof((x)[0])))",
            "#endif",
            "#ifndef COLPOLY_GET_NORMAL",
            "#define COLPOLY_GET_NORMAL(x) ((f32)(x))",
            "#endif",
            "#ifndef CONVEYOR_DIRECTION_TO_BINANG",
            "#define CONVEYOR_DIRECTION_TO_BINANG(x) (x)",
            "#endif",
            "#ifndef SQ",
            "#define SQ(x) ((x) * (x))",
            "#endif",
            "#ifndef LANGUAGE_MAX",
            "#define LANGUAGE_MAX 4",
            "#endif",
            "#ifndef LANGUAGE_ARRAY",
            "#define LANGUAGE_ARRAY(jpn, eng, ger, fra) { jpn, eng, ger, fra }",
            "#endif",
            "#ifndef OPEN_DISPS",
            "#define OPEN_DISPS(...) ((void)0)",
            "#endif",
            "#ifndef CLOSE_DISPS",
            "#define CLOSE_DISPS(...) ((void)0)",
            "#endif",
            "#ifndef ANIMSFX_DATA",
            "#define ANIMSFX_DATA(type, frame) (frame)",
            "#endif",
            "#ifndef MELEE_WEAPON_INFO_TIP",
            "#define MELEE_WEAPON_INFO_TIP(info) (&((info)->tip))",
            "#endif",
            "#ifndef MELEE_WEAPON_INFO_BASE",
            "#define MELEE_WEAPON_INFO_BASE(info) (&((info)->base))",
            "#endif",
            "#ifndef GET_PLAYER_ANIM",
            "#define GET_PLAYER_ANIM(group, type) ((void*)0)",
            "#endif",
            "#ifndef CHECK_BTN_ALL",
            "#define CHECK_BTN_ALL(cur, mask) (((cur) & (mask)) == (mask))",
            "#endif",
            "#ifndef T",
            "#define T(jpn, eng) (eng)",
            "#endif",
            "#ifndef G_CC_MODULATEIA_PRIM",
            "#define G_CC_MODULATEIA_PRIM 0",
            "#endif",
            "#ifndef G_TX_RENDERTILE",
            "#define G_TX_RENDERTILE 0",
            "#endif",
            "#ifndef G_IM_FMT_IA",
            "#define G_IM_FMT_IA 0",
            "#endif",
            "#ifndef G_IM_SIZ_8b",
            "#define G_IM_SIZ_8b 0",
            "#endif",
            "#ifndef G_TX_NOMIRROR",
            "#define G_TX_NOMIRROR 0",
            "#endif",
            "#ifndef G_TX_CLAMP",
            "#define G_TX_CLAMP 0",
            "#endif",
            "#ifndef G_TX_NOMASK",
            "#define G_TX_NOMASK 0",
            "#endif",
            "#ifndef G_TX_NOLOD",
            "#define G_TX_NOLOD 0",
            "#endif",
            "#ifndef OOT3D_DIRECT_FIELD",
            "#define OOT3D_DIRECT_FIELD(base, offset) (*(s32*)((u8*)(base) + (offset)))",
            "#endif",
            "",
            "typedef void (*OverrideLimbDrawOpa)(void);",
            "typedef void (*PostLimbDrawOpa)(void);",
            "void BossVa_ZapperDamaged(BossVa* this, PlayState* play);",
            "",
            "#define PLAYER_LEDGE_CLIMB_NONE 0",
            "#define PLAYER_LEDGE_CLIMB_1 1",
            "#define PLAYER_LEDGE_CLIMB_2 2",
            "#define PLAYER_LEDGE_CLIMB_3 3",
            "#define PLAYER_LEDGE_CLIMB_4 4",
            "#define PLAYER_STATE1_0 1u",
            "#define PLAYER_STATE1_26 0x04000000u",
            "#define PLAYER_STATE1_29 0x20000000u",
            "#define PLAYER_STATE1_31 0x80000000u",
            "#define PLAYER_STATE1_DEAD 0x00000080u",
            "#define PLAYER_STATE2_CRAWLING 0x00040000u",
            "#define PLAYER_STATE2_FORCE_SAND_FLOOR_SOUND 0x00000200u",
            "#define PLAYER_BOOTS_IRON 1",
            "#define ACTORCAT_PLAYER 2",
            "#define BGCHECK_SCENE 0x32",
            "#define BGCHECKFLAG_GROUND 0x0001",
            "#define BGCHECKFLAG_WALL 0x0008",
            "#define BGCHECKFLAG_CEILING 0x0010",
            "#define BGCHECKFLAG_WATER 0x0020",
            "#define BGCHECKFLAG_PLAYER_WALL_INTERACT 0x0200",
            "#define UPDBGCHECKINFO_FLAG_0 0x01",
            "#define UPDBGCHECKINFO_FLAG_1 0x02",
            "#define UPDBGCHECKINFO_FLAG_2 0x04",
            "#define UPDBGCHECKINFO_FLAG_3 0x08",
            "#define UPDBGCHECKINFO_FLAG_4 0x10",
            "#define UPDBGCHECKINFO_FLAG_5 0x20",
            "#define CONVEYOR_SPEED_DISABLED 0",
            "#define SURFACE_SFX_OFFSET_WATER_SHALLOW 4",
            "#define SURFACE_SFX_OFFSET_WATER_DEEP 5",
            "#define SURFACE_SFX_OFFSET_SAND 1",
            "#define LEDGE_DIST_MAX 9999.0f",
            "#define R_RUN_SPEED_LIMIT 100.0f",
            "#define R_UPDATE_RATE 1.0f",
            "#define DEBUG_FEATURES 0",
            "#define OOT_NTSC 0",
            "#define PLATFORM_IQUE 0",
            "#define GC_US 0",
            "#define OOT_VERSION 1",
            "",
            "#define PHASE_4 4",
            "#define BOSSVA_ZAPPER_1 3",
            "#define BOSSVA_ZAPPER_2 4",
            "#define BOSSVA_ZAPPER_3 5",
            "#define DEATH_BODY_TUMORS 1",
            "#define DEATH_ZAPPER_1 2",
            "#define DEATH_ZAPPER_2 3",
            "#define DEATH_ZAPPER_3 4",
            "#define TUMOR_ARM 0",
            "#define SPARK_BODY 0",
            "#define NA_SE_EN_BALINADE_BREAK2 0",
            "#define NA_SE_EN_BALINADE_BL_SPARK 0",
            "#define NA_SE_EN_BALINADE_HIT_RINK 0",
            "#define NA_SE_EN_BALINADE_THUNDER 0",
            "#define AT_HIT 1",
            "#define SPARK_LINK 1",
            "#define SPARK_BLAST 2",
            "#define PHASE_DEATH 99",
            "#define INTRO_TITLE 0",
            "#define INTRO_BRIGHTEN 1",
            "#define INTRO_FINISH 2",
            "#define BOSSVA_BATTLE 3",
            "#define CAM_ID_MAIN 0",
            "#define ITEM_BOTTLE_EMPTY 0",
            "#define PLAYER_IA_BOTTLE 0",
            "#define NA_SE_EV_BOTTLE_CAP_OPEN 0",
            "#define MSGMODE_TEXT_START 0",
            "#define SCENE_CAM_TYPE_FIXED_MARKET 1",
            "#define SCENE_CAM_TYPE_DEFAULT 0",
            "#define MSGMODE_NONE 0",
            "#define MSGMODE_TEXT_STARTING 1",
            "#define MSGMODE_TEXT_BOX_GROWING 2",
            "#define MSGMODE_TEXT_NEXT_MSG 3",
            "#define MSGMODE_TEXT_DISPLAYING 4",
            "#define MSGMODE_TEXT_CONTINUING 5",
            "#define MSGMODE_TEXT_AWAIT_INPUT 6",
            "#define MSGMODE_TEXT_DELAYED_BREAK 7",
            "#define MSGMODE_OCARINA_STARTING 8",
            "#define TEXTBOX_ENDTYPE_FADING 0",
            "#define TEXTBOX_ENDTYPE_PERSISTENT 1",
            "#define TEXTBOX_ENDTYPE_EVENT 2",
            "#define TEXTBOX_ENDTYPE_2_CHOICE 3",
            "#define TEXTBOX_ENDTYPE_HAS_NEXT 4",
            "#define TEXTBOX_ENDTYPE_DEFAULT 0",
            "#define TEXTBOX_POS_TOP 0",
            "#define TEXTBOX_POS_MIDDLE 1",
            "#define TEXTBOX_TYPE_NONE_BOTTOM 0",
            "#define TEXTBOX_TYPE_NONE_NO_SHADOW 1",
            "#define TEXT_STATE_CLOSING 0",
            "#define BOTTLE_CATCH_NONE 0",
            "#define BTN_DDOWN 0x1",
            "#define BTN_L 0x2",
            "#define BTN_B 0x4",
            "#define CAM_ITEM_TYPE_4 4",
            "#define FAIRY_REVIVE_BOTTLE 0",
            "#define UI_OVERLAY_QUAD_MAX 24",
            "#define UI_OVERLAY_QUAD_BUTTON_LR_WIDTH 32",
            "#define UI_OVERLAY_QUAD_BUTTON_LR_HEIGHT 16",
            "#define PAUSE_CURSOR_PAGE_LEFT -1",
            "#define PAUSE_CURSOR_PAGE_RIGHT -2",
            "#define PAUSE_MAIN_STATE_IDLE 0",
            "#define PLAYER_STATE1_14 0x00004000u",
            "#define PLAYER_STATE1_18 0x00040000u",
            "#define PLAYER_STATE1_27 0x08000000u",
            "#define PLAYER_STATE1_28 0x10000000u",
            "#define PLAYER_STATE1_CARRYING_ACTOR 0x00000800u",
            "#define PLAYER_STATE1_CHARGING_SPIN_ATTACK 0x00002000u",
            "#define PLAYER_STATE1_SWINGING_BOTTLE 0x00001000u",
            "#define PLAYER_STATE2_5 0x20u",
            "#define PLAYER_STATE2_6 0x40u",
            "#define PLAYER_STATE3_0 0x01",
            "#define PLAYER_STATE3_4 0x10",
            "#define NA_BGM_ITEM_GET 0",
            "#define NA_SE_NONE 0",
            "#define NA_SE_IT_SCOOP_UP_WATER 0",
            "#define NA_SE_EV_FIATY_HEAL 0",
            "#define SFX_FLAG 0",
            "#define ANIMSFX_TYPE_VOICE 0",
            "#define ANIMSFX_TYPE_GENERAL 1",
            "#define PLAYER_IA_BOTTLE_FISH 0",
            "#define ACTOR_EN_FISH 0",
            "#define ACTOR_EN_ICE_HONO 0",
            "#define ACTOR_EN_INSECT 0",
            "#define FISH_DROPPED 0",
            "#define INSECT_TYPE_FIRST_DROPPED 0",
            "#define SCENE_CASTLE_COURTYARD_GUARDS_DAY 0",
            "#define SCENE_MARKET_DAY 0",
            "#define SCENE_MARKET_NIGHT 0",
            "#define SCENE_MARKET_RUINS 0",
            "#define CS_INDEX_0 0",
            "#define DO_ACTION_NEXT 0",
            "#define HUD_VISIBILITY_NOTHING 0",
            "",
            "static s32 sCsState;",
            "static s32 sFightPhase;",
            "static s32 sBodyState;",
            "static Vec3f sSubCamAt;",
            "static Vec3f sSubCamEye;",
            "static s32 sSubCamId;",
            "static s32 sPrevFloorProperty;",
            "static f32 sYDistToFloor;",
            "static s32 sConveyorSpeed;",
            "static s32 sIsFloorConveyor;",
            "static s16 sConveyorYaw;",
            "static s32 sTouchedWallFlags;",
            "static s32 sShapeYawToTouchedWall;",
            "static s32 sWorldYawToTouchedWall;",
            "static s32 sInteractWallCheckResult;",
            "static s32 sMessageStartFrameCount;",
            "static s32 sFloorType;",
            "static s16 sFloorShapePitch;",
            "static s32 sTextFade;",
            "static s32 sTextboxSkipped;",
            "static Vec3f sEffects[16];",
            "static s32 sUpperBodyIsBusy;",
            "static void* Player_Action_80845EF8;",
            "static void* Player_Action_80845CA4;",
            "typedef void LinkAnimationHeader;",
            "static s32 sUpperBodyLimbCopyMap[32];",
            "static LinkAnimationHeader* D_80853D4C[16][4];",
            "static s32 D_80854384[16];",
            "static s32 D_80854380[16];",
            "static s32 D_80854368[16];",
            "static s32 D_80854360[16];",
            "static s32 D_80854370[16];",
            "static s32 D_80854378[16];",
            "static struct_80854190 D_80854190[16];",
            "static BunnyEarKinematics sBunnyEarKinematics;",
            "static void* sActionHandlerListTurnInPlace;",
            "static void* sActionHandlerList5;",
            "static void* sActionHandlerList8;",
            "static void* sActionHandlerList9;",
            "static void* sActionHandlerList10;",
            "static s32 sUseHeldItem;",
            "static s16 sNextTextId;",
            "static s16 sLastPlayedSong;",
            "static BossMo* sMorphaCore;",
            "static BossMo* sMorphaTent1;",
            "static BossMo* sMorphaTent2;",
            "static s16 sTentSpawnIndex[32];",
            "static Vec3f sTentSpawnPos[32];",
            "static s16 gMorphaTransposeTable[16];",
            "static s16 sAttackRot[32];",
            "static s16 sCurlRot[32];",
            "static s16 sGrabRot[32];",
            "static s32 sInDungeonScene;",
            "static u8 gAreaGsFlags[256];",
            "static s32 sDListsLodOffset;",
            "static s32 sLeftHandType;",
            "static s32 sRightHandType;",
            "static Color_RGB8 sTunicColors[8];",
            "static Color_RGB8 sGauntletColors[8];",
            "static struct { s32 eyeIndex; s32 mouthIndex; } sPlayerFaces[32];",
            "static void* sEyeTextures[4][32];",
            "static void* sMouthTextures[4][32];",
            "static void* sBootDListGroups[8][8];",
            "static Vec3f* sCurBodyPartPos;",
            "static Gfx* gPlayerLeftHandOpenDLs[8];",
            "static Gfx* gPlayerLeftHandClosedDLs[8];",
            "static Gfx* sPlayerRightHandClosedDLs[8];",
            "static Gfx* sFirstPersonLeftForearmDLs[8];",
            "static Gfx* sFirstPersonLeftHandDLs[8];",
            "static Gfx* sFirstPersonRightShoulderDLs[8];",
            "static Gfx* sFirstPersonForearmDLs[8];",
            "static Gfx* sFirstPersonRightHandHoldingWeaponDLs[8];",
            "static Gfx* D_80125D28[32];",
            "static uintptr_t gSegments[16];",
            "static Vec3f sGetItemRefPos;",
            "s32 Player_OverrideLimbDrawGameplayFirstPerson(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* thisx);",
            "s32 Player_OverrideLimbDrawGameplayCrawling(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* thisx);",
            "static GetItemEntry sGetItemTable[256];",
            "static s16 D_80854528[256];",
            "void func_80834B5C(Player* this, PlayState* play);",
            "void Player_Draw(Actor* actor, PlayState* play);",
            "void Player_Action_80840450(Player* this, PlayState* play);",
            "void Player_Action_80843188(Player* this, PlayState* play);",
            "void Player_Action_80843954(Player* this, PlayState* play);",
            "void Player_Action_80843A38(Player* this, PlayState* play);",
            "void Player_Action_80844E68(Player* this, PlayState* play);",
            "void Player_Action_80845000(Player* this, PlayState* play);",
            "void Player_Action_80845308(Player* this, PlayState* play);",
            "void Player_Action_808505DC(Player* this, PlayState* play);",
            "void Player_Action_CsAction(Player* this, PlayState* play);",
            "extern volatile s32 R_TEXTBOX_X;",
            "extern volatile s32 R_TEXTBOX_Y;",
            "extern volatile s32 R_TEXTBOX_X_TARGET;",
            "extern volatile s32 R_TEXTBOX_Y_TARGET;",
            "extern volatile s32 R_TEXTBOX_WIDTH;",
            "extern volatile s32 R_TEXTBOX_HEIGHT;",
            "extern volatile s32 R_TEXTBOX_TEXWIDTH;",
            "extern volatile s32 R_TEXTBOX_TEXHEIGHT;",
            "extern volatile s32 R_TEXTBOX_END_YPOS;",
            "extern volatile s32 R_TEXT_CHOICE_YPOS_VALUES[4];",
            "#ifndef R_TEXT_CHOICE_YPOS",
            "#define R_TEXT_CHOICE_YPOS(index) (R_TEXT_CHOICE_YPOS_VALUES[(index)])",
            "#endif",
            "typedef struct ProbeInventory { u32 questItems; } ProbeInventory;",
            "typedef struct ProbePlayerData { f32 swordHealth; u8 bgsFlag; s16 healthCapacity; s16 health; } ProbePlayerData;",
            "typedef struct ProbeEquips { s16 buttonItems[4]; s16 cButtonSlots[3]; s16 equipment; } ProbeEquips;",
            "typedef struct ProbeFaroresWind { s32 set; Vec3f pos; s16 yaw; s16 playerParams; s16 entranceIndex; s8 roomIndex; u32 tempSwchFlags; u32 tempCollectFlags; } ProbeFaroresWind;",
            "typedef struct ProbeRespawnData { Vec3f pos; Vec3s rot; s16 yaw; s16 entranceIndex; s8 roomIndex; u32 tempSwchFlags; u32 tempCollectFlags; s32 data; } ProbeRespawnData;",
            "typedef struct ProbeSaveInfo { s32 linkAge; s32 gameMode; s32 language; ProbeInventory inventory; ProbePlayerData playerData; ProbeEquips equips; ProbeFaroresWind fw; } ProbeSaveInfo;",
            "struct { struct { s32 cutsceneIndex; s32 linkAge; ProbeSaveInfo info; } save; s32 healthAccumulator; s32 prevHudVisibilityMode; s32 hudVisibilityMode; s32 language; s32 gameMode; s32 nextTransitionType; s32 seqId; s32 natureAmbienceId; ProbeRespawnData respawn[8]; s32 respawnFlag; s32 magicState; } oot3d_global_state_packets;",
            "",
            *define_lines,
            "",
        ]
    )


def find_tool(name: str, prefix: str) -> str:
    exe = f"{prefix}-{name}"
    found = shutil.which(exe)
    if found:
        return found
    candidates = []
    devkitarm = None
    # Avoid importing os only for one environment lookup in normal paths.
    import os

    devkitarm = os.environ.get("DEVKITARM")
    if devkitarm:
        candidates.append(Path(devkitarm) / "bin" / f"{exe}.exe")
    candidates.append(Path("C:/devkitPro/devkitARM/bin") / f"{exe}.exe")
    for msys in ("mingw64", "ucrt64", "clang64"):
        candidates.append(Path(f"C:/msys64/{msys}/bin") / f"{exe}.exe")
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    raise RuntimeError(f"could not find {exe}")


def compile_command(gcc: str, source: Path, obj: Path, optimization: str) -> list[str]:
    return [
        gcc,
        "-std=gnu99",
        optimization,
        "-g",
        "-Wall",
        "-Wno-implicit-function-declaration",
        "-Wno-int-conversion",
        "-Wno-incompatible-pointer-types",
        "-Wno-pointer-to-int-cast",
        "-Wno-int-to-pointer-cast",
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
        "-fmax-errors=80",
        "-fdiagnostics-color=never",
        "-c",
        str(source),
        "-o",
        str(obj),
    ]


def safe_stem(packet: Packet) -> str:
    text = f"{packet.entry}_{packet.name}"
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", text).strip("_")


def normalize_probe_source(text: str) -> str:
    normalized = text
    for old, new in sorted(DIRECT_FIELD_REPLACEMENTS.items(), key=lambda item: -len(item[0])):
        normalized = normalized.replace(old, new)
    normalized = re.sub(
        r"OOT3D_DIRECT_FIELD\(\s*this\s*,\s*oot3d_boss_va_joint_table\s*\)",
        "((Vec3s*)this->skelAnime.jointTable)",
        normalized,
    )
    normalized = re.sub(
        r"OOT3D_DIRECT_FIELD\(\s*msgCtx\s*,\s*DAT_00343ec4\s*\)",
        "msgCtx->textboxEndType",
        normalized,
    )
    normalized = re.sub(
        r"OOT3D_DIRECT_FIELD\(\s*msgCtx\s*,\s*DAT_00343edc\s*\)",
        "msgCtx->textId",
        normalized,
    )
    normalized = re.sub(
        r"OOT3D_DIRECT_FIELD\(\s*msgCtx\s*,\s*DAT_00343e[0-9a-fA-F]{2}\s*\)",
        "msgCtx->stateTimer",
        normalized,
    )
    return normalized


def probe_brace_depth(text: str) -> int:
    depth = 0
    i = 0
    state = "code"
    while i < len(text):
        char = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if char == "/" and nxt == "/":
                state = "line-comment"
                i += 2
                continue
            if char == "/" and nxt == "*":
                state = "block-comment"
                i += 2
                continue
            if char == '"':
                state = "string"
                i += 1
                continue
            if char == "'":
                state = "char"
                i += 1
                continue
            if char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
        elif state == "line-comment":
            if char == "\n":
                state = "code"
        elif state == "block-comment":
            if char == "*" and nxt == "/":
                state = "code"
                i += 2
                continue
        elif state == "string":
            if char == "\\":
                i += 2
                continue
            if char == '"':
                state = "code"
        elif state == "char":
            if char == "\\":
                i += 2
                continue
            if char == "'":
                state = "code"
        i += 1
    return depth


def balance_probe_braces(text: str) -> str:
    missing_closures = probe_brace_depth(text)
    if missing_closures <= 0:
        return text
    closures = "\n".join("}" for _ in range(missing_closures))
    return text.rstrip() + "\n\n/* Probe-only closure for truncated direct packet slices. */\n" + closures + "\n"


def extra_probe_declarations(text: str) -> str:
    lines: list[str] = []
    texture_tokens: set[str] = set()
    integer_tokens: set[str] = set()
    generated_symbols: set[str] = set()

    for token in sorted(set(GLOBAL_ASSET_RE.findall(text))):
        if token.endswith("_WIDTH") or token.endswith("_HEIGHT"):
            integer_tokens.add(token)
        else:
            texture_tokens.add(token)

    for token in sorted(integer_tokens):
        lines.append(f"#ifndef {token}")
        lines.append(f"#define {token} 32")
        lines.append("#endif")
        generated_symbols.add(token)
    for token in sorted(texture_tokens):
        lines.append(f"extern const u8 {token}[];")
        generated_symbols.add(token)

    for token in sorted(PROBE_DISPLAY_LIST_POINTERS & set(ALL_CAPS_TOKEN_RE.findall(text))):
        lines.append(f"static u32 {token};")
        generated_symbols.add(token)

    for lane in sorted(set(PAUSE_REG_LANE_RE.findall(text))):
        token = f"oot3d_pause_reg_{lane}_lane"
        array_name = f"PROBE_{token}_VALUES"
        lines.append(f"extern volatile s32 {array_name}[256];")
        lines.append(f"#ifndef {token}")
        lines.append(f"#define {token}(index) ({array_name}[(index)])")
        lines.append("#endif")

    assigned_scalars = set(R_SCALAR_ASSIGN_RE.findall(text)) - PROBE_PRELUDE_VARIABLES
    indexed_r_macros = set(R_INDEXED_USE_RE.findall(text))
    for token in sorted(assigned_scalars):
        lines.append(f"extern volatile s32 {token};")
        lines.append(f"#ifndef {token}")
        lines.append(f"#define {token} {token}")
        lines.append("#endif")
        generated_symbols.add(token)
    for token in sorted(indexed_r_macros):
        array_name = f"PROBE_{token}_VALUES"
        lines.append(f"extern volatile s32 {array_name}[256];")
        lines.append(f"#ifndef {token}")
        lines.append(f"#define {token}(index) ({array_name}[(index)])")
        lines.append("#endif")
        generated_symbols.add(token)

    for token in sorted(MELEE_OFFSET_SYMBOLS):
        if not re.search(rf"\b{re.escape(token)}\b", text):
            continue
        if re.search(rf"\b(?:static\s+|extern\s+)?(?:const\s+)?Vec3f\s+{re.escape(token)}\b", text):
            continue
        lines.append(f"static Vec3f {token};")
        generated_symbols.add(token)

    constant_tokens = (
        (set(ALL_CAPS_TOKEN_RE.findall(text)) | set(MIXED_CONSTANT_RE.findall(text)))
        - generated_symbols
        - PROBE_DISPLAY_LIST_POINTERS
        - PROBE_PRELUDE_VARIABLES
    )
    auto_value = 1
    for token in sorted(constant_tokens):
        if token.startswith(("OOT3D_", "__")):
            continue
        if re.fullmatch(r"D(?:AT)?_[0-9A-Fa-f_]+", token):
            continue
        if re.search(rf"\b{re.escape(token)}\s*\(", text):
            continue
        if token in {"NULL", "TRUE", "FALSE"}:
            continue
        if re.search(rf"#\s*define\s+{re.escape(token)}\b", text):
            continue
        lines.append(f"#ifndef {token}")
        lines.append(f"#define {token} {auto_value}")
        lines.append("#endif")
        generated_symbols.add(token)
        auto_value += 1

    # Keep this list explicit; auto-declaring every s-prefixed token collides
    # with static locals that the N64 decomp already defines in many packets.
    needed_static_arrays = {
        "sBottleSwingInfo": "static BottleSwingInfo sBottleSwingInfo[4];",
        "sBottleCatchInfo": "static BottleCatchInfo sBottleCatchInfo[8];",
    }
    for token, declaration in needed_static_arrays.items():
        if re.search(rf"\b{re.escape(token)}\b", text):
            if re.search(rf"\b(static|extern)\b[^;=]*\b{re.escape(token)}\b", text):
                continue
            lines.append(declaration)

    if not lines:
        return ""
    return "\n".join(lines) + "\n"


def write_probe_source(packet: Packet, source_dir: Path, prelude: str) -> Path:
    source_dir.mkdir(parents=True, exist_ok=True)
    out = source_dir / f"{safe_stem(packet)}.probe.c"
    original = packet.output.read_text(encoding="utf-8", errors="replace")
    normalized = balance_probe_braces(normalize_probe_source(original))
    out.write_text(prelude + "\n" + extra_probe_declarations(normalized) + normalized, encoding="utf-8")
    return out


def classify_error(message: str) -> tuple[str, str]:
    for label, pattern in (
        ("unknown-type", UNKNOWN_TYPE_RE),
        ("undeclared-symbol", UNDECLARED_RE),
        ("missing-member", NO_MEMBER_RE),
        ("member-on-nonstruct", MEMBER_ON_NONSTRUCT_RE),
        ("incomplete-typedef", INCOMPLETE_RE),
    ):
        match = pattern.search(message)
        if match:
            return label, ":".join(match.groups())
    if "lvalue required" in message:
        return "direct-field-lvalue", "OOT3D_DIRECT_FIELD"
    if "invalid type argument of '->'" in message:
        return "direct-field-pointer", "OOT3D_DIRECT_FIELD"
    if "expected" in message:
        return "syntax-shim-gap", message[:80]
    return "other-error", message[:80]


def analyze_stderr(stderr: str) -> dict[str, Any]:
    errors: list[str] = []
    warnings: list[str] = []
    blocker_counts: Counter[str] = Counter()
    blocker_examples: dict[str, Counter[str]] = defaultdict(Counter)

    for line in stderr.splitlines():
        error = ERROR_RE.search(line)
        if error:
            message = error.group("message")
            errors.append(message)
            label, token = classify_error(message)
            blocker_counts[label] += 1
            blocker_examples[label][token] += 1
            continue
        warning = WARNING_RE.search(line)
        if warning:
            warnings.append(warning.group("message"))

    primary = blocker_counts.most_common(1)[0][0] if blocker_counts else ""
    return {
        "error_count": len(errors),
        "warning_count": len(warnings),
        "primary_blocker": primary,
        "blocker_counts": dict(blocker_counts),
        "blocker_examples": {
            label: [{"token": token, "count": count} for token, count in examples.most_common(8)]
            for label, examples in sorted(blocker_examples.items())
        },
        "first_errors": errors[:10],
        "first_warnings": warnings[:10],
    }


def run_probe(packet: Packet, gcc: str, objdump: str, args: argparse.Namespace, prelude: str) -> dict[str, Any]:
    source_dir = args.build_dir / "sources"
    object_dir = args.build_dir / "objects"
    dump_dir = args.build_dir / "dumps"
    log_dir = args.build_dir / "logs"
    object_dir.mkdir(parents=True, exist_ok=True)
    dump_dir.mkdir(parents=True, exist_ok=True)
    log_dir.mkdir(parents=True, exist_ok=True)

    probe_source = write_probe_source(packet, source_dir, prelude)
    stem = safe_stem(packet)
    obj = object_dir / f"{safe_stem(packet)}.o"
    dump_path = dump_dir / f"{stem}.dump"
    for stale in (obj, dump_path):
        if stale.is_file():
            stale.unlink()

    command = compile_command(gcc, probe_source, obj, args.optimization)
    env = os.environ.copy()
    tool_dir = str(Path(gcc).parent)
    env["PATH"] = tool_dir + os.pathsep + env.get("PATH", "")
    completed = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, env=env)
    log_path = log_dir / f"{safe_stem(packet)}.stderr.txt"
    log_path.write_text(completed.stderr, encoding="utf-8")
    stdout_path = log_dir / f"{safe_stem(packet)}.stdout.txt"
    stdout_path.write_text(completed.stdout, encoding="utf-8")
    analysis = analyze_stderr(completed.stderr)
    compiled = completed.returncode == 0 and obj.is_file()
    objdump_returncode: int | None = None
    objdump_stderr_path = log_dir / f"{stem}.objdump.stderr.txt"
    dumped = False
    if compiled:
        dump_completed = subprocess.run([objdump, "-dr", str(obj)], cwd=ROOT, text=True, capture_output=True, env=env)
        objdump_returncode = dump_completed.returncode
        dump_path.write_text(dump_completed.stdout, encoding="utf-8")
        objdump_stderr_path.write_text(dump_completed.stderr, encoding="utf-8")
        dumped = dump_completed.returncode == 0 and dump_path.is_file()
    else:
        objdump_stderr_path.write_text("", encoding="utf-8")

    return {
        "entry": packet.entry,
        "name": packet.name,
        "domain": packet.domain,
        "lane": packet.lane,
        "packet": rel(packet.output),
        "probe_source": rel(probe_source),
        "object": rel(obj),
        "dump": rel(dump_path),
        "stderr_log": rel(log_path),
        "objdump_stderr_log": rel(objdump_stderr_path),
        "returncode": completed.returncode,
        "compiled": compiled,
        "dumped": dumped,
        "objdump_returncode": objdump_returncode,
        "rewrite_count": packet.rewrites,
        "adapter_count": packet.adapters,
        "shape_anchors": packet.shape_anchors,
        **analysis,
    }


def summarize(rows: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "packets": len(rows),
        "compiled": sum(1 for row in rows if row["compiled"]),
        "dumped": sum(1 for row in rows if row.get("dumped")),
        "failed": sum(1 for row in rows if not row["compiled"]),
        "domains": dict(Counter(str(row["domain"]) for row in rows)),
        "primary_blockers": dict(Counter(str(row["primary_blocker"]) for row in rows if row["primary_blocker"])),
        "total_errors": sum(int_value(row.get("error_count")) for row in rows),
        "total_warnings": sum(int_value(row.get("warning_count")) for row in rows),
    }


def write_reports(args: argparse.Namespace, rows: list[dict[str, Any]], summary: dict[str, Any], gcc: str, objdump: str) -> None:
    data = {
        "summary": summary,
        "gcc": gcc,
        "objdump": objdump,
        "optimization": args.optimization,
        "build_dir": rel(args.build_dir),
        "rows": rows,
    }
    write_json(args.out_json, data)
    write_csv(
        args.out_csv,
        rows,
        [
            "compiled",
            "primary_blocker",
            "error_count",
            "warning_count",
            "entry",
            "name",
            "domain",
            "rewrite_count",
            "adapter_count",
            "shape_anchors",
            "packet",
            "probe_source",
            "object",
            "dump",
            "stderr_log",
            "objdump_stderr_log",
        ],
    )
    write_markdown(args.out_md, rows, summary, gcc, args.optimization)


def write_markdown(path: Path, rows: list[dict[str, Any]], summary: dict[str, Any], gcc: str, optimization: str) -> None:
    blocker_text = ", ".join(f"{name}: {count}" for name, count in summary["primary_blockers"].items()) or "none"
    lines = [
        "# Direct Packet Compile Probe",
        "",
        "This report compiles throwaway probes for materialized N64->OOT3D direct packets. A passing probe is not a match; it only means the packet has crossed the C compilability barrier.",
        "",
        f"- GCC: `{gcc}`",
        f"- Optimization: `{optimization}`",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Packets probed | {summary['packets']} |",
        f"| Compiled probes | {summary['compiled']} |",
        f"| Dumped probes | {summary['dumped']} |",
        f"| Failed probes | {summary['failed']} |",
        f"| Total compile errors | {summary['total_errors']} |",
        f"| Total warnings | {summary['total_warnings']} |",
        "",
        f"- Primary blockers: {blocker_text}",
        "",
        "## Compiled Candidate Queue",
        "",
        "These packets now compile as throwaway C probes. They are ready for maintained-source promotion attempts and structured match comparison; they are not counted as matched by this probe.",
        "",
        "| OOT3D | Domain | Rewrites | Anchors | Packet | Probe object | Probe dump |",
        "| --- | --- | ---: | ---: | --- | --- | --- |",
    ]
    compiled_rows = [row for row in rows if row["compiled"]]
    if compiled_rows:
        for row in sorted(compiled_rows, key=lambda item: (str(item["domain"]), str(item["entry"]))):
            lines.append(
                f"| `{row['entry']}` `{row['name']}` | `{row['domain']}` | {row['rewrite_count']} | "
                f"{row['shape_anchors']} | `{row['packet']}` | `{row['object']}` | `{row['dump']}` |"
            )
    else:
        lines.append("| _none_ |  |  |  |  |  |  |")

    lines.extend(
        [
            "",
            "## Remaining Batch Blockers",
            "",
            "| Domain | Packets | Primary blockers |",
            "| --- | ---: | --- |",
        ]
    )
    remaining_by_domain: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        if not row["compiled"]:
            remaining_by_domain[str(row["domain"])].append(row)
    for domain, domain_rows in sorted(remaining_by_domain.items()):
        blockers = Counter(str(row["primary_blocker"]) for row in domain_rows if row["primary_blocker"])
        blockers_text = ", ".join(f"{name}: {count}" for name, count in blockers.most_common()) or "none"
        lines.append(f"| `{domain}` | {len(domain_rows)} | {blockers_text} |")

    lines.extend(
        [
            "",
        "## Packet Queue",
        "",
        "| Status | Blocker | Errors | Warnings | OOT3D | Domain | Rewrites | Anchors | Top examples | Packet |",
        "| --- | --- | ---: | ---: | --- | --- | ---: | ---: | --- | --- |",
        ]
    )
    for row in rows:
        examples = []
        for label, items in (row.get("blocker_examples") or {}).items():
            if label != row.get("primary_blocker"):
                continue
            examples = [f"{item['token']}:{item['count']}" for item in items[:5]]
            break
        status = "compiled" if row["compiled"] else "failed"
        lines.append(
            f"| `{status}` | `{row['primary_blocker']}` | {row['error_count']} | {row['warning_count']} | "
            f"`{row['entry']}` `{row['name']}` | `{row['domain']}` | {row['rewrite_count']} | "
            f"{row['shape_anchors']} | `{' '.join(examples)}` | `{row['packet']}` |"
        )

    lines.extend(["", "## First Errors"])
    for row in rows:
        if row["compiled"] or not row.get("first_errors"):
            continue
        lines.extend(["", f"### `{row['entry']}` `{row['name']}`", ""])
        for error in row["first_errors"][:5]:
            lines.append(f"- `{error}`")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--materialized-dir", type=Path, default=DEFAULT_MATERIALIZED)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--optimization", default="-O2")
    parser.add_argument("--tool-prefix", default="arm-none-eabi")
    parser.add_argument("--entry", action="append", default=[], help="Limit to one OOT3D entry; repeatable.")
    args = parser.parse_args()

    packets = discover_packets(args.materialized_dir)
    if args.entry:
        wanted = {entry.lower().removeprefix("0x").zfill(8) for entry in args.entry}
        packets = [packet for packet in packets if packet.entry in wanted]
    if not packets:
        raise SystemExit("no direct packets selected")

    args.build_dir.mkdir(parents=True, exist_ok=True)
    gcc = find_tool("gcc", args.tool_prefix)
    objdump = find_tool("objdump", args.tool_prefix)
    prelude = probe_prelude()
    rows = [run_probe(packet, gcc, objdump, args, prelude) for packet in packets]
    rows.sort(key=lambda row: (row["compiled"], row["primary_blocker"], -int_value(row["error_count"]), row["entry"]))
    summary = summarize(rows)
    write_reports(args, rows, summary, gcc, objdump)

    print(
        "direct packet compile probe: "
        f"{summary['compiled']}/{summary['packets']} compiled, "
        f"{summary['dumped']} dumped, {summary['failed']} failed, {summary['total_errors']} errors"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
