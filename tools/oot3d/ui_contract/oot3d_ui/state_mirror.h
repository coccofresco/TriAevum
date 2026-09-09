#pragma once

#include "oot3d_ui/ui_semantics.h"

namespace oot3d::ui {

// Incrementally populated host mirror. `semantic` is the format-neutral OoT3D
// source of truth for new code. The scalar projection is retained so the
// existing diagnostic N64 prototype keeps identical behavior while its state
// probes migrate to the native button and inventory shapes.
struct Oot3dUiStateMirror {
    int hearts_current = -1;
    int hearts_max = -1;
    int magic_current = -1;
    int magic_max = -1;
    int rupees = -1;
    int b_button_item = -1;
    int c_left_item = -1;
    int c_down_item = -1;
    int c_right_item = -1;
    int ammo_bow = -1;
    int ammo_slingshot = -1;
    int ammo_bombs = -1;
    int ammo_deku_nuts = -1;
    int ammo_deku_sticks = -1;
    int ammo_beans = -1;
    int small_keys = -1;
    int scene_id = -1;
    int room_id = -1;
    int pause_state = -1;
    Oot3dUiSemanticState semantic;
};

} // namespace oot3d::ui
