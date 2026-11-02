# macOS Forge publisher workflow

The Apple Silicon publisher follows the
[precompiled release contract](TRIAEVUM_PRECOMPILED_RELEASE.md). It reuses the
Android publisher's role selection and translation provenance checks; it does
not regenerate translated code. The output is an unpublished candidate, not
a release-readiness claim.

## Build and package inputs

Use the pinned Rust toolchain and run native Python oracle tests with
`TRIAEVUM_FORGE_PYTHON` set to an absolute Python 3.11+ interpreter that has the
oracle dependencies. Build `forge-desktop` and `forge-cli` on the Mac through
`tools/triaevum_release/rust_forge_build.py --target aarch64-apple-darwin`.
The publisher remaps source, Cargo and toolchain paths and emits the build
receipt bound to Cargo.lock, the toolchain pin and crate notices.

The runtime and title are built separately with `ports/macos/runtime` and
`ports/macos/title`, arm64, deployment target 14.0. The title reuses the verified
Android/b12 translated source inventory at `-O1 -ffp-model=strict -ffp-contract=off`.
Both entry points force `TRIAEVUM_TITLES=oot3d`, including existing CMake caches;
other title adapters cannot enter the public Mac build through shared defaults.
Do not rebuild translated sources on an end user's machine. Build the tiny
bundle entry on the publisher with:

```sh
xcrun clang -arch arm64 -mmacosx-version-min=14.0 -O2 \
  tools/triaevum_release/macos_launcher.c -o "$BUILD/TriAevumLauncher"
```

Both Mac CMake entry points remap source, build and home prefixes for file,
macro and debug paths before adding native targets and dependencies. The Mac
bundle audit scans every native binary, including the runtime, title, helpers,
launcher and Frameworks, for embedded build-machine paths. Failures identify
the packaged binary without reproducing a matched host path. Prebuilt donor
libraries must pass this same gate; the publisher does not rewrite their data.

`ports/macos/package_app.sh --help` lists explicit input paths. Supply:

- `--reference`: complete audited desktop reference package, including its
  recipes, qualified shared SPIR-V corpus and translated-source archives;
- `--runtime`, `--native-module`, `--title`, `--shader-compiler`,
  `--pipeline-helper`, `--launcher`: actual native outputs, never stubs;
- `--sdl`, `--vulkan-loader`, `--moltenvk`: licensed arm64 libraries;
- `--forge-bundle`: the Rust publisher output and its receipt;
- `--source-archive`, `--source-commit`: exact public corresponding source for
  the native platform build, including all current patches, not a stale base;
- `--moltenvk-license`, `--vulkan-license`: complete texts from the exact
  dependency distributions used, matching the reviewed notices in the verified
  corresponding source (the publisher rejects arbitrary replacement text);
- `--output`: a new directory; `--version`: the intended candidate version.

Export corresponding source with `source_archive.py --target aarch64-apple-darwin
--forge-profile rust` from the reviewed public source repository. The Mac
export filters private-title paths and refuses remaining private-title text,
including mixed notices, rather than silently removing credits. The publisher
verifies the source inventory and takes its notices and installation document
from that archive, never from its own checkout. The standalone Mac package
audit checks both paths and contents, including uncompressed source members.
All license payloads and the contribution, audio provenance, approval record
and precompiled release documents also come from that verified archive.
The macOS audit pins the upstream loader/MoltenVK license texts and embedded
dependency notices listed in `LICENSES/MacOS-donor-notices.json`. Review the
actual dependency distributions against these snapshots when updating inputs;
update the notices and audit pins together if their licensing changes.

Use `TRIAEVUM_PUBLISHER_PYTHON` to select the publisher interpreter. The tool
refuses to overwrite existing outputs and leaves failed staging for diagnosis. It never
invokes a general compiler, downloads anything, or publishes an artifact.
The owner requires only signing identity `-` (ad-hoc). Apple Developer
identity signing and notarization are excluded from this release; they are
not pending release steps. No Apple account is used.

## Development artifact retention

Keep one current build per required configuration. Once a replacement is
validated, retire the superseded outputs and completed probe binaries,
including copied private probe apps. Failed staging is temporary: after
diagnosing the failure and recording a small verification receipt, delete its
disposable generated payload. Do not archive obsolete output instead of
deleting it. A private signing probe is not a frozen product comparison baseline.

Before another large build, generation or capture batch, check free space and
remove the previous batch's disposable outputs. Preserve corresponding source
and IR, current native objects, small verification receipts and the frozen
product comparison baseline. Delete disposable assembly and detailed compiler
reports after their validated consumers finish; retain accepted comparison
captures, not every experimental frame.

Preview cleanup with `tools/oot3d/azahar_aot/prune_workspace.py` for the
workspaces it manages. For separate macOS publisher/probe directories, make an
explicit inventory of the exact paths, contents and sizes before deletion.
Review the manifest, verify resolved paths stay under the intended workspace,
and protect live build/source roots and other agents' work before applying it.
Never target user inputs, saves, states, settings or mod sources. Retain the
small cleanup receipt with the qualification evidence; the publisher does not
delete arbitrary caller-owned output directories on failure.

## Bundle and audit

```text
TriAevum.app/Contents/
  Info.plist
  MacOS/
    TriAevumLauncher             # bundle entry, execs Forge
  Resources/
    TriAevumForge                # desktop
    TriAevum                    # neutral runtime
    forge/                      # CLI, neutral module and shader helpers
    titles/<execution-id>/triaevum_title_aot.dylib
    recipes/ resources/ LICENSES/ source/ docs/
    release-manifest.json
    config/vulkan/MoltenVK_icd.json
  Frameworks/
    libSDL2-2.0.0.dylib libvulkan.1.dylib libMoltenVK.dylib
  _CodeSignature/CodeResources
```

The catalog root is `Contents/Resources`. macOS code signing rejects arbitrary
data directories in `Contents/MacOS`; that directory contains only the native
launcher. The pre-signed Forge/runtime/helper code lives with its catalog in
Resources and is also covered by the app's resource seal. Native install names are rewritten to
`@rpath` with a Frameworks path relative to each binary, then signed inside
out. Forge routes its native children to the bundled relative MoltenVK ICD;
no developer `DYLD_*` variable is required. The title remains in the signed
bundle; the user activation lives in Application Support or the explicit
development override.

All release manifest, runtime, title and Forge hashes describe their final signed files. The
dedicated launcher uses an explicit `macho_code_without_bundle_signature_v1`
digest: bundle signing changes its signature after sealing the manifest, so
hashing that signature in the sealed manifest would be circular. The digest
covers the complete executable up to the final signature, normalizing only
the signature size and `__LINKEDIT` sizes. Its load-command/signature bounds
are checked, and macOS `codesign --verify --deep --strict` verifies the seal.
This code digest is restricted to the exact launcher path in the separate
outer-bundle receipt; the common release manifest uses whole-file hashes.

`audit_release.py TriAevum.app/Contents/Resources` applies the shared no-content,
no-SDK, source and title-catalog gates, then audits the entire outer bundle.
Private title directories are excluded from both package files and all source
archives, including case and path-separator variants. This applies to a
standalone audit as well as the publisher's early corresponding-source check.
Each Mach-O must be thin arm64 with a declared macOS floor no newer than the
app's minimum; foreign binaries, missing dependencies and absolute build
rpaths fail. The outer receipt names exactly Info.plist, the three libraries
and the launcher; the catalog-root manifest binds the relative ICD. The only
generated signing file is CodeResources.
Unknown outer files and symlinks fail. The archive is made only after audit.

The app minimum is a binary compatibility floor. It must not be described as
tested on that OS until a physical or appropriate clean-machine test exists.
Ad-hoc integrity verification is distinct from Gatekeeper approval and the
project's Ed25519 publisher-signature policy.

## Qualification

Qualification order is developer launch to gameplay first, then a fresh
Forge-installed package. The installed run must use the exact catalogued
runtime/module, private ROM import, generated launch profile and local device
preparation. Repeat installation and compare saved settings/save sentinels.
Test again without developer Vulkan variables, and verify bundle signatures
and file inventories before and after installation. Capture rgb8 framebuffer
evidence and review it as JPEG; record input/audio/Retina/frame-pacing findings
separately from installation. A successful build is not gameplay proof.

Do not change global Gatekeeper settings or register/approve an app during
remote qualification. An ad-hoc candidate can fail Gatekeeper assessment even
with a valid seal; record that result separately. Follow the
[player document](TRIAEVUM_MACOS_INSTALL.md) for the intended manual flow.
