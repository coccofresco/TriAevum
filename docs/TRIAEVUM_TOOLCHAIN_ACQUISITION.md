# Windows Toolchain Acquisition

## Intended Flow

Acquire the Windows dependencies privately on the user's machine, rather than
redistributing Microsoft SDK material inside TriAevum. Reuse the existing LLVM
compiler/linker. Separate catalog pinning, payload download, extraction,
sysroot inventory, native probe and final activation.

The reference examined was [PortableMSVC](https://github.com/tgbender/portablemsvc)
at commit `76d1149870991c94ae9fda29c19507db253ca5a9` (MIT). It illustrates
package selection and MSI/CAB extraction, but its broad install also includes
compiler/debugging tools not needed by Forge. No source code from that project
has been vendored or made a runtime dependency in this tranche.

## Implemented

- `plan_windows_toolchain.py`: verifies a pinned VS catalog and selects CRT
  headers/x64 desktop libraries plus SDK headers/UCRT/x64 libraries. It records
  the SDK cabinet index for subsequent resolution through MSI Media tables.
- `toolchain_download.py`: HTTPS Microsoft-only payload URLs, size/SHA-256
  verification, content-addressed cache, per-payload kernel locks, timeout and
  atomic publication. Completed files are reused; interrupted individual
  downloads restart. No unverified partial file is activated.
- Plans explicitly require license acceptance and mark cabinet resolution as
  incomplete. Neither automatic SDK extraction nor GUI activation is claimed.
- Tests reject altered catalogs, downloads with bad hashes/sizes, untrusted
  URLs/redirects, and incomplete component selections. Suite: 93 tests.

## Current Source Inconsistency

Observed on 2026-09-05 from the official channel
`https://aka.ms/vs/17/release/channel`:

- Catalog item: `Microsoft.VisualStudio.Manifests.VisualStudio`.
- Declared bytes: `30,444,084`.
- Declared SHA-256:
  `bd98dd01efa4195cb1c11030da63b9e4a3bcec7bc406799a9db80339d6dabd79`.
- Catalog URL:
  `https://download.visualstudio.microsoft.com/download/pr/fa619120-9c0e-47e6-bfe0-3ee96fb671b2/bd98dd01efa4195cb1c11030da63b9e4a3bcec7bc406799a9db80339d6dabd79/VisualStudio.vsman`.

The URL returned valid JSON of `17,955,171` bytes with SHA-256
`3891c3018a07338b3880cbb28088bb22ef7762eb9206523655b2e3972b9d527e`,
using both PowerShell and Python urllib. HTTP Content-Length also reported
17,955,171. Therefore the planner refused the catalog. The observed hash was
not substituted for the declared trusted hash to bypass that failure.
Local inspection copy: `I:/oot3dre_tools/VisualStudio-bd98dd01.vsman`.
This is evidence of a mismatch along the retrieval path, not proof of its cause.

## MSI Dependency Resolution

`msi_metadata.py` reads the MSI `Media` table through Windows Installer's
read-only database API. It does not call installation actions or execute
package code. See [MsiOpenDatabaseW](https://learn.microsoft.com/en-us/windows/win32/api/msiquery/nf-msiquery-msiopendatabasew).
`plan_windows_toolchain.py --resolve-cache` verifies each selected MSI by size
and SHA-256 before querying it and resolves only the external CABs declared by
the pinned catalog. Embedded streams require no download. Missing cabinets,
path traversal, URL substitution and modified MSI bytes are rejected.

Local read-only proof: SDK 10.1.26100.6901 Desktop Libs x64 MSI in
`C:/ProgramData/Package Cache/{36D06095-DD2A-C8B2-1C97-352DDB5C1A74}v10.1.26100.6901/Installers/Windows SDK Desktop Libs x64-x86_en-us.msi`,
SHA-256 `920d7ca82532a50da586bd70f9cff3e03c0b37cc3b055793790a1ba8465b9e45`.
The required external cabinet is `58314d0646d7e1a25e97c902166c3155.cab`.
No package was installed or redistributed in this test.

The read-only reader also exposes the fixed `Directory`, `Component` and `File`
tables. `file_layout` joins file IDs to target-relative paths, applies target
versus source and short versus long MSI naming, rejects cycles, duplicate
case-insensitive destinations, traversal and reserved Windows names. On that
same real SDK MSI it resolves all 365 libraries below
`Windows Kits/10/Lib/10.0.26100.0/um/x64` (including `AclUI.Lib`, `advpack.Lib`
and `appmgmts.lib`). This establishes the extraction layout, not CAB extraction
or SDK installation. Suite: 107 tests.

The catalog discrepancy also reproduced with no-cache/identity headers and
the official 17.12 LTSC channel. For that channel the declared SHA was
`524d784d450775f65c0a563d65d4ce5290d3e4eee518ade757b37a105f8bba46`,
while the received file hashed to
`2b827c355ee16b892c99d87030ff0d0a46ab6ec3ca20ea6b541996e9f16c8f91`.
Inspection copy: `I:/oot3dre_tools/VisualStudio-1712-catalog.vsman`.
No download identity was weakened or repinned to the mismatching bytes.

## Remaining Acquisition Work

### CAB and MSI Extraction Implemented

`cabinet_extract.py` uses [SetupAPI cabinet callbacks](https://learn.microsoft.com/en-us/windows/win32/setupapi/creating-a-cabinet-callback-routine),
not `msiexec`. It verifies the CAB hash, selects only MSI-declared IDs, checks
sizes and writes the explicit target path. It rejects duplicate destinations,
overlong SetupAPI paths and spanned-cabinet requests instead of silently
following another file. `msi_extract.py` verifies the MSI, extracts into fresh
staging and publishes only when every expected file is present. It does not
activate a toolchain or accept license terms.

Real local proof: all 365 files (69,790,662 bytes) extracted from the SDK MSI
above. All 365 SHA-256 values match the installed SDK files. CAB SHA-256:
`41e2f49e5c0ef12454fc1ee2a8b031f1e271c4d172afd11b71481db83216cdec`.
Evidence: `I:/oot3dre_work/sdk-cab-proof/extraction.json` and
`I:/oot3dre_work/sdk-msi-proof/extraction.json`. These are private local
verification artifacts, not approved redistributable payloads.
Native tests create a synthetic CAB with Windows MakeCab and verify extraction,
bad hashes/sizes, traversal, missing IDs and non-publication on incomplete MSI
coverage. Suite: 108 tests (native CAB test skipped outside Windows).

### VSIX Extraction Implemented

`vsix_extract.py` extracts a hash-verified VSIX as a ZIP container, preserving
package metadata. It does not register extensions or run any extracted binary.
Fresh staging is published only after complete extraction/CRC checking;
traversal, Windows reserved names, case-insensitive duplicates, links,
encryption and excessive declared sizes/counts are rejected.

Local format proof used the already-installed package
`C:/ProgramData/Microsoft/VisualStudio/Packages/Microsoft.VisualCpp.Tools.Common.UtilsPrereq,version=16.11.35704.18/payload.vsix`.
SHA-256: `9af71e606e22d3e8a777f95f2ac1c101152604341017a794c975468fc1fc21d4`.
Extracted 32 files, 6,975,026 bytes; receipt at
`I:/oot3dre_work/sdk-vsix-proof/extraction.json`. This validates extraction of a
real VSIX, not qualification of this old package as the selected Forge CRT.
The package is not added to any public distribution.
Suite: 111 tests, including synthetic VSIX extraction and negative cases.

### Remaining End-to-End Work

Obtain a consistent official catalog/payload identity and qualify the resolver
on the downloaded MSI set. Connect verified SDK/MSVC tree
assembly to the acquired packages, preserve license material and require applicable
license acceptance before installation. Verify the resulting sysroot with the
real DLL probe before wiring GUI acquisition. Do not execute an installer just
to discover its contents. Continue other report items independently while this
source is unqualified; do not declare the entire report blocked by this alone.

### Component Assembly and Native Proof

`assemble_windows_sysroot.py` merges explicit component trees into a new private
sysroot. Identical duplicate destinations are shared; conflicting contents,
links and incomplete inventories are rejected before publication. The local
snapshot importer uses this same assembler, avoiding a second inventory path.

The real assembly exposed a missing acquisition dependency: Desktop Libs does
not contain `kernel32.lib`. The plan now requires `Windows SDK for Windows Store
Apps Libs-x86_en-us.msi` as well. The installed copy under package-cache identity
`{F8FD4844-9C5B-6AF9-0899-6E2B8574D4A1}v10.1.26100.6901` extracted 352 files,
164,251,112 bytes into `I:/oot3dre_work/sdk-base-libs-proof`.

Both extracted UM library trees, combined with the previous private headers,
CRT and Clang snapshot, produced `I:/oot3dre_work/triaevum-assembled-sysroot`.
Its inventory identity matches the previous verified snapshot:
`e7ea207892cee2249ebfe26805695c86fc92ce9a67357ae78d9dc99580ab8c86`.
The real native ABI probe passed all six checks in 36.30 seconds, producing
plugin SHA `85d69baa913c642b5bc01d8e73c9d24a23f2b2513741580896b448f70f27d271`.
Receipt: `I:/oot3dre_work/triaevum-assembled-sysroot-probe`.
Suite: 113 tests. This is a synthetic ABI proof with locally acquired inputs,
not a complete title build or clean-machine acquisition qualification.

### Acquisition Coordinator

`acquire_windows_components.py` connects verified payload download, MSI CAB
resolution and extraction. A caller must confirm applicable license acceptance
before any download. It re-derives CAB dependencies from verified MSI files,
retains package metadata and emits `components.json` containing the resolved
plan and per-file extraction hashes. Progress callbacks are available for Forge.
No installer runs and no compiler or sysroot is activated by this command.

```powershell
python tools/triaevum_release/acquire_windows_components.py --plan PRIVATE_PLAN.json --cache PRIVATE_CACHE --output NEW_PRIVATE_COMPONENTS --accept-microsoft-licenses
```

Use only an independently verified acquisition plan; this command does not
establish trust in a catalog or substitute for reviewing its license terms.
Downloads already complete and verified remain reusable after failure; failed
component extraction is not published. Existing output is never overwritten.

Real cached VSIX proof: `I:/oot3dre_work/triaevum-component-acquisition-proof`,
32 files extracted with the coordinator. This used the local UtilsPrereq
package documented above and a cache-only fixture descriptor, not a network
download or qualification of the selected CRT. Suite: 115 tests, all passed
with CMake available, including consent-before-download and failure cleanup.
Remaining: qualified remote catalog, selected-package layout adapter and license
presentation in Forge, then full private sysroot assembly/probe and clean-PC test.

### Real Selected CRT Acquisition

Installed VS `_package.json` records supplied pinned identities for CRT Headers
14.44.35220 and CRT x64 Desktop 14.44.35221. Headers declares 2,128,977 bytes,
but the download has 2,116,223 bytes and matches the pinned SHA-256 exactly:
`852382a9aa73502b7849c1bcadfb603ba7175c4e8b60e6aba03c7de711d4ece5`.
Downloader and MSI resolver now treat size as an upper bound, still requiring
the exact hash. Shorter files with another hash and overruns remain rejected.
No hash was repinned. Errors report expected/actual hashes and sizes.

The coordinator downloaded and extracted both real CRT packages: 324 header
files (15,039,136 bytes) and 45 library files (214,928,983 bytes), including
package metadata. Library archive SHA:
`eb66efac9a8e7ecb70f10eedba9ba21c816a9e69ea3a6e0ea89d8323fc7eea5b`.
Receipt: `I:/oot3dre_work/triaevum-crt-components-proof/components.json`.
Toolset path: `Contents/VC/Tools/MSVC/14.44.35207`; package servicing versions
are not directory versions. Suite: 116 tests passed. This is selected-CRT
acquisition from installed metadata, not resolution of the catalog hash mismatch.

### Verified Component Layout Consumer

`windows_component_layout.py` verifies every receipt-listed file before mapping
MSI SDK and VSIX toolset paths to the Forge sysroot. It rejects changed, extra,
linked, escaped or unsupported component layouts. Toolset versions are explicit;
it does not confuse package servicing versions with the directory version or
search the host for a fallback. Package metadata remains in the component tree.

```powershell
python tools/triaevum_release/windows_component_layout.py --components PRIVATE_COMPONENTS --msvc-version 14.44.35207 --sdk-version 10.0.26100.0 --clang-resource PRIVATE_CLANG_RESOURCE --clang-version 22 --output NEW_PRIVATE_SYSROOT
```

Multiple `--components` inputs are supported. Complete assembly verifies required
headers/libraries before publication, and refuses an existing output.
Real test combined the acquired CRT trees with the previously verified SDK/Clang
trees (SDK headers still originate from the local snapshot, not remote acquisition).
Result: `I:/oot3dre_work/triaevum-acquired-crt-sysroot`, 5,891 files, 804,071,496 bytes,
identity `e30c9dd040e0b325e46d67ac5bc1a544ad6e7e3fded263f26aacea44ca030b19`.
The native ABI probe passed all six checks in 38.44 seconds and produced the
same synthetic plugin SHA as before. Receipt:
`I:/oot3dre_work/triaevum-acquired-crt-probe`. Suite: 118 tests passed.
This is not yet a clean-machine or complete-title qualification.

### Complete CRT/SDK Acquisition from Installed Metadata

`import_local_vs_plan.py` bootstraps the existing planner from explicit installed
VS `_package.json` files. It records each source path/hash and labels the result
`installed_visual_studio_private_bootstrap`; it is a developer verification tool,
not a replacement for a clean-PC catalog resolver.
Plan: `I:/oot3dre_work/triaevum-local-sdk-plan.json`, using the two CRT records
above and `Win11SDK_10.0.26100,version=10.0.26100.13,productarch=neutral`.

All 11 selected CRT/SDK components were downloaded with pinned hashes and
extracted successfully into `I:/oot3dre_work/triaevum-full-components`.
No installer ran. ARM64 header packages are excluded from this x64 plan;
the x86-named shared headers remain required. The initial all-Headers selector
incorrectly included an empty ARM64 MSI and has been corrected.

One directory publication returned Windows access denied. MSI publication now
uses a bounded retry for Windows errors 5/32/33 (at most 1.5 seconds total delay),
never overwriting existing output or hiding permanent failures. Suite: 121 tests.
This qualifies component acquisition from local package metadata, not the remote
catalog resolver, GUI, complete-title compilation or a machine without VS.

### Complete Acquired Sysroot Native Proof

The complete component set assembled successfully into
`I:/oot3dre_work/triaevum-fully-acquired-sysroot`: 5,888 files, 803,936,290 bytes,
identity `3ab0946f30d5d528336687428cd0e9cc24ed7d733e88ec56ee28804ca16c160c`.
All CRT/SDK inputs now come from the verified packages; Clang resource headers
come from the LLVM distribution. The native probe passed all six checks in
36.81 seconds with the same synthetic plugin SHA as previous proofs.
Receipt: `I:/oot3dre_work/triaevum-fully-acquired-probe/receipt.json`.

`prepare_windows_toolchain.py` now coordinates acquisition, verified layout,
assembly and the native probe before publishing a new private generation.
The probe identity must match the assembled sysroot. Failures leave existing
generations untouched; diagnostics remain outside staging. Complete component
trees are retained alongside the sysroot to preserve package/license metadata.
This costs additional private disk space and is not a public redistribution.
The orchestration is unit-tested for failed/mismatching proof and non-publication;
its complete command has not yet been qualified end to end. GUI wiring and a
clean-machine catalog source still remain open.

### Coordinator End-to-End Proof

The complete `prepare_windows_toolchain.py` command succeeded with the private
local-metadata plan, verified download cache and LLVM 22.1.6. It reacquired/verified
all components, extracted, assembled, compiled and executed the ABI probe, then
published `I:/oot3dre_work/triaevum-toolchain-generation-proof` with status
`native_probe_passed`. Diagnostics are retained at
`I:/oot3dre_work/triaevum-toolchain-generation-diagnostics`; the generation includes
`toolchain.json`, `sysroot/` and original `components/`.
This supersedes the earlier unqualified-coordinator note, not the clean-PC or
full-title limitations. The invocation used existing pinned metadata and cache;
it is not a cold-download timing measurement.

Forge now accepts an explicit sysroot in `build_private_title`,
`build_private_whole_aot`, CLI `build-title --sysroot` and `InstallRequest`.
The GUI worker sends the same path to preflight and compilation (tested).
Visual acquisition/consent and automatic generation selection still remain open.

### Frozen Forge Probe

`TriAevumForge.exe verify-toolchain` accepts `--compiler`, `--archiver`,
`--support`, `--include`, `--sysroot`, `--output` and runs the real synthetic ABI
probe. Its C++ source is bundled, and resource lookup uses the PyInstaller
distribution root. This prevents a source-only success from masking a missing
file in the executable release.
Real frozen build: `I:/oot3dre_work/triaevum-forge-toolchain-proof/TriAevumForge.exe`.
Run from that directory with the completely acquired sysroot: all six checks
passed in 44.31 seconds, same plugin SHA as previous runs. Evidence:
`I:/oot3dre_work/triaevum-frozen-toolchain-probe/receipt.json`.
This proof still supplies private LLVM/support/include/sysroot paths explicitly;
it does not establish clean-machine acquisition or full-title readiness.

### GUI Setup Contract

Forge now recognizes `forge/toolchain-setup.json` with format
`triaevum_toolchain_setup_v1`. Required fields: `plan`, `plan_sha256`, `license`,
`license_sha256`, `clang_resource`, `clang_version`, `msvc_version`, `sdk_version`.
Paths are relative to this descriptor's directory and cannot escape it.
Plan/license contents must match their pinned hashes. Version strings are numeric.

When no private generation exists, the GUI presents the complete supplied
license text in a scrollable modal with explicit Accept/Cancel. Acceptance is
bound to the descriptor hash; the worker reloads the descriptor and checks that
identity before any acquisition. It prepares at `<data-root>/toolchain`, then
selects the proven sysroot for the title build. Existing generations are reused.
No new ROM/support input fields are added. Missing descriptors retain the old
path; malformed present descriptors fail, rather than inventing download sources.

Tests cover pinned license/plan, consent rejection, setup routing and worker
propagation. No actual setup descriptor/license payload has yet been added to
the public package. This code therefore does not make the current candidate
automatically download dependencies; a reviewed descriptor and frozen GUI
end-to-end qualification still remain required.

The release preparer now includes all 300 LLVM 22 resource header/wrapper files
at `forge/clang/include`, using an explicit dedicated allowlist role. A setup
descriptor beside them can use `clang_resource: "clang"`. The previous package
included the builtins library only; that was insufficient for clean-PC sysroot
assembly. Existing LLVM notices remain required. No Microsoft SDK files are
added by this change.

License review sources checked for setup preparation:
[Microsoft license directory](https://visualstudio.microsoft.com/license-terms/)
and [Visual Studio licensing guidance](https://www.microsoft.com/licensing/guidance/Visual-Studio).
These are reference entry points, not a substitute license for the selected
packages. A public setup must present the applicable actual terms, not a generic
copyright notice or an assertion that every end user's use is licensed.
