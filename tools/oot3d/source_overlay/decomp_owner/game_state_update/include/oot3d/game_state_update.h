#ifndef OOT3D_GAME_STATE_UPDATE_H
#define OOT3D_GAME_STATE_UPDATE_H

#include <stddef.h>
#include <stdint.h>

typedef struct Oot3dGameStateUpdateView {
    uint8_t pad_000[0x04];
    uint32_t main;
    uint8_t pad_008[0xF0];
    uint32_t frames;
} Oot3dGameStateUpdateView;

_Static_assert(offsetof(Oot3dGameStateUpdateView, main) == 0x04,
               "GameState.main offset");
_Static_assert(offsetof(Oot3dGameStateUpdateView, frames) == 0xF8,
               "GameState.frames offset");

uint32_t oot3d_game_state_read_main(
    const Oot3dGameStateUpdateView* game_state);
uint32_t oot3d_game_state_increment_frames(
    Oot3dGameStateUpdateView* game_state);

#endif
