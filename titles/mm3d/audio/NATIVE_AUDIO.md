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
| Mixer dispatch | entry `0x1d63bc`; calls `0x1d6408`, `0x1d641c` | four contiguous 160-word channels; selects delay **or** stereo, not both; native recursive lock acquisition/release |
| Delay | `0x1d9a80` | enable +0x56, channels +0x55, ring pointers +0x1c, filters +0x2c, block count/index +0x3c/+0x40, gains +0x44/+0x48/+0x4c |
| Stereo reverb | `0x1d9b70` | enable **+0x104**, not OoT +0x100; exactly two channels × 160 samples |
| Stereo rings | +0x3c/+0x4c/+0x5c/+0x60/+0x7c | channel stride 4 except paired comb pointers, stride 8 |
| Lengths/cursors | +0x9c/a0, +0xa4/a8, +0xac/b4, +0xb0/b8, +0xc4/c8 | advance 160 samples, wrap at native lengths |
| Coefficients | +0xbc/c0/cc, +0xe0/e4/e8/ec | two comb gains, feedback gain, dry/wet and filter pair |
| Filter history | +0xd0/+0xd4 | guest-owned, retained in guest memory snapshots |
| Parameter conversion | `0x1d9da0..0x1d9f90` | dynamically executed, including native `powf` at `0x1cdce0` and zero-fill tail `0x16e4e0`; ten parameter/mode cases |

`native_effect_tests.cpp` contains field/word guards and an independent integer
reference. It loads the user's code externally and compares every executed
registry instruction to that code. It executes the default initializer, four
channel delay, stereo reverb, negative samples, five blocks (including wraps),
disabled paths and copied-memory replay of the reverb tail. Alternating blocks
also pass through the real mixer dispatch and native recursive-lock helpers;
entry counters verify exactly one effect, including delay precedence when both
pointers are populated. Integer and VFP callee-save registers and the stack are
seeded and checked. No ROM payload or PCM capture is stored in Git. It fails on
a missing/wrong title registry.

Parameter tests distinguish the standalone default parameter structure from the
effect object, whose parameter block starts at **+4**. They verify minimum/block
quantization (4.888 ms / 160 samples), delay lengths, cursor reset, two decay gains
against an independent `pow(10, -3 * delay / decaySamples)` model, diffusion/dry/wet
conversion, both damping modes and the .95 upper clamp. Native ring zeroing is
bounded by sentinels, and the separately guest-owned filter history is retained.
No math calls are intercepted or replaced by host answers. The extended offline
suite passes **669,863 checks** on the local supported image.

Build the optional `mm3d_native_effect_tests` target with the MM A32 registry,
then pass the local EUR `code.bin`. This is an offline kernel test, not a game
launch and not proof of an audible room transition or physical audio device.

## Shared platform proof and remaining acceptance

Existing offline suites cover DSP differential mixing and save/load, codecs,
resampler storage, source playback/sleep, and output policy/gain/ramping.
`audio_output_policy_tests` requires exact identity at 100% gain and a reference
match at 50%. These do not replace testing SDL/WASAPI/PulseAudio/ALSA devices.

The window summary additionally reports the actual output backend (including a
Null fallback), current-player identity, output policy and focus state. These are
observations only: DSP samples, output policy and backend behavior are unchanged.
`--audio-pcm-dump` records the **native pre-policy** stream, not physical-device
output; it must not be described as an audible/device capture.

Still required for FULL F14: qualified in-game room transitions, pause/mute/preview,
state-load tails and supported-platform device listening/hotplug/reopen evidence.
Safe single-shot locale/audio runs and their exact successes/omissions are kept
in the external LANGAUDIO results. The parameter conversion and native effect
mixer are now dynamically covered; this offline fixture is not an entire
mixer-to-DSP/device replay. No FULL F14 closure is claimed by offline proof alone.

### Non-silent live state smoke and real-time limit (fourth continuation)

Runtime production source remains `3047bf8a`; the executed dual-title/v10 binary
was built at `07a87492` (later changes are offline tests/documentation only).
The external LANGAUDIO receipts bind exact executable/input hashes.

- One cold-boot real-time acceptance ran 75.022 seconds, producing 51.342 seconds
  of native PCM, 111 underruns and 1,411 dropped refreshes. Only 1,170 presentation
  frames completed: save at 1,100 happened, load at 1,400 and capture at 1,700 did
  not. The three-refresh limit was active; 15.596 presentations/second cannot
  sustain 60 refreshes/second under that limit. Native pipeline creation totaled
  0.293 seconds; attributing the whole deficit to cold pipeline compilation is
  unsupported. Guest/render timers overlap; host contention was not isolated.
- The single complementary fixed-clock smoke loaded that newly saved state,
  saved once and loaded it live once. **218,240 stereo frames (6.668296 seconds)
  replay byte-identically**, with nonzero samples in all seven intersected
  one-second blocks. Native capture rate/count/FNV agree with runtime. No stale
  pre-load native audio appears in that compared interval.
- The actual post-load image is **English file select**, not gameplay. It proves
  continued rendering and a native music replay, not a room transition or an
  active reverb tail. The intended cold-boot gameplay route was not achieved.
  Fixed-clock playback reports one underrun; it is not device-continuity proof.

The native non-silent state replay subgate is closed for this file-select
fixture. FULL F13/F14 remain open: real-time starvation, gameplay/room/effect
and interactive/physical-platform audio qualification remain unresolved. No
repeat of the failed timed test or speculative buffer/catch-up increase was
performed. Evidence: `LANGAUDIO/{cold-replay-04,fixed-state-smoke-04}-analysis.json`,
`cold-replay-04-diagnosis.json`, and `fixed-state-smoke-04/post-restore.png`.
