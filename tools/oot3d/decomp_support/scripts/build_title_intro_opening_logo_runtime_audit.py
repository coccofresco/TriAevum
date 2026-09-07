#!/usr/bin/env python3
"""Build the OOT3D open-title EnMag/logo runtime bridge audit.

The generated runtime keeps EnMag constrained to what is currently proven from
OOT3D code.bin/Ghidra and native asset tables: component lookup, draw-state
projection, and the compact alpha/timer state machine decoded from
EnMag_Init/EnMag_Update. The audit is non-destructive: maintained C sources are
no longer rewritten by this helper.
"""

from __future__ import annotations

import csv
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"

ORCHESTRATION = ANALYSIS / "title_intro_opening_orchestration.json"
SOURCE_TABLE = ANALYSIS / "title_intro_source_table.json"

OUT_JSON = ANALYSIS / "title_intro_opening_logo_runtime_audit.json"
OUT_MD = ANALYSIS / "title_intro_opening_logo_runtime_audit.md"
OUT_UPDATE_CSV = ANALYSIS / "title_intro_opening_logo_runtime_updates.csv"
OUT_DRAW_CSV = ANALYSIS / "title_intro_opening_logo_runtime_draw_snapshot.csv"
OUT_HEADER = ROOT / "include" / "oot3d" / "title_intro_opening_logo_runtime.h"
OUT_SOURCE = ROOT / "src" / "code" / "z_title_intro_opening_logo_runtime.c"
REPO_ROOT = ROOT.parents[2]
DEMO_HOST = REPO_ROOT / "tools" / "oot3d" / "native_demo_host" / "oot3d_native_fast3d_demo.cpp"

NO_INDEX = 0xFFFF

OP_BY_PHASE = {
    "init_defaults": "OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_INIT_DEFAULTS",
    "init_force_rising_button_alphas": "OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_INIT_FORCE_RISING_BUTTON",
    "initial_to_fade_in_on_env3": "OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_ENV3_TO_FADE_IN",
    "fade_in_substate_0_bind_csab": "OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_BIND_MAIN_CSAB",
    "fade_in_substate_1_wait": "OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_COUNTDOWN_TO_NEXT",
    "fade_in_substate_2_main_logo_alpha": "OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_ADD_MAIN_ALPHA_STEP",
    "fade_in_substate_3_title_text_effect_alpha": "OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_ADD_TITLE_TEXT_EFFECT_STEP",
    "fade_in_substate_4_copyright_alpha": "OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_ADD_COPYRIGHT_ALPHA_STEP",
    "display_to_fade_out_on_env4": "OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_ENV4_TO_FADE_OUT",
    "button_to_transition_delay_state4": "OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_BUTTON_TO_TRANSITION_DELAY",
    "transition_delay_state4_to_fade_out": "OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_TRANSITION_DELAY_TO_FADE_OUT",
    "fade_out_state3": "OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_SUBTRACT_FADEOUT",
    "fade_out_state6_with_transition_guard": "OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_SUBTRACT_FADEOUT_TRANSITION_GUARD",
}


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def write_json(path: Path, data: Any) -> None:
    write_text(path, json.dumps(data, indent=2) + "\n")


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        if not rows:
            return
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def c_u16(value: Any) -> str:
    return f"{int(value) & 0xFFFF}u"


def c_s16(value: Any) -> str:
    return str(int(value))


def c_float(value: Any) -> str:
    text = f"{float(value):.9g}"
    if "e" not in text.lower() and "." not in text:
        text += ".0"
    return text + "f"


def c_string(value: Any) -> str:
    text = "" if value is None else str(value)
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def clamp(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))


def alpha_for_offset(state: dict[str, Any], offset: int) -> float:
    if offset == 0x1D0:
        return float(state["title_text_alpha"])
    if offset == 0x1D4:
        return float(state["main_logo_alpha"])
    if offset == 0x1D8:
        return float(state["copyright_alpha"])
    if offset == 0x1DC:
        return float(state["effect_alpha"])
    return 0.0


def apply_row_model(state: dict[str, Any], row: dict[str, Any]) -> None:
    phase = row["phase_role"]
    max_alpha = float(row["max_alpha"])
    min_alpha = float(row["min_alpha"])
    main_step = float(row["main_alpha_step"])
    title_step = float(row["title_text_effect_alpha_step"])
    copyright_clamp = float(row["copyright_alpha_clamp"])

    if phase == "init_defaults":
        state.update(
            {
                "state": 0,
                "substate": 0,
                "delay_timer": 0,
                "timer": int(row["timer_initial_value"]),
                "pending_transition": 0,
                "copyright_alpha_step": int(row["copyright_alpha_step_default"]),
                "fade_out_alpha_step": int(row["fade_out_alpha_step_default"]),
                "main_csab_bound": 0,
                "title_anim_state_set": 0,
                "transition_requested": 0,
                "title_text_alpha": min_alpha,
                "main_logo_alpha": min_alpha,
                "copyright_alpha": min_alpha,
                "effect_alpha": min_alpha,
            }
        )
        return
    if phase == "init_force_rising_button_alphas":
        state.update(
            {
                "state": int(row["next_state"]),
                "substate": int(row["next_substate"]),
                "delay_timer": int(row["timer_initial_value"]),
                "timer": 0,
                "pending_transition": 0,
                "copyright_alpha_step": int(row["copyright_alpha_step_default"]),
                "fade_out_alpha_step": int(row["fade_out_alpha_step_default"]),
                "main_csab_bound": 1,
                "title_anim_state_set": 0,
                "transition_requested": 0,
                "title_text_alpha": max_alpha,
                "main_logo_alpha": max_alpha,
                "copyright_alpha": max_alpha,
                "effect_alpha": max_alpha,
            }
        )
        return
    if phase == "initial_to_fade_in_on_env3":
        state["state"] = int(row["next_state"])
        state["substate"] = int(row["next_substate"])
        state["timer"] = int(row["timer_initial_value"])
        return
    if phase == "fade_in_substate_0_bind_csab":
        state["main_csab_bound"] = 1
        state["timer"] = int(row["timer_initial_value"])
        state["substate"] = int(row["next_substate"])
        return
    if phase == "fade_in_substate_1_wait":
        state["timer"] -= 1
        if state["timer"] <= 0:
            state["timer"] = int(row["timer_initial_value"])
            state["substate"] = int(row["next_substate"])
        return
    if phase == "fade_in_substate_2_main_logo_alpha":
        state["main_logo_alpha"] = clamp(float(state["main_logo_alpha"]) + main_step, min_alpha, max_alpha)
        state["timer"] -= 1
        if state["timer"] <= 0:
            state["main_logo_alpha"] = max_alpha
            state["timer"] = int(row["timer_initial_value"])
            state["substate"] = int(row["next_substate"])
        return
    if phase == "fade_in_substate_3_title_text_effect_alpha":
        state["title_text_alpha"] = clamp(float(state["title_text_alpha"]) + title_step, min_alpha, max_alpha)
        state["effect_alpha"] = clamp(float(state["effect_alpha"]) + title_step, min_alpha, max_alpha)
        state["timer"] -= 1
        if state["timer"] <= 0:
            state["title_text_alpha"] = max_alpha
            state["effect_alpha"] = max_alpha
            state["substate"] = int(row["next_substate"])
            state["title_anim_state_set"] = 1
        return
    if phase == "fade_in_substate_4_copyright_alpha":
        state["copyright_alpha"] = clamp(
            float(state["copyright_alpha"]) + float(state["copyright_alpha_step"]), min_alpha, copyright_clamp
        )
        if float(state["copyright_alpha"]) >= copyright_clamp:
            state["copyright_alpha"] = max_alpha
            state["state"] = int(row["next_state"])
            state["substate"] = int(row["next_substate"])
            state["delay_timer"] = int(row["timer_initial_value"])
        return
    if phase == "display_to_fade_out_on_env4":
        state["state"] = int(row["next_state"])
        state["substate"] = int(row["next_substate"])
        return
    if phase == "button_to_transition_delay_state4":
        state["state"] = int(row["next_state"])
        state["substate"] = int(row["next_substate"])
        state["delay_timer"] = int(row["timer_initial_value"])
        state["pending_transition"] = 1
        return
    if phase == "transition_delay_state4_to_fade_out":
        state["copyright_alpha_step"] = int(row["transition_copyright_alpha_step"])
        state["fade_out_alpha_step"] = int(row["transition_fade_out_alpha_step"])
        state["state"] = int(row["next_state"])
        state["substate"] = int(row["next_substate"])
        state["transition_requested"] = 1
        return
    if phase in {"fade_out_state3", "fade_out_state6_with_transition_guard"}:
        fade = float(state["fade_out_alpha_step"])
        state["title_text_alpha"] = clamp(float(state["title_text_alpha"]) - fade, min_alpha, max_alpha)
        state["main_logo_alpha"] = clamp(float(state["main_logo_alpha"]) - fade, min_alpha, max_alpha)
        state["copyright_alpha"] = clamp(float(state["copyright_alpha"]) - fade, min_alpha, max_alpha)
        if (
            float(state["title_text_alpha"]) == min_alpha
            and float(state["main_logo_alpha"]) == min_alpha
            and float(state["copyright_alpha"]) == min_alpha
        ):
            state["state"] = int(row["next_state"])
            state["substate"] = int(row["next_substate"])
            if phase == "fade_out_state6_with_transition_guard" and int(state["delay_timer"]) == 0:
                state["transition_requested"] = 1
        return
    raise SystemExit(f"unhandled logo update phase {phase}")


def row_for_phase(rows: list[dict[str, Any]], phase: str) -> dict[str, Any]:
    matches = [row for row in rows if row["phase_role"] == phase]
    if len(matches) != 1:
        raise SystemExit(f"expected one update row for {phase}, found {len(matches)}")
    return matches[0]


def build_header() -> str:
    return """#ifndef OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_H
#define OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_H

#include "oot3d/title_intro_opening_orchestration.h"
#include "oot3d/title_intro_source_table.h"
#include "oot3d/types.h"

#define OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_NO_INDEX 0xFFFFu
#define OOT3D_TITLE_INTRO_OPENING_LOGO_COMPONENT_CAPACITY 3u
#define OOT3D_TITLE_INTRO_OPENING_LOGO_DRAW_CAPACITY 3u

typedef enum {
    OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_OK = 0,
    OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_NULL_OUTPUT,
    OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_NULL_STATE,
    OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_MISSING_ORCHESTRATION,
    OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_INDEX_OUT_OF_RANGE,
    OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_NO_MATCHING_STEP,
} Oot3dTitleIntroOpeningLogoRuntimeStatus;

typedef enum {
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_INIT_DEFAULTS = 0,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_INIT_FORCE_RISING_BUTTON,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_ENV3_TO_FADE_IN,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_BIND_MAIN_CSAB,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_COUNTDOWN_TO_NEXT,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_ADD_MAIN_ALPHA_STEP,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_ADD_TITLE_TEXT_EFFECT_STEP,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_ADD_COPYRIGHT_ALPHA_STEP,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_ENV4_TO_FADE_OUT,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_BUTTON_TO_TRANSITION_DELAY,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_TRANSITION_DELAY_TO_FADE_OUT,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_SUBTRACT_FADEOUT,
    OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_SUBTRACT_FADEOUT_TRANSITION_GUARD,
} Oot3dTitleIntroOpeningLogoUpdateOp;

typedef struct Oot3dTitleIntroOpeningLogoUpdateRuntimeRow {
    u16 updateRuntimeIndex;
    u16 orchestrationIndex;
    u16 sourceUpdateIndex;
    u16 op;
    u16 state;
    u16 substate;
    u16 nextState;
    u16 nextSubstate;
    u16 timerFieldOffset;
    s16 timerInitialValue;
    u16 flagId;
    s16 copyrightAlphaStepDefault;
    s16 fadeOutAlphaStepDefault;
    s16 transitionCopyrightAlphaStep;
    s16 transitionFadeOutAlphaStep;
    float literalValue;
    float minAlpha;
    float maxAlpha;
    float mainAlphaStep;
    float titleTextEffectAlphaStep;
    float copyrightAlphaClamp;
    const char* phaseRole;
    const char* alphaFieldOffsets;
    const char* alphaOperation;
    const char* nativeBasis;
} Oot3dTitleIntroOpeningLogoUpdateRuntimeRow;

typedef struct Oot3dTitleIntroOpeningLogoState {
    u16 state;
    u16 substate;
    s16 delayTimer;
    s16 timer;
    u16 pendingTransition;
    s16 copyrightAlphaStep;
    s16 fadeOutAlphaStep;
    u16 selectedMainCsabIndex;
    u8 mainCsabBound;
    u8 titleAnimStateSet;
    u8 transitionRequested;
    float titleTextAlpha;
    float mainLogoAlpha;
    float copyrightAlpha;
    float effectAlpha;
} Oot3dTitleIntroOpeningLogoState;

typedef struct Oot3dTitleIntroOpeningLogoStepInput {
    u8 envFlag3;
    u8 envFlag4;
    u8 buttonPressed;
} Oot3dTitleIntroOpeningLogoStepInput;

typedef struct Oot3dTitleIntroOpeningLogoStepResult {
    u8 applied;
    u8 delayTimerDecremented;
    u8 stateChanged;
    u8 boundMainCsab;
    u8 titleAnimStateSet;
    u8 transitionRequested;
    u16 updateRuntimeIndex;
    u16 sourceUpdateIndex;
    const Oot3dTitleIntroOpeningLogoUpdateRuntimeRow* runtimeRow;
    const Oot3dTitleIntroLogoUpdateRow* sourceUpdateRow;
} Oot3dTitleIntroOpeningLogoStepResult;

typedef struct Oot3dTitleIntroOpeningLogoDrawComponentState {
    u16 drawIndex;
    u16 componentIndex;
    u16 handleFieldOffset;
    u16 alphaFieldOffset;
    u16 effectAlphaFieldOffset;
    u8 visible;
    float alpha;
    float alphaNormalized;
    float effectAlpha;
    float effectAlphaNormalized;
    float colorR;
    float colorG;
    float colorB;
    float colorA;
    float matrix[16];
    const Oot3dTitleIntroLogoDrawRow* sourceDrawRow;
    const Oot3dTitleIntroLogoComponentRow* sourceComponentRow;
} Oot3dTitleIntroOpeningLogoDrawComponentState;

typedef struct Oot3dTitleIntroOpeningLogoDrawSnapshot {
    u16 orchestrationIndex;
    u16 drawCount;
    Oot3dTitleIntroOpeningLogoDrawComponentState draws[OOT3D_TITLE_INTRO_OPENING_LOGO_DRAW_CAPACITY];
} Oot3dTitleIntroOpeningLogoDrawSnapshot;

extern const Oot3dTitleIntroOpeningLogoUpdateRuntimeRow gOot3dTitleIntroOpeningLogoUpdateRuntimeRows[];
extern const u32 gOot3dTitleIntroOpeningLogoUpdateRuntimeRowCount;

const Oot3dTitleIntroActorInitSourceRow* Oot3d_TitleIntroOpeningLogoRuntimeGetActorInitSourceRow(u16 orchestrationIndex);
const Oot3dTitleIntroLogoComponentRow* Oot3d_TitleIntroOpeningLogoRuntimeGetComponentSourceRow(u16 orchestrationIndex, u16 localComponentIndex);
const Oot3dTitleIntroLogoDrawRow* Oot3d_TitleIntroOpeningLogoRuntimeGetDrawSourceRow(u16 orchestrationIndex, u16 localDrawIndex);
const Oot3dTitleIntroLogoUpdateRow* Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateSourceRow(u16 orchestrationIndex, u16 localUpdateIndex);
const Oot3dTitleIntroLogoDrawContextRow* Oot3d_TitleIntroOpeningLogoRuntimeGetDrawContextSourceRow(u16 contextIndex);
const Oot3dTitleIntroOpeningLogoUpdateRuntimeRow* Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateRuntimeRow(u16 updateRuntimeIndex);
Oot3dTitleIntroOpeningLogoRuntimeStatus Oot3d_TitleIntroOpeningLogoRuntimeInitDefault(Oot3dTitleIntroOpeningLogoState* state);
Oot3dTitleIntroOpeningLogoRuntimeStatus Oot3d_TitleIntroOpeningLogoRuntimeInitForceRisingButton(Oot3dTitleIntroOpeningLogoState* state);
Oot3dTitleIntroOpeningLogoRuntimeStatus Oot3d_TitleIntroOpeningLogoRuntimeApplyUpdateRow(
    Oot3dTitleIntroOpeningLogoState* state,
    const Oot3dTitleIntroOpeningLogoUpdateRuntimeRow* row,
    Oot3dTitleIntroOpeningLogoStepResult* outResult
);
Oot3dTitleIntroOpeningLogoRuntimeStatus Oot3d_TitleIntroOpeningLogoRuntimeStep(
    Oot3dTitleIntroOpeningLogoState* state,
    const Oot3dTitleIntroOpeningLogoStepInput* input,
    Oot3dTitleIntroOpeningLogoStepResult* outResult
);
Oot3dTitleIntroOpeningLogoRuntimeStatus Oot3d_TitleIntroOpeningLogoRuntimeBuildDrawSnapshot(
    u16 orchestrationIndex,
    const Oot3dTitleIntroOpeningLogoState* state,
    Oot3dTitleIntroOpeningLogoDrawSnapshot* outSnapshot
);

#endif
"""


def build_source(runtime_rows: list[dict[str, Any]]) -> str:
    initializers = []
    for row in runtime_rows:
        initializers.append(
            "    { "
            + ", ".join(
                [
                    c_u16(row["update_runtime_index"]),
                    c_u16(row["orchestration_index"]),
                    c_u16(row["source_update_index"]),
                    row["op"],
                    c_u16(row["state"]),
                    c_u16(row["substate"]),
                    c_u16(row["next_state"]),
                    c_u16(row["next_substate"]),
                    c_u16(row["timer_field_offset"]),
                    c_s16(row["timer_initial_value"]),
                    c_u16(row["flag_id"]),
                    c_s16(row["copyright_alpha_step_default"]),
                    c_s16(row["fade_out_alpha_step_default"]),
                    c_s16(row["transition_copyright_alpha_step"]),
                    c_s16(row["transition_fade_out_alpha_step"]),
                    c_float(row["literal_value"]),
                    c_float(row["min_alpha"]),
                    c_float(row["max_alpha"]),
                    c_float(row["main_alpha_step"]),
                    c_float(row["title_text_effect_alpha_step"]),
                    c_float(row["copyright_alpha_clamp"]),
                    c_string(row["phase_role"]),
                    c_string(row["alpha_field_offsets"]),
                    c_string(row["alpha_operation"]),
                    c_string(row["native_basis"]),
                ]
            )
            + " },"
        )
    runtime_table = "\n".join(initializers)

    return f"""#include "oot3d/title_intro_opening_logo_runtime.h"

#include <string.h>

const Oot3dTitleIntroOpeningLogoUpdateRuntimeRow gOot3dTitleIntroOpeningLogoUpdateRuntimeRows[] = {{
{runtime_table}
}};
const u32 gOot3dTitleIntroOpeningLogoUpdateRuntimeRowCount =
    sizeof(gOot3dTitleIntroOpeningLogoUpdateRuntimeRows) / sizeof(gOot3dTitleIntroOpeningLogoUpdateRuntimeRows[0]);

static float Oot3d_TitleIntroOpeningLogoRuntimeClamp(float value, float minimum, float maximum) {{
    if (value < minimum) {{
        return minimum;
    }}
    if (value > maximum) {{
        return maximum;
    }}
    return value;
}}

static float Oot3d_TitleIntroOpeningLogoRuntimeAlphaForOffset(
    const Oot3dTitleIntroOpeningLogoState* state,
    u16 offset
) {{
    if (offset == 0x01D0u) {{
        return state->titleTextAlpha;
    }}
    if (offset == 0x01D4u) {{
        return state->mainLogoAlpha;
    }}
    if (offset == 0x01D8u) {{
        return state->copyrightAlpha;
    }}
    if (offset == 0x01DCu) {{
        return state->effectAlpha;
    }}
    return 0.0f;
}}

static void Oot3d_TitleIntroOpeningLogoRuntimeCopyDrawMatrix(
    const Oot3dTitleIntroLogoDrawRow* row,
    float* matrix
) {{
    matrix[0] = row->matrix00;
    matrix[1] = row->matrix01;
    matrix[2] = row->matrix02;
    matrix[3] = row->matrix03;
    matrix[4] = row->matrix10;
    matrix[5] = row->matrix11;
    matrix[6] = row->matrix12;
    matrix[7] = row->matrix13;
    matrix[8] = row->matrix20;
    matrix[9] = row->matrix21;
    matrix[10] = row->matrix22;
    matrix[11] = row->matrix23;
    matrix[12] = row->matrix30;
    matrix[13] = row->matrix31;
    matrix[14] = row->matrix32;
    matrix[15] = row->matrix33;
}}

const Oot3dTitleIntroActorInitSourceRow* Oot3d_TitleIntroOpeningLogoRuntimeGetActorInitSourceRow(
    u16 orchestrationIndex
) {{
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration =
        Oot3d_TitleIntroOpeningGetOrchestrationRow(orchestrationIndex);
    if (orchestration == 0 || orchestration->titleLogoActorInitIndex >= gOot3dTitleIntroActorInitSourceRowCount) {{
        return 0;
    }}
    return &gOot3dTitleIntroActorInitSourceRows[orchestration->titleLogoActorInitIndex];
}}

const Oot3dTitleIntroLogoComponentRow* Oot3d_TitleIntroOpeningLogoRuntimeGetComponentSourceRow(
    u16 orchestrationIndex,
    u16 localComponentIndex
) {{
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration =
        Oot3d_TitleIntroOpeningGetOrchestrationRow(orchestrationIndex);
    u32 sourceIndex;
    if (orchestration == 0 || localComponentIndex >= orchestration->titleLogoComponentRefCount) {{
        return 0;
    }}
    sourceIndex = (u32)orchestration->titleLogoComponentRefStart + localComponentIndex;
    if (sourceIndex >= gOot3dTitleIntroLogoComponentRowCount) {{
        return 0;
    }}
    return &gOot3dTitleIntroLogoComponentRows[sourceIndex];
}}

const Oot3dTitleIntroLogoDrawRow* Oot3d_TitleIntroOpeningLogoRuntimeGetDrawSourceRow(
    u16 orchestrationIndex,
    u16 localDrawIndex
) {{
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration =
        Oot3d_TitleIntroOpeningGetOrchestrationRow(orchestrationIndex);
    u32 sourceIndex;
    if (orchestration == 0 || localDrawIndex >= orchestration->titleLogoDrawRefCount) {{
        return 0;
    }}
    sourceIndex = (u32)orchestration->titleLogoDrawRefStart + localDrawIndex;
    if (sourceIndex >= gOot3dTitleIntroLogoDrawRowCount) {{
        return 0;
    }}
    return &gOot3dTitleIntroLogoDrawRows[sourceIndex];
}}

const Oot3dTitleIntroLogoUpdateRow* Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateSourceRow(
    u16 orchestrationIndex,
    u16 localUpdateIndex
) {{
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration =
        Oot3d_TitleIntroOpeningGetOrchestrationRow(orchestrationIndex);
    u32 sourceIndex;
    if (orchestration == 0 || localUpdateIndex >= orchestration->titleLogoUpdateRefCount) {{
        return 0;
    }}
    sourceIndex = (u32)orchestration->titleLogoUpdateRefStart + localUpdateIndex;
    if (sourceIndex >= gOot3dTitleIntroLogoUpdateRowCount) {{
        return 0;
    }}
    return &gOot3dTitleIntroLogoUpdateRows[sourceIndex];
}}

const Oot3dTitleIntroLogoDrawContextRow* Oot3d_TitleIntroOpeningLogoRuntimeGetDrawContextSourceRow(
    u16 contextIndex
) {{
    if (contextIndex >= gOot3dTitleIntroLogoDrawContextRowCount) {{
        return 0;
    }}
    return &gOot3dTitleIntroLogoDrawContextRows[contextIndex];
}}

const Oot3dTitleIntroOpeningLogoUpdateRuntimeRow* Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateRuntimeRow(
    u16 updateRuntimeIndex
) {{
    if (updateRuntimeIndex >= gOot3dTitleIntroOpeningLogoUpdateRuntimeRowCount) {{
        return 0;
    }}
    return &gOot3dTitleIntroOpeningLogoUpdateRuntimeRows[updateRuntimeIndex];
}}

static void Oot3d_TitleIntroOpeningLogoRuntimeFillResult(
    Oot3dTitleIntroOpeningLogoStepResult* outResult,
    const Oot3dTitleIntroOpeningLogoUpdateRuntimeRow* row,
    u16 oldState,
    u16 oldSubstate,
    u8 oldBoundMainCsab,
    u8 oldTitleAnimStateSet,
    u8 oldTransitionRequested,
    const Oot3dTitleIntroOpeningLogoState* state
) {{
    if (outResult == 0) {{
        return;
    }}
    memset(outResult, 0, sizeof(*outResult));
    outResult->applied = 1u;
    outResult->stateChanged = (oldState != state->state || oldSubstate != state->substate) ? 1u : 0u;
    outResult->boundMainCsab = (oldBoundMainCsab == 0u && state->mainCsabBound != 0u) ? 1u : 0u;
    outResult->titleAnimStateSet = (oldTitleAnimStateSet == 0u && state->titleAnimStateSet != 0u) ? 1u : 0u;
    outResult->transitionRequested =
        (oldTransitionRequested == 0u && state->transitionRequested != 0u) ? 1u : 0u;
    outResult->updateRuntimeIndex = row->updateRuntimeIndex;
    outResult->sourceUpdateIndex = row->sourceUpdateIndex;
    outResult->runtimeRow = row;
    if (row->sourceUpdateIndex < gOot3dTitleIntroLogoUpdateRowCount) {{
        outResult->sourceUpdateRow = &gOot3dTitleIntroLogoUpdateRows[row->sourceUpdateIndex];
    }}
}}

Oot3dTitleIntroOpeningLogoRuntimeStatus Oot3d_TitleIntroOpeningLogoRuntimeApplyUpdateRow(
    Oot3dTitleIntroOpeningLogoState* state,
    const Oot3dTitleIntroOpeningLogoUpdateRuntimeRow* row,
    Oot3dTitleIntroOpeningLogoStepResult* outResult
) {{
    u16 oldState;
    u16 oldSubstate;
    u8 oldBoundMainCsab;
    u8 oldTitleAnimStateSet;
    u8 oldTransitionRequested;
    float fadeOutStep;

    if (state == 0) {{
        return OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_NULL_STATE;
    }}
    if (row == 0) {{
        return OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_INDEX_OUT_OF_RANGE;
    }}
    if (row->op == OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_INIT_DEFAULTS ||
        row->op == OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_INIT_FORCE_RISING_BUTTON) {{
        oldState = 0u;
        oldSubstate = 0u;
        oldBoundMainCsab = 0u;
        oldTitleAnimStateSet = 0u;
        oldTransitionRequested = 0u;
    }} else {{
        oldState = state->state;
        oldSubstate = state->substate;
        oldBoundMainCsab = state->mainCsabBound;
        oldTitleAnimStateSet = state->titleAnimStateSet;
        oldTransitionRequested = state->transitionRequested;
    }}

    switch ((Oot3dTitleIntroOpeningLogoUpdateOp)row->op) {{
        case OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_INIT_DEFAULTS:
            memset(state, 0, sizeof(*state));
            state->state = row->nextState;
            state->substate = row->nextSubstate;
            state->timer = row->timerInitialValue;
            state->copyrightAlphaStep = row->copyrightAlphaStepDefault;
            state->fadeOutAlphaStep = row->fadeOutAlphaStepDefault;
            state->selectedMainCsabIndex = OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_NO_INDEX;
            state->titleTextAlpha = row->minAlpha;
            state->mainLogoAlpha = row->minAlpha;
            state->copyrightAlpha = row->minAlpha;
            state->effectAlpha = row->minAlpha;
            break;
        case OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_INIT_FORCE_RISING_BUTTON:
            memset(state, 0, sizeof(*state));
            state->state = row->nextState;
            state->substate = row->nextSubstate;
            state->delayTimer = row->timerInitialValue;
            state->copyrightAlphaStep = row->copyrightAlphaStepDefault;
            state->fadeOutAlphaStep = row->fadeOutAlphaStepDefault;
            state->selectedMainCsabIndex = OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_NO_INDEX;
            state->mainCsabBound = 1u;
            state->titleTextAlpha = row->maxAlpha;
            state->mainLogoAlpha = row->maxAlpha;
            state->copyrightAlpha = row->maxAlpha;
            state->effectAlpha = row->maxAlpha;
            break;
        case OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_ENV3_TO_FADE_IN:
            state->state = row->nextState;
            state->substate = row->nextSubstate;
            state->timer = row->timerInitialValue;
            break;
        case OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_BIND_MAIN_CSAB:
            state->mainCsabBound = 1u;
            state->timer = row->timerInitialValue;
            state->substate = row->nextSubstate;
            break;
        case OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_COUNTDOWN_TO_NEXT:
            state->timer--;
            if (state->timer <= 0) {{
                state->timer = row->timerInitialValue;
                state->substate = row->nextSubstate;
            }}
            break;
        case OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_ADD_MAIN_ALPHA_STEP:
            state->mainLogoAlpha =
                Oot3d_TitleIntroOpeningLogoRuntimeClamp(state->mainLogoAlpha + row->mainAlphaStep, row->minAlpha, row->maxAlpha);
            state->timer--;
            if (state->timer <= 0) {{
                state->mainLogoAlpha = row->maxAlpha;
                state->timer = row->timerInitialValue;
                state->substate = row->nextSubstate;
            }}
            break;
        case OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_ADD_TITLE_TEXT_EFFECT_STEP:
            state->titleTextAlpha = Oot3d_TitleIntroOpeningLogoRuntimeClamp(
                state->titleTextAlpha + row->titleTextEffectAlphaStep,
                row->minAlpha,
                row->maxAlpha
            );
            state->effectAlpha = Oot3d_TitleIntroOpeningLogoRuntimeClamp(
                state->effectAlpha + row->titleTextEffectAlphaStep,
                row->minAlpha,
                row->maxAlpha
            );
            state->timer--;
            if (state->timer <= 0) {{
                state->titleTextAlpha = row->maxAlpha;
                state->effectAlpha = row->maxAlpha;
                state->substate = row->nextSubstate;
                state->titleAnimStateSet = 1u;
            }}
            break;
        case OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_ADD_COPYRIGHT_ALPHA_STEP:
            state->copyrightAlpha = Oot3d_TitleIntroOpeningLogoRuntimeClamp(
                state->copyrightAlpha + (float)state->copyrightAlphaStep,
                row->minAlpha,
                row->copyrightAlphaClamp
            );
            if (state->copyrightAlpha >= row->copyrightAlphaClamp) {{
                state->copyrightAlpha = row->maxAlpha;
                state->state = row->nextState;
                state->substate = row->nextSubstate;
                state->delayTimer = row->timerInitialValue;
            }}
            break;
        case OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_ENV4_TO_FADE_OUT:
            state->state = row->nextState;
            state->substate = row->nextSubstate;
            break;
        case OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_BUTTON_TO_TRANSITION_DELAY:
            state->state = row->nextState;
            state->substate = row->nextSubstate;
            state->delayTimer = row->timerInitialValue;
            state->pendingTransition = 1u;
            break;
        case OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_TRANSITION_DELAY_TO_FADE_OUT:
            state->copyrightAlphaStep = row->transitionCopyrightAlphaStep;
            state->fadeOutAlphaStep = row->transitionFadeOutAlphaStep;
            state->state = row->nextState;
            state->substate = row->nextSubstate;
            state->transitionRequested = 1u;
            break;
        case OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_SUBTRACT_FADEOUT:
        case OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_SUBTRACT_FADEOUT_TRANSITION_GUARD:
            fadeOutStep = (float)state->fadeOutAlphaStep;
            state->titleTextAlpha =
                Oot3d_TitleIntroOpeningLogoRuntimeClamp(state->titleTextAlpha - fadeOutStep, row->minAlpha, row->maxAlpha);
            state->mainLogoAlpha =
                Oot3d_TitleIntroOpeningLogoRuntimeClamp(state->mainLogoAlpha - fadeOutStep, row->minAlpha, row->maxAlpha);
            state->copyrightAlpha =
                Oot3d_TitleIntroOpeningLogoRuntimeClamp(state->copyrightAlpha - fadeOutStep, row->minAlpha, row->maxAlpha);
            if (state->titleTextAlpha == row->minAlpha && state->mainLogoAlpha == row->minAlpha &&
                state->copyrightAlpha == row->minAlpha) {{
                state->state = row->nextState;
                state->substate = row->nextSubstate;
                if (row->op == OOT3D_TITLE_INTRO_OPENING_LOGO_UPDATE_OP_SUBTRACT_FADEOUT_TRANSITION_GUARD &&
                    state->delayTimer == 0) {{
                    state->transitionRequested = 1u;
                }}
            }}
            break;
    }}
    Oot3d_TitleIntroOpeningLogoRuntimeFillResult(
        outResult,
        row,
        oldState,
        oldSubstate,
        oldBoundMainCsab,
        oldTitleAnimStateSet,
        oldTransitionRequested,
        state
    );
    return OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_OK;
}}

Oot3dTitleIntroOpeningLogoRuntimeStatus Oot3d_TitleIntroOpeningLogoRuntimeInitDefault(
    Oot3dTitleIntroOpeningLogoState* state
) {{
    return Oot3d_TitleIntroOpeningLogoRuntimeApplyUpdateRow(
        state,
        Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateRuntimeRow(0u),
        0
    );
}}

Oot3dTitleIntroOpeningLogoRuntimeStatus Oot3d_TitleIntroOpeningLogoRuntimeInitForceRisingButton(
    Oot3dTitleIntroOpeningLogoState* state
) {{
    return Oot3d_TitleIntroOpeningLogoRuntimeApplyUpdateRow(
        state,
        Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateRuntimeRow(1u),
        0
    );
}}

static const Oot3dTitleIntroOpeningLogoUpdateRuntimeRow*
Oot3d_TitleIntroOpeningLogoRuntimeSelectStepRow(
    const Oot3dTitleIntroOpeningLogoState* state,
    const Oot3dTitleIntroOpeningLogoStepInput* input
) {{
    if (state->state == 0u && input != 0 && input->envFlag3 != 0u) {{
        return Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateRuntimeRow(2u);
    }}
    if (state->state == 1u) {{
        if (state->substate == 0u) {{
            return Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateRuntimeRow(3u);
        }}
        if (state->substate == 1u) {{
            return Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateRuntimeRow(4u);
        }}
        if (state->substate == 2u) {{
            return Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateRuntimeRow(5u);
        }}
        if (state->substate == 3u) {{
            return Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateRuntimeRow(6u);
        }}
        if (state->substate == 4u) {{
            return Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateRuntimeRow(7u);
        }}
    }}
    if (state->state == 2u && state->substate == 5u) {{
        if (input != 0 && input->buttonPressed != 0u) {{
            return Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateRuntimeRow(9u);
        }}
        if (input != 0 && input->envFlag4 != 0u) {{
            return Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateRuntimeRow(8u);
        }}
    }}
    if (state->state == 3u && state->substate == 5u) {{
        return Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateRuntimeRow(11u);
    }}
    if (state->state == 4u && state->substate == 5u) {{
        return Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateRuntimeRow(10u);
    }}
    if (state->state == 6u && state->substate == 5u) {{
        return Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateRuntimeRow(12u);
    }}
    return 0;
}}

Oot3dTitleIntroOpeningLogoRuntimeStatus Oot3d_TitleIntroOpeningLogoRuntimeStep(
    Oot3dTitleIntroOpeningLogoState* state,
    const Oot3dTitleIntroOpeningLogoStepInput* input,
    Oot3dTitleIntroOpeningLogoStepResult* outResult
) {{
    const Oot3dTitleIntroOpeningLogoUpdateRuntimeRow* row;
    if (state == 0) {{
        return OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_NULL_STATE;
    }}
    if (outResult != 0) {{
        memset(outResult, 0, sizeof(*outResult));
    }}
    if (state->state > 1u && state->delayTimer > 0) {{
        state->delayTimer--;
        if (outResult != 0) {{
            outResult->delayTimerDecremented = 1u;
        }}
        return OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_OK;
    }}
    row = Oot3d_TitleIntroOpeningLogoRuntimeSelectStepRow(state, input);
    if (row == 0) {{
        return OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_NO_MATCHING_STEP;
    }}
    return Oot3d_TitleIntroOpeningLogoRuntimeApplyUpdateRow(state, row, outResult);
}}

Oot3dTitleIntroOpeningLogoRuntimeStatus Oot3d_TitleIntroOpeningLogoRuntimeBuildDrawSnapshot(
    u16 orchestrationIndex,
    const Oot3dTitleIntroOpeningLogoState* state,
    Oot3dTitleIntroOpeningLogoDrawSnapshot* outSnapshot
) {{
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration;
    u16 index;
    if (state == 0) {{
        return OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_NULL_STATE;
    }}
    if (outSnapshot == 0) {{
        return OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_NULL_OUTPUT;
    }}
    memset(outSnapshot, 0, sizeof(*outSnapshot));
    orchestration = Oot3d_TitleIntroOpeningGetOrchestrationRow(orchestrationIndex);
    if (orchestration == 0) {{
        return OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_MISSING_ORCHESTRATION;
    }}
    outSnapshot->orchestrationIndex = orchestrationIndex;
    outSnapshot->drawCount = orchestration->titleLogoDrawRefCount;
    if (outSnapshot->drawCount > OOT3D_TITLE_INTRO_OPENING_LOGO_DRAW_CAPACITY) {{
        outSnapshot->drawCount = OOT3D_TITLE_INTRO_OPENING_LOGO_DRAW_CAPACITY;
    }}
    for (index = 0; index < outSnapshot->drawCount; index++) {{
        const Oot3dTitleIntroLogoDrawRow* draw = Oot3d_TitleIntroOpeningLogoRuntimeGetDrawSourceRow(orchestrationIndex, index);
        Oot3dTitleIntroOpeningLogoDrawComponentState* drawState = &outSnapshot->draws[index];
        if (draw == 0) {{
            continue;
        }}
        drawState->drawIndex = draw->drawIndex;
        drawState->componentIndex = draw->componentIndex;
        drawState->handleFieldOffset = draw->handleFieldOffset;
        drawState->alphaFieldOffset = draw->alphaFieldOffset;
        drawState->effectAlphaFieldOffset = draw->effectAlphaFieldOffset;
        drawState->alpha = Oot3d_TitleIntroOpeningLogoRuntimeAlphaForOffset(state, draw->alphaFieldOffset);
        drawState->alphaNormalized = drawState->alpha * draw->alphaScale;
        drawState->effectAlpha =
            Oot3d_TitleIntroOpeningLogoRuntimeAlphaForOffset(state, draw->effectAlphaFieldOffset);
        drawState->effectAlphaNormalized = drawState->effectAlpha * draw->alphaScale;
        drawState->visible = drawState->alpha > 0.0f ? 1u : 0u;
        drawState->colorR = draw->baseColorR;
        drawState->colorG = draw->baseColorG;
        drawState->colorB = draw->baseColorB;
        drawState->colorA = drawState->alphaNormalized;
        drawState->sourceDrawRow = draw;
        drawState->sourceComponentRow =
            Oot3d_TitleIntroOpeningLogoRuntimeGetComponentSourceRow(orchestrationIndex, draw->componentIndex);
        Oot3d_TitleIntroOpeningLogoRuntimeCopyDrawMatrix(draw, drawState->matrix);
    }}
    return OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_OK;
}}
"""


def build_markdown(audit: dict[str, Any], runtime_rows: list[dict[str, Any]], draw_rows: list[dict[str, Any]]) -> str:
    lines = [
        "# OOT3D Open Title Logo Runtime Audit",
        "",
        "This audit promotes the native EnMag title-logo rows into a runtime-facing alpha/timer/draw-state bridge.",
        "",
        "## Summary",
        "",
        f"- OK: {audit['ok']}",
        f"- Components: {audit['counts']['components']}",
        f"- Draw rows: {audit['counts']['draw_rows']}",
        f"- Update runtime rows: {audit['counts']['update_runtime_rows']}",
        "- Next gate: keep EnMag draw state wired to the render path while closing remaining resource-version and material/effect backend fidelity gaps.",
        "",
        "## Checks",
        "",
        "| Check | Status |",
        "| --- | --- |",
    ]
    for key, value in audit["checks"].items():
        lines.append(f"| `{key}` | `{value}` |")
    lines.extend(
        [
            "",
            "## Runtime Update Rows",
            "",
            "| Row | Source | Op | State | Next | Timer | Alpha fields |",
            "| ---: | ---: | --- | --- | --- | --- | --- |",
        ]
    )
    for row in runtime_rows:
        timer = "none" if int(row["timer_field_offset"]) == NO_INDEX else f"0x{int(row['timer_field_offset']):04X}={row['timer_initial_value']}"
        lines.append(
            f"| `{row['update_runtime_index']}` | `{row['source_update_index']}` | `{row['phase_role']}` | "
            f"`{row['state']}/{row['substate']}` | `{row['next_state']}/{row['next_substate']}` | "
            f"`{timer}` | `{row['alpha_field_offsets']}` |"
        )
    lines.extend(
        [
            "",
            "## Draw Snapshot From Default State",
            "",
            "| Draw | Component | Visible | Alpha | Matrix role |",
            "| ---: | --- | ---: | ---: | --- |",
        ]
    )
    for row in draw_rows:
        lines.append(
            f"| `{row['draw_index']}` | `{row['component_role']}` | `{row['visible']}` | "
            f"`{row['alpha']}` | `{row['matrix_role']}` |"
        )
    lines.extend(
        [
            "",
            "## Unresolved",
            "",
            "- The demo host now uses the EnMag draw snapshot to submit visible native CMB components to the render scene, with material color slot 5 alpha/color overrides.",
            "- Audio/global transition side effects are represented as flags on the step result, not executed.",
            "- Resource version selection for JP/EU vs US CMB/CSAB remains sourced in the component rows and still needs engine resource wiring.",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> None:
    orchestration = read_json(ORCHESTRATION)
    source = read_json(SOURCE_TABLE)
    demo_text = read_text(DEMO_HOST)
    orchestration_row = orchestration["orchestration_rows"][0]
    update_rows = source["title_logo_update_rows"]
    component_rows = source["title_logo_component_rows"]
    draw_rows = source["title_logo_draw_rows"]
    actor_init_rows = source["title_logo_actor_init"]

    runtime_rows: list[dict[str, Any]] = []
    for row in update_rows:
        phase = row["phase_role"]
        if phase not in OP_BY_PHASE:
            raise SystemExit(f"unknown EnMag phase role {phase}")
        runtime_rows.append(
            {
                "update_runtime_index": len(runtime_rows),
                "orchestration_index": orchestration_row["orchestration_index"],
                "source_update_index": row["update_index"],
                "op": OP_BY_PHASE[phase],
                "state": row["state"],
                "substate": row["substate"],
                "next_state": row["next_state"],
                "next_substate": row["next_substate"],
                "timer_field_offset": row["timer_field_offset"],
                "timer_initial_value": row["timer_initial_value"],
                "flag_id": row["flag_id"],
                "copyright_alpha_step_default": row["copyright_alpha_step_default"],
                "fade_out_alpha_step_default": row["fade_out_alpha_step_default"],
                "transition_copyright_alpha_step": row["transition_copyright_alpha_step"],
                "transition_fade_out_alpha_step": row["transition_fade_out_alpha_step"],
                "literal_value": row["literal_value"],
                "min_alpha": row["min_alpha"],
                "max_alpha": row["max_alpha"],
                "main_alpha_step": row["main_alpha_step"],
                "title_text_effect_alpha_step": row["title_text_effect_alpha_step"],
                "copyright_alpha_clamp": row["copyright_alpha_clamp"],
                "phase_role": phase,
                "alpha_field_offsets": row["alpha_field_offsets"],
                "alpha_operation": row["alpha_operation"],
                "native_basis": row["oot3d_basis"],
            }
        )

    default_state = {}
    apply_row_model(default_state, row_for_phase(update_rows, "init_defaults"))
    force_state = {}
    apply_row_model(force_state, row_for_phase(update_rows, "init_force_rising_button_alphas"))

    fade_state = dict(default_state)
    fade_state.update({"state": 1, "substate": 2, "timer": 2, "main_logo_alpha": 252.0})
    apply_row_model(fade_state, row_for_phase(update_rows, "fade_in_substate_2_main_logo_alpha"))

    fade_out_state = dict(force_state)
    fade_out_state.update({"state": 3, "substate": 5, "delay_timer": 0})
    apply_row_model(fade_out_state, row_for_phase(update_rows, "fade_out_state3"))

    draw_snapshot_rows: list[dict[str, Any]] = []
    for row in draw_rows:
        alpha = alpha_for_offset(default_state, int(row["alpha_field_offset"]))
        draw_snapshot_rows.append(
            {
                "draw_index": row["draw_index"],
                "submit_order": row["submit_order"],
                "component_index": row["component_index"],
                "component_role": row["component_role"],
                "visible": alpha > 0.0,
                "alpha": alpha,
                "alpha_normalized": alpha * float(row["alpha_scale"]),
                "matrix_role": row["matrix_role"],
                "handle_field_offset": row["handle_field_offset"],
                "alpha_field_offset": row["alpha_field_offset"],
                "effect_alpha_field_offset": row["effect_alpha_field_offset"],
            }
        )

    checks = {
        "orchestration_audit_ok": bool(orchestration["summary"]["ok"]),
        "actor_init_is_enmag": len(actor_init_rows) == 1 and actor_init_rows[0]["actor_name"] == "ACTOR_EN_MAG",
        "component_count_is_3": len(component_rows) == 3,
        "draw_count_is_3": len(draw_rows) == 3,
        "update_runtime_count_is_13": len(runtime_rows) == 13,
        "no_unknown_update_ops": len(runtime_rows) == len(update_rows),
        "default_init_clears_alphas": all(
            default_state[key] == 0.0
            for key in ["title_text_alpha", "main_logo_alpha", "copyright_alpha", "effect_alpha"]
        ),
        "default_init_uses_native_timer_60": default_state["timer"] == 60,
        "force_rising_sets_display_state": force_state["state"] == 2 and force_state["delay_timer"] == 30,
        "force_rising_sets_all_alphas_255": all(
            force_state[key] == 255.0
            for key in ["title_text_alpha", "main_logo_alpha", "copyright_alpha", "effect_alpha"]
        ),
        "main_logo_alpha_step_clamps_to_255": fade_state["main_logo_alpha"] == 255.0,
        "fade_out_subtracts_native_default_step": fade_out_state["title_text_alpha"] == 245.0
        and fade_out_state["main_logo_alpha"] == 245.0
        and fade_out_state["copyright_alpha"] == 245.0,
        "draw_snapshot_default_all_hidden": all(not row["visible"] for row in draw_snapshot_rows),
        "demo_host_binds_logo_draw_snapshot_to_render_scene": "ApplyTitleIntroOpeningFrameRuntimeLogoDraw" in demo_text,
        "demo_host_applies_logo_material_color_slot_5": "kOot3dTitleIntroLogoMaterialColorSlot" in demo_text
        and "ApplyOot3dNativeRenderModelRuntimeMaterialColorOverride" in demo_text,
    }
    audit = {
        "ok": all(checks.values()),
        "source": {
            "orchestration": str(ORCHESTRATION.relative_to(ROOT)).replace("\\", "/"),
            "source_table": str(SOURCE_TABLE.relative_to(ROOT)).replace("\\", "/"),
        },
        "counts": {
            "components": len(component_rows),
            "draw_rows": len(draw_rows),
            "update_source_rows": len(update_rows),
            "update_runtime_rows": len(runtime_rows),
        },
        "checks": checks,
        "sample_states": {
            "default": default_state,
            "force_rising_button": force_state,
            "main_logo_fade_step": fade_state,
            "fade_out_step": fade_out_state,
        },
        "runtime_rows": runtime_rows,
        "draw_snapshot_default": draw_snapshot_rows,
        "unresolved": [
            "Backend render binding is present in the demo host; full engine-side CMB handle/submit-manager parity still needs runtime/three_ds_recomp integration beyond this standalone path.",
            "Audio/global transition side effects remain represented as result flags.",
            "Resource version selection for JP/EU vs US CMB/CSAB needs engine resource wiring.",
        ],
    }

    write_json(OUT_JSON, audit)
    write_csv(OUT_UPDATE_CSV, runtime_rows)
    write_csv(OUT_DRAW_CSV, draw_snapshot_rows)
    write_text(OUT_MD, build_markdown(audit, runtime_rows, draw_snapshot_rows))
    print(f"wrote {OUT_MD.relative_to(ROOT)} ok={audit['ok']}")


if __name__ == "__main__":
    main()
