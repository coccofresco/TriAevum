# MM3D native timing receipts (not a timing policy)

EUR v1.1 guard windows come from `analysis/hooks/MAP_4.md` and were checked
against the original image. All five PCs already exist in registry v10; no
registry generation or instruction replacement is required.

- `106750`: GameState_Update receives graphics context in r0, **state in r1**.
  Observe state frame +138/main +104 and native VSync/interval.
- `1F9C70`: Player_Update receives player in r0/play in r1. Confirm the play
  player owner and actor identity before reading flags, speed, ledge bytes and
  SaveContext form. Focus actor is **+DFC**, not +11DFC (MAP_4 audit correction).
- `124EC0`: limiter begin receives its state in r0.
- `124F6C`: deadline decision uses state r4; the native comparison is signed
  `deadline > vsync` after a VSync wait. The observer follows that comparison;
  it does not import OoT's modulo-counter rewrite.
- `124F7C`: limiter commit inputs use state r4. Fields C8/CC/D0 are maximum,
  interval and deadline. The bounded interval histogram records **inputs before
  the instruction**, not committed output. Bin16 includes every input >=16.

Native VSync is 6A3A08; native interval is 6A3A14. This interval also has an
outer-loop consumer. It is NOT a simulation-rate knob equivalent to OoT's
`[51B2F4]+110`. MM native motion/math owners fold the 30 Hz factors into code;
changing the limiter alone cannot qualify 60 Hz simulation.

The observer is read-only, accepts const CPU registers, allocates no per-frame
storage and calls no guest-write API. All guards and registry boundaries must
verify before any data observation. Invalid reads/identities clear the current
snapshot and count failures. Reset clears snapshots but retains aggregate run
counters; frame rollback/state change is counted only within an observed epoch.
The host summary labels the receipts `read_only=true`; no snapshot feeds a
simulation clock or interpolation identity.

These receipts implement the missing GameState observation and player probe
mechanics. They do **not** implement G5.02 slow-host catch-up, G5.03 temporal
identity, or G5.05 true60. Player forms, transition/pause and rate combinations
still need acceptance. The existing native instructions, Restoration-owned
frame increment at 106740, three-day clock and OoT policy remain unchanged.
