# TriAevum diagnostics

How TriAevum reports a failure so that nobody needs a screenshot, what a
support bundle contains, and the test-only switches that simulate failures
(UX plan WP-03). Engine: `tools/triaevum_forge/crates/forge-core/src/diagnostics.rs`
and `preflight.rs`; runtime: `tools/oot3d/native_game_runtime/triaevum_fatal_report.*`,
`triaevum_console.*`, `runtime/three_ds_recomp/src/fast/extension/display_recovery.*`.

## When the game stops with an error

- The runtime writes `TRIAEVUM_FATAL <message>` (one line) to its error
  output and, when its log is open, a `[critical]` line to
  `logs/oot3d_native_game.log` in its working directory.
- An interactive desktop launch then shows a native dialog, "TriAevum could
  not continue", with the message, the log folder (only when the log was
  written) and a pointer to Forge's Diagnostics page. There is no dialog
  - after a crash handler already showed its own crash dialog;
  - on Android (the installer's Diagnostics screen reads `logs/native-stderr.log`);
  - for scripted runs and tool modes: `--input-timeline`, `--frames`,
    `--max-seconds`, `--fixed-delta-seconds`, `--graphics-settings-schedule`,
    `--screenshot`, `--screenshot-sequence`, `--throughput-benchmark`,
    `--scenario-auto-exit`, `--convert-state`, `--audio-pcm-dump`,
    `--product-info`, `--game-language-info`, `--prebuild-interior-sun`,
    `--verify-title-plugin`;
  - with `TRIAEVUM_NO_FATAL_DIALOG=1` (any value except empty or `0`).
  Automation that starts the game interactively must set
  `TRIAEVUM_NO_FATAL_DIALOG=1` or it waits for someone to close the dialog.
- The exit code is 1 (2 for invalid arguments); a crash ends with the
  system's code (`0xC0000005`, `0xC0000409`, a signal).

## Windows console (owner decision 12)

`TriAevum.exe` is a GUI-subsystem program: players never see a console
window. Output that is redirected is inherited as before, so these keep
stdout, stderr and the exit code: Bash (`det_run.sh`, `play_run.sh`,
`> log 2> err`), Python `subprocess` with pipes, Forge (`popen_native`), and
PowerShell `Start-Process -Wait -RedirectStandardError ...`.

PowerShell treats a GUI program differently: a bare `& TriAevum.exe ...` or
`& TriAevum.exe ... > file` neither waits nor sets `$LASTEXITCODE`. Pipe it
(`& TriAevum.exe ... 2>&1 | Out-Host`, or `| Out-File log`) or use
`Start-Process -Wait`.

A developer console: `TriAevum.exe --console ...` (the option is removed
before the arguments are parsed; accepted and ignored on other systems) or
`TRIAEVUM_CONSOLE=1`. The game attaches to the console of the program that
started it, else opens its own; streams already redirected stay where they
go. From `cmd`, use `start /wait TriAevum.exe --console ...` so the prompt
waits.

## What Forge shows

- **Play** keeps the game's process. Its error output goes to
  `data/logs/game-stderr.log` (replaced at each launch), the run is recorded
  in `data/logs/last-game-run.json` (start, duration, exit code), and an
  error exit becomes the card "The game closed with an error": the exit code
  in words, the game's `TRIAEVUM_FATAL` message and the last 40 lines of the
  game log and error output written during that run. A clean exit shows
  nothing. `--play` records the run as well.
- **Diagnostics** (desktop page, and the Android installer's Diagnostics
  screen): the installation state, the last game run, a display recovery,
  the log folders, the support bundle's file list, **Copy diagnostics** and
  **Save support bundle...** (Android: **Share diagnostics**, `ACTION_SEND`
  of the zip through a non-exported provider that serves only that file).
- **Display recovery**: a launch marker left by a launch that ended within
  its first minute makes the runtime reset the display settings
  (`TRIAEVUM_SAFE_PRESENTATION`). The runtime then writes
  `logs/last-recovery.json` beside its log (`triaevum_display_recovery_v1`:
  time, reason, fields changed) and shows a notice from the first drawn
  frame; Forge's Diagnostics shows the record.

The CLI prints the same report: `triaevum-forge diagnostics [--package-root P]
[--data-root D] [--bundle out.zip]`; the C ABI is `TriAevumForgeDiagnostics`
(v4) and JNI `ForgeNative.diagnostics`.

## The report and the support bundle

The copyable report is at most 16 KB: Forge and package versions, the
system, the installation state, the graphics device from the game log, the
last game run, the game's last fatal message, display recovery, the runtime
preflight error, crash-dump names, the last Forge job and the log tails.

The bundle is an allowlist; nothing else is read:

| In the zip | Source |
|---|---|
| `diagnostics.txt`, `manifest.json` | the report; members, omissions, redaction note |
| `crash-dumps.txt` | names, sizes and times of `*.dmp` files (Windows); never the dumps |
| `forge-logs/forge.log`, `forge.log.1`..`.4` | Forge's log |
| `forge-logs/game-stderr.log`, `last-game-run.json`, `preflight-error.json` | Forge's records |
| `game-logs/oot3d_native_game.log`, `.1.log`, `.2.log` | the runtime's log and two earlier launches |
| `game-logs/native-stderr.log`, `last-recovery.json` | Android error output; display recovery |
| `settings/TriAevum.json` (`TriAevum.android.json`) | the runtime settings |
| `state/<launch profile>`, `state/runtime-state.json` | launch arguments; runtime summary up to 2 MB |
| `android/game-stages.txt`, `java-failure-*.txt` | Android startup records |

Never included: saves, save states, game sources, prepared titles, caches,
mods, downloads, texture packs, crash dump files, any file not listed.

Redaction applies to every text line and to every JSON string and key: the
installation's own folders become `<package>`, `<activation>` and `<data>`
(either separator, JSON-escaped spellings, ASCII case on Windows), then any
remaining path component that is a home folder's user name or the current
user name becomes `<user>`. Logs keep their last 2 MB, a JSON record above
2 MB is left out, the whole bundle stops at 16 MB before compression. The zip
is built in memory and renamed into place, so an interrupted save never
leaves a partial bundle.

## Input devices in the game log

At startup and whenever a pad is connected, removed or selected, the runtime
writes the input devices to its log (`game-logs/oot3d_native_game.log` in the
support bundle), at warning level so release builds keep it:

```
TRIAEVUM_INPUT_DEVICES reason=startup pads=2 selected=5 video_driver=windows
TRIAEVUM_INPUT_PAD id=5 selected=1 name="Xbox Series X Controller" type=xbox_one guid=0300... prompts=xbox gyro=0 accel=0 touchpads=0 rumble=1 serial=present
TRIAEVUM_INPUT_KEYBOARD enabled=1 focus=1 prompts=keyboard
```

A serial number is never written (only `serial=present` or `none`); a device
name loses control characters and quotes, and the account name (`USERNAME`,
`USER`, `LOGNAME`) becomes `<user>`. SDL 2 does not list keyboards, so the
keyboard line gives the Keyboard switch, the window's keyboard focus and the
video driver.

`TRIAEVUM_INPUT_TRACE=1` (for keyboard reports such as #123) adds, in the
same log, every key event SDL delivers (`sdl_key down scancode=4 (A)
keycode=0x61 (A) repeat=0 mod=0x0000 window=2 focus_window=2`), keyboard focus
changes (`sdl_focus gained|lost`), each bound key's edges
(`action=A 3ds=A keys=X/None raw=1 effective=0 keyboard_enabled=1
captured=1`: `raw` is the key state the window keeps, `effective` what the
game reads after the Keyboard switch and a host menu's capture) and each
rumble played. It reads events only.

## Checks before a preparation

Before the import writes anything (`forge-core/src/preflight.rs`):

- the installation folder and the data folder accept a new file (create and
  delete a probe); a refusal is `permission_denied` / read-only destination;
- the data folder's volume has room for RomFS + code of the selected input,
  the package's largest shader corpus and a 512 MB margin; otherwise
  `storage_full` with reason `insufficient_space`, whose details give the
  required and free amounts ("needs about 3.8 GB ... 1.0 GB are free. Free
  at least 2.8 GB"); a write that still runs out of space fails as before;
- the Vulkan loader (`vulkan-1.dll`, `libvulkan.so.1`) resolves; when it does
  not, a warning is reported and the preparation continues.

## Test-only switches

| Variable | Effect |
|---|---|
| `TRIAEVUM_TEST_FATAL=vulkan-device` | the runtime fails right before creating its window with "the Vulkan device could not be created (simulated ...)": exercises the dialog, the `TRIAEVUM_FATAL` line, the game log and Forge's card |
| `TRIAEVUM_TEST_FATAL=crash` | the runtime faults right after creating its window: exercises the crash handlers (crash dialog, dump in `savedata/crashes` on Windows product builds) and Forge's card for a crash exit code |
| `TRIAEVUM_NO_FATAL_DIALOG=1` | no fatal dialog (automation) |
| `TRIAEVUM_CONSOLE=1` | as `--console` |
| `TRIAEVUM_INPUT_TRACE=1` | key events, focus changes, bound-key edges and rumble in the game log (see "Input devices in the game log") |
| `TRIAEVUM_GUI_RUNTIME=<TriAevum.exe>` | runs `forge-native/tests/gui_runtime_launch.rs`: Forge's product query and a failed run's captured error output against a built runtime |

A display recovery can be reproduced by creating `<config>.launching`
beside the runtime settings before a launch.
