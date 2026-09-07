#include "oot3d/game_state_update.h"

uint32_t oot3d_game_state_read_main(
    const Oot3dGameStateUpdateView* game_state) {
    return game_state->main;
}

uint32_t oot3d_game_state_increment_frames(
    Oot3dGameStateUpdateView* game_state) {
    return ++game_state->frames;
}
