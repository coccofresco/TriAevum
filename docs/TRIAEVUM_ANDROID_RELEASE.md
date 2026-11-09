# TriAevum Android releases (Echoes of Time, Moonfall Curse)

Status: **in preparation, not qualified.** No release APK has been built or run on a device yet. The developer APK
(`org.triaevum.android`, `0.1-intro-dev`, Gradle flavor `dev`) is unchanged and is still not a supported release.

Both products ship as separate arm64 APKs from one Gradle module (`ports/android/app`), one flavor per title:

| | Echoes of Time | Moonfall Curse |
|---|---|---|
| title | `oot3d` | `mm3d` |
| Gradle flavor | `eot` | `mc` |
| application id | `org.triaevum.echoesoftime` | `org.triaevum.moonfallcurse` |
| version | `0.8.0-beta.2` (versionCode 800202) | `0.5.0-alpha.1` (versionCode 500101) |
| launcher icon | `tools/triaevum_release/branding/masters` | `titles/mm3d/branding/masters` |

The facts are in `titles/<title>/android.json`. Gradle and the APK audit read the same file; a missing or invalid fact
fails the build. versionCode = `M*10^7 + m*10^5 + p*10^3 + stage*100 + n` (alpha 1, beta 2, rc 3, final 9).
The application ids can change only until the first public release.

No game assets are included in either APK. Each APK's Forge prepares the game on the device from the player's own
decrypted cartridge image, chosen through the system document picker.

## Publisher flow (per title, one at a time)

1. **Translated sources.** Use the translated title sources of the title's desktop release (the same sources, never
   regenerated: Moonfall Curse builds use the registry `a32_generated_overlay_r18`).
2. **Title module.** Configure `ports/android/native` with the NDK toolchain and build the title target:
   `-DTRIAEVUM_ANDROID_TITLE=<oot3d|mm3d> -DTRIAEVUM_TRANSLATED_TITLE_DIR=<sources> -DTRIAEVUM_ANDROID_TITLE_OPTIMIZATION=1`
   (strict floating point, `-g0`, 16 KiB pages). An unknown title, or sources whose manifest names another title,
   fail at configure time.
3. **Forge library.** Build `crates/forge-android` for `aarch64-linux-android` (release) with the title's
   `forge_features` from `titles/<title>/android.json`: none for Echoes of Time, `--features title-mm3d` for
   Moonfall Curse. Without that feature the library refuses to prepare Moonfall Curse (dekui and Project
   Restoration produce-at-install are compiled in only with it, as in the desktop MC Forge).
   Then **stage** the runtime, renderer, title module, Forge library and `libc++_shared.so` with
   `tools/android/stage_native_app.py` (stripped).
4. **Forge package.** `tools/triaevum_release/android_forge_package.py --title <title> --reference-package <desktop
   release> --native-stage <stage> --source-commit <commit> --output <dir>`. The reference must be that title's
   desktop release. Its licences and notices are copied to `notices/`. Game content (`.gar`, `.mfpj`, ROM containers)
   is refused. Add `--sign` with `TRIAEVUM_RELEASE_SIGNING_KEY` naming the release key (outside the repository and
   the package): it writes `release-signature.json` over the exact bytes of `forge-package.json` (the desktop
   signature scheme and domain). On the device `ForgeNative.verifyPackage` checks that signature under the
   publisher keys built into Forge (no developer override on Android) and then the size and SHA-256 of every
   package file; the APK audit refuses an APK without the signature.
5. **APK.** `gradlew :app:assemble<Eot|Mc>Release -PtriaevumSdlSource=… -PtriaevumNativeStage=<stage>
   -PtriaevumForgePackage=<dir>`.
6. **Audit.** Run `aapt2 dump badging` and `apksigner verify --print-certs`, then
   `android_release_policy.py audit <apk> --title <title> --badging <file> --signer-sha256 <sha>
   --expected-signer <release cert sha>`.

## Signing

The release signing config reads only `TRIAEVUM_ANDROID_KEYSTORE`, `TRIAEVUM_ANDROID_KEYSTORE_PASS`,
`TRIAEVUM_ANDROID_KEY_ALIAS` and `TRIAEVUM_ANDROID_KEY_PASS` from the environment. Without them every release task
fails before building; there is no fallback to the debug key. The keystore and passwords never enter the repository
or any work folder. `-PtriaevumUnsignedReleaseCandidate=true` builds an unsigned candidate
(`*-release-unsigned.apk`). The audit refuses that candidate unless it is told it is auditing one
(`--allow-unsigned`). The release notes publish the signing certificate's SHA-256.

## APK audit (`tools/triaevum_release/android_release_policy.py`)

The audit refuses an APK that:
- lacks any of the six `lib/arm64-v8a` libraries, or carries another ABI
- has a library that is not AArch64, has a LOAD segment below 16 KiB alignment, or is unstripped
- carries game data (`.gar`, `.mfpj`, `.3ds`, `.cci`, `.cia`, `romfs/`, `exefs/`, …), debug or map files, keystores,
  or `local.properties`/`gradle.properties`
- contains a build-machine path (the release policy's markers)
- is debuggable, or whose application id, version or label differ from the title facts
- is unsigned, or signed by another certificate
- holds a Forge catalog whose runtime is not `lib/arm64-v8a/libTriAevum.so`, or that names the other title

The desktop policy (`platform_policy.py`) still rejects the Android target, so the Windows/Linux/macOS audits are
unchanged.

## Qualification plan (owner's device, one window)

| gate | Echoes of Time | Moonfall Curse |
|---|---|---|
| install, first launch, Forge import of the owner's ROM | pending (needs DEVICE_GRANT) | pending (needs DEVICE_GRANT) |
| produce-at-install of the dekui and Project Restoration recipes | n/a | pending (host proof first) |
| boot and deterministic scene (`--max-seconds`), SHADERC 0 / misses 0 | pending | pending |
| touch overlay, gestures, controller | pending | pending |
| lifecycle: background/resume, rotation, screen off | pending | pending |
| performance at the Enhanced for Mobile defaults, thermal gate | pending | pending |
| update over the previous version with data kept | pending | pending |
