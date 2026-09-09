# Steam-Compatible Linux Build

Publisher/developer workflow, not a Forge installation step. The player still
uses a precompiled runtime and title module plus their personal decrypted ROM.
This work does not yet qualify a Steam Deck release or Linux feature parity.

## Toolchain Boundary

Use Valve's Steam Linux Runtime 4 SDK, version `4.0.20260805.254769`, rather than
libraries from a rolling host distribution. The previous CachyOS executable
required `GLIBC_2.43` and host-specific library versions. A successful build on
that machine alone did not establish portability.

- SDK archive SHA-256:
  `d105e579c1261755714b403bafd842e9acb3d79d5a13df869693818a49b72379`.
- SDK Clang 19.1.7, CMake 3.31.6, Debian trixie ABI baseline.
- Additional development packages are pinned in
  `scripts/steamrt4-sdk-dependencies.sh`. APT verifies their downloads against
  indexes supplied by the checksum-verified SDK; the script never updates them.
- The preparation script extracts packages into the local SDK without running
  installation scripts, changing the host package database, or requiring sudo.
- During builds, Bubblewrap exposes source and SDK read-only. Only the separate
  work directory is persistent and writable. Compiler jobs default to three.
- The SDK is approximately 1 GiB compressed / 3.4 GiB unpacked. Reuse it and the
  incremental build directory; do not ship either to players.

Primary references: [Valve's runtime guidance](https://github.com/ValveSoftware/steam-runtime),
[pinned SDK](https://repo.steampowered.com/steamrt4/images/4.0.20260805.254769/),
[Steam Linux deployment](https://partner.steamgames.com/doc/store/application/platforms/linux).

## Prepare and Build

Run these Bash commands from the public source checkout. The host requires
`bwrap` with unprivileged user namespaces, `curl`, `tar`, and coreutils; no
host compiler, Docker daemon, system upgrade, or decompilation checkout.

```sh
SDK="$HOME/triaevum-sdk4"
WORK="$HOME/triaevum-sdk4-work"
bash scripts/prepare-steamrt4-sdk.sh "$SDK" "$WORK"
bash scripts/test-steamrt4-environment.sh "$SDK" "$WORK"
bash scripts/run-in-steamrt4.sh "$SDK" "$WORK" \
  env TRIAEVUM_BUILD_DIR=/home/work/runtime bash /home/src/scripts/build-linux-runtime.sh
```

The preparation helper verifies the archive before extraction, refuses to
overwrite unrecognized directories, and recognizes a completed SDK by archive
and dependency-recipe identity. An interrupted extraction is never treated as a
valid SDK; preserve it for diagnosis or choose a new destination. The archive
cache can be reused. All paths must be outside the source checkout and mutually
non-overlapping. The helper retains package SHA-256 records inside the SDK.

Build the **already published**, manifest-verified translated title separately:

```sh
export TRIAEVUM_TRANSLATED_TITLE_DIR="/absolute/path/to/extracted-title-source"
bash scripts/run-in-steamrt4.sh "$SDK" "$WORK" \
  cmake -S /home/src/tools/triaevum_release/linux_title -B /home/work/title \
  -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release \
  -DTRIAEVUM_TRANSLATED_TITLE_DIR=/home/title-source
bash scripts/run-in-steamrt4.sh "$SDK" "$WORK" \
  cmake --build /home/work/title --parallel 3
```

Run the runtime and title builds sequentially. Strict floating point, source
hash checks, symbol visibility and ABI checks remain unchanged. This does not
perform a new translation. The runtime's root `triaevum_title_aot.so` is an empty
bootstrap dependency; the real game module is `title/triaevum_title_aot.so`.

## Qualification

- Source and SDK write protection, writable work directory, path overlap
  rejection, shell syntax, and distribution metadata fallback have passed.
- Clean SDK preparation and repeated no-op preparation have passed on the
  physical Linux host.
- Root CMake configuration succeeds without private UI evidence. Minimal SDKs
  no longer require `lsb_release`; Debian's `libshaderc.so` and legacy/GLVND
  OpenGL naming are handled through the build configuration.
- Runtime, neutral module and real title completed their SDK builds. Product
  inspection reports NRI/F1/TopScreen and ABI 2; the real title loads, and the
  empty bootstrap is rejected as a game title. All 12 native service tests and
  all three UI consumer tests (SDK GCC 14.2) pass. The Windows-side targeted
  source/Forge/catalog suite passes 37 tests; display-power tests add five.

## Binary and GPU Evidence (2026-09-09)

| Artifact | Bytes | Highest direct GLIBC requirement |
| --- | ---: | --- |
| TriAevum | 18,044,072 | 2.38 |
| Real title module | 84,791,720 | 2.14 |
| Neutral game module | 1,525,464 | 2.38 |

These are each ELF's requirements, **not** a claim that every dependency can
run on glibc 2.38. The complete closure is still qualified against Steam Runtime
4. Runtime SHA-256:
`9171f072572fcb9786dcbc86d4c875b0d3c691937cd747efa8eb81c7886d1469`.
Title SHA-256:
`ac927143b9cf98b99597eb52b6214c0358f8ea755757a0941641f48f289dca47`.

The new runtime/title completed a 60.02-second native run through the installed
Steam Runtime 4 on the physical RTX 4060, exit 0. Captures at 120, 420 and 720
were produced by the renderer; frame 720 was inspected and shows the title,
scene, grass and toon. TopScreen remained active. The private fixture used
SDK copies of `libzip.so.5`, `libtinyxml2.so.11`, `libspdlog.so.1.15`,
`libfmt.so.10` and `libshaderc.so.1`; no GPU driver or libc was copied into the
fixture's library directory. Final dependency/license packaging remains open.

This is not an FPS benchmark: it retains pacing, interpolation, effects and
framebuffer readback, and incurs cold caches. Maximum interval was **17.50 s**;
performance is not accepted. The run recorded 1,780 guest refreshes and 958
presentations, which must not be conflated into a native FPS measurement.

Private evidence: `I:/oot3dre_work/linux-port-proof/steamrt4-build-proof.json`,
`steamrt4-captures/`, and the corresponding Linux build/play directories.
The transferred source contains the portability changes accompanying this
document; its older remote Git HEAD is not an exact release-source identity.
Public packaging must bind freshly exported source to final artifacts.

Steam Runtime 4 is a **build ABI boundary**, not an emulator or renderer change.
Never bundle host GPU drivers, a host Vulkan ICD, or the SDK into the release.
The final runtime environment must satisfy the selected Steam runtime ABI.
The KDE mapping failure was traced to an output powered off by DPMS: the game
waited for an X11 event and Forge remained iconified. After waking the output,
the same frozen Forge passed with a normal 760x443 window, mapped controls and
no clipping, and the game completed the run above. Developer graphical helpers
now reject the known all-outputs-off state before launching tests. They never
change power/security preferences. See [Linux Forge](TRIAEVUM_LINUX_FORGE.md).
SSSR, rendering stalls/black flashes and on-device Steam Deck performance remain
independent acceptance items.
