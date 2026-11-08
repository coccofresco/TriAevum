# MM3D EUR native effects and platform output

## Ownership

MM gameplay already executes its native ARM delay/reverb before the shared
Azahar DSP HLE and `Ship::AudioOutputPolicy`. Do **not** attach the stand-alone
`NativeAudioService::MountReverbProfile` mixer to that path: it is used by tools
and tests, not by game playback, and would apply an additional effect.

The OoT `Oot3dEurRev0AudioReverbLayout` and compiled kernels are not MM layouts.
This change deliberately does not modify them, DSP mixing, resampling, volume,
focus policy, backend selection, output gain or shader artifacts.

## Semantically located EUR v1.1 ABI

Local code SHA-256:
`6eec27b1827eab2c87744ac38066d569ffe6fe75790b1eb3300bd91f5099038d`.
Addresses are native virtual addresses, not offsets into a file.

| Owner | MM address / fields | Evidence |
|---|---|---|
| Default initializer | `0x1628b0` | stores 60/4000/100 from r2/r3/r12, not OoT's immediate-register pattern |
| Float defaults | `0x1628f0/f4/f8` | damping/wet .4, diffusion .5, dry .6; stores observed at +0xc/+0x10/+0x18/+0x1c |
| Delay table | pointer at `0x1628fc` → `0x6a406c` | static 3040/3680/2080; not a relocated OoT table |
| Mixer dispatch | `0x1d6408`, `0x1d641c` | calls delay and stereo effect respectively |
| Delay | `0x1d9a80` | enable +0x56, channels +0x55, ring pointers +0x1c, filters +0x2c, block count/index +0x3c/+0x40, gains +0x44/+0x48/+0x4c |
| Stereo reverb | `0x1d9b70` | enable **+0x104**, not OoT +0x100; exactly two channels × 160 samples |
| Stereo rings | +0x3c/+0x4c/+0x5c/+0x60/+0x7c | channel stride 4 except paired comb pointers, stride 8 |
| Lengths/cursors | +0x9c/a0, +0xa4/a8, +0xac/b4, +0xb0/b8, +0xc4/c8 | advance 160 samples, wrap at native lengths |
| Coefficients | +0xbc/c0/cc, +0xe0/e4/e8/ec | two comb gains, feedback gain, dry/wet and filter pair |
| Filter history | +0xd0/+0xd4 | guest-owned, retained in guest memory snapshots |
| Parameter conversion | `0x1d9da0..0x1d9f90` | conversion literals include 32728 and -3 at `0x1d9f74/78`; mapped, not claimed dynamically exercised |

`native_effect_tests.cpp` contains field/word guards and an independent integer
reference. It loads the user's code externally and compares every executed
registry instruction to that code. It executes the default initializer, four
channel delay, stereo reverb, negative samples, five blocks (including wraps),
disabled paths and copied-memory replay of the reverb tail. No ROM payload or
PCM capture is stored in Git. It fails on a missing/wrong title registry.

Build the optional `mm3d_native_effect_tests` target with the MM A32 registry,
then pass the local EUR `code.bin`. This is an offline kernel test, not a game
launch and not proof of an audible room transition or physical audio device.

## Shared platform proof and remaining acceptance

Existing offline suites cover DSP differential mixing and save/load, codecs,
resampler storage, source playback/sleep, and output policy/gain/ramping.
`audio_output_policy_tests` requires exact identity at 100% gain and a reference
match at 50%. These do not replace testing SDL/WASAPI/PulseAudio/ALSA devices.

Still required for FULL F14: safe single-shot in-game music/SFX/room transition,
pause/focus/mute and save/load-tail checks, plus platform device listening or
capture evidence. The parameter-conversion function and complete mixer-to-DSP
handoff are not dynamically exercised by this fixture. No FULL F14 closure is
claimed by the offline evidence alone.
