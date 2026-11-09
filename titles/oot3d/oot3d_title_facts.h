#pragma once

// OoT3D title facts the shared runtime reads (guard words: Echoes of Time,
// Ocarina of Time 3D, EUR code.bin 16a6b0aa...).

namespace Oot3dNativeGame {

// Release decision D4 (G1b-01, 2026-11-08): OoT3D's draws render only the
// bit-exact specialized program, in interactive play as in fixed-step
// sessions. Since e490e335 regenerated the OoT3D vertex family, a draw whose
// specialized program was still compiling could render the always-available
// (parametric) program, which is not bit-exact (c24bd592: 7 px, red +-8 in
// Kokiri frames 100/200). OoT3D therefore completes the specialized program
// instead, as every OoT3D draw did before the vertex repair; startup
// preparation and the pipeline cache are unchanged. Owner acceptance of the
// transient difference would turn this off.
inline constexpr bool kOot3dDrawsCompleteSpecializedPrograms = true;

} // namespace Oot3dNativeGame
