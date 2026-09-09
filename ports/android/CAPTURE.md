# Android Display Capture

Developer tooling only, independent from the renderer and APK. scrcpy is the
default; Android's `screenrecord` is a dependency-minimal fallback. Both capture
the device display, not a Windows/Linux mirror window. No root, persistent app
installation, device setting changes or input injection is required.

## Local USB Host

Verified on 2026-09-09, Windows USB bridge to the SM-S931B (Android 16):

- ADB: `I:/oot3dre_tools/android/platform-tools/adb.exe`.
- scrcpy: `I:/oot3dre_tools/android/scrcpy-4.1/scrcpy-win64-v4.1/scrcpy.exe`.
- Official scrcpy 4.1 Windows x64 archive SHA-256:
  `5b12172b3264b2889f4583ee64752ce832e29bc8b1089dca81093459697165db`.
- Private captures: `I:/oot3dre_work/android-captures/`.
- Local launchers in that directory: `Capture-Android.ps1`, `Preview-Android.ps1`.
- Linux remains the Android build host. The Python helper works on either host
  with Python >= 3.11, ADB and scrcpy; no SDK/compiler is needed for capture.

The binaries, their licenses and capture data stay outside the repository. scrcpy
is an external diagnostic tool, not a runtime or release dependency.

## Record

From the source root, PowerShell:

```powershell
$env:ADB = 'I:/oot3dre_tools/android/platform-tools/adb.exe'
$env:SCRCPY = 'I:/oot3dre_tools/android/scrcpy-4.1/scrcpy-win64-v4.1/scrcpy.exe'
python tools/android/capture_device.py --output I:/oot3dre_work/android-captures --seconds 15
```

Optional switches:

- `--audio`: record internal audio through scrcpy (AAC), without playing it on
  the host. No microphone capture. The phone/app must permit audio capture.
- `--package <running.app.id>`: record only that process's epoch-timestamped
  logcat. No global log collection/clearing; no package is guessed.
- `--serial <adb-device>`: select explicitly when multiple devices are attached.
- `--backend adb`: native screenrecord, video only; `--size 1280x720` is optional.
- `--ffprobe <path>`: validate actual decoded video frames. Auto-detected on PATH.

Each invocation creates a distinct UTC/UUID directory with `display.mp4`,
`before.png`, `after.png`, recorder output, optional `logcat.txt`, media metadata
and `capture.json` (device model/API/ABI, timestamps, sizes, hashes and result).
PNG bytes are transferred directly with `adb exec-out`; PowerShell never
converts binary output to text. Durations are bounded to 1-180 seconds. The ADB
fallback removes only its own UUID-named temporary video from the device.
On host interruption, native screenrecord still has its requested device-side
time limit; it cannot become an unlimited background recording.

The helper refuses output inside its source repository. Captures may include
private screen content and logs; review them before explicitly sharing. They
must not enter public source archives or release packages.

## Read-Only Preview

```powershell
& $env:SCRCPY --no-control --no-audio --window-title='TriAevum Android'
```

This displays the device but does not forward keyboard/mouse input. Use the
phone's controls. Close the window to stop. To save evidence, use `--record` or
the helper above, never a desktop screen recorder pointed at this window.

## Interpretation and Tests

- Device recording can add encoder/GPU load. Do not use it for baseline FPS or
  compare capture-on results to capture-off results as an optimization claim.
- Video frame count is **not game FPS**. Static screens produce few frames;
  scrcpy records device timestamps. Requested duration includes encoder startup
  and may exceed the duration of actual decoded frames.
- MP4 is lossy composed color output, not a depth/normal attachment dump. For
  PICA fidelity and orientation use the renderer's own captures as well.
- Host/device timestamp samples bracket each run; they are not a shared
  per-draw GPU clock. PID-filtered logcat stops following the app after a process
  restart. A new process needs a new capture.
- The tool does not unlock or wake the phone. Verify the visible state first;
  a valid MP4 of standby/lock UI does not establish gameplay rendering.

`python -m unittest tools.android.test_capture_device` passes ten checks covering
device selection, output isolation, unique sessions, bounded/no-control commands
and byte-exact PNG transfer. Real scrcpy capture produced a decodable H.264
1080x2340 MP4; the native screenrecord fallback was also exercised. The first
device test captured the charging standby screen, not TriAevum. App-filtered
logs and audio are instrumented but not yet qualified with the Android game.

Primary references: [scrcpy recording](https://github.com/Genymobile/scrcpy/blob/v4.1/doc/recording.md),
[official Windows release](https://github.com/Genymobile/scrcpy/releases/tag/v4.1),
[ADB recording](https://developer.android.com/tools/adb#screenrecord).
