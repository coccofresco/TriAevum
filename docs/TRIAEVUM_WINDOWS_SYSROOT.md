# Forge Windows Sysroot

## Scope

This is the R02/R07 dependency contract, not clean-machine qualification.
Forge can now consume an explicit, inventoried Windows C++ sysroot. Existing
installations without one still report `dependency_resolution=host_discovery`;
they must not be presented as hermetic releases.

`windows_sysroot.py` validates every file's size/SHA-256, rejects missing or
unindexed files, and constructs explicit MSVC, SDK, Clang resource and compiler
compatibility arguments. Developer-shell flags and include/library variables
are removed when using this contract. The object and plugin cache identities
include the verified sysroot identity; changing a header/library invalidates
the associated build identity. TLS callbacks in the title ABI are unchanged.

## Local Preparation

`import_windows_sysroot.py` copies locally available SDK material into a new
private directory. It does not authorize public redistribution of that material.

```powershell
python tools/triaevum_release/import_windows_sysroot.py `
  --msvc "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207" `
  --sdk "C:/Program Files (x86)/Windows Kits/10" --sdk-version 10.0.26100.0 `
  --clang I:/oot3dre_tools/llvm-22.1.6/lib/clang/22 `
  --output I:/oot3dre_work/triaevum-private-sysroot
```

The output must not already exist. Builders accept a `sysroot` path in their
toolchain object; otherwise they look for `sysroot/` beside clang-cl.exe. An
explicit or present-but-invalid sysroot never silently falls back to the host.
The probe accepts the same explicit parameter. The shipped r5 candidate has
not been modified and does not include this private snapshot.

## Evidence

The local snapshot contains 6,081 files, 1,116,464,323 bytes, with identity
`e7ea207892cee2249ebfe26805695c86fc92ce9a67357ae78d9dc99580ab8c86`.
MSVC 14.44.35207, Windows SDK 10.0.26100.0, Clang resources 22.

An actual compile/link/run succeeded with the explicit sysroot. The strengthened
probe covers vector allocation, span/JSON, exceptions, callback invocation,
floating point under `/fp:strict`, and TLS isolation across a joined thread.
Driver diagnostics showed system include and library search paths under the
private root. The driver initially defaulted to MSVC compatibility 19.33 when
given the neutral `msvc` directory; the contract now explicitly selects 19.44
from the versioned v14 toolset rather than relying on directory-name inference.

Full inventory hashing adds roughly 15-20 seconds to this local probe. Do not
repeat it per shard: each archive build resolves it once. The plugin pipeline
now passes its verified contract to the archive builder within the same build
operation. The contract is root-bound and not persisted between runs. Toolchain
files must remain immutable during the operation. Future acquisition should
publish immutable generations; do not replace content verification with an
unchecked persistent cache.

## Real DLL Boundary Probe

`validate_whole_aot_toolchain.py` runs the actual generator, object cache,
wrapper compiler and plugin linker against the host's support library. Its
five-instruction arithmetic/load/store/return program is synthetic, not extracted
from the game. `whole_aot_abi_probe.cpp` then loads the DLL and checks:

- ABI rejection for an unsupported version and expected dispatch entry.
- Guest register arithmetic and host-created guest-memory read/write.
- Block callback from the plugin back into the host.
- `ExecutionActive` across the real DLL TLS boundary.
- `ObservableExit` restoring the guest snapshot and leaving TLS inactive.

```powershell
python tools/triaevum_release/validate_whole_aot_toolchain.py `
  --compiler I:/oot3dre_tools/llvm-22.1.6/bin/clang-cl.exe `
  --archiver I:/oot3dre_tools/llvm-22.1.6/bin/llvm-lib.exe `
  --support I:/oot3dre_work/triaevum-direct-module-build/triaevum_title_whole_aot_support.lib `
  --include C:/vcpkg/installed/x64-windows-static/include `
  --sysroot I:/oot3dre_work/triaevum-private-sysroot `
  --output I:/oot3dre_work/triaevum-native-abi-probe
```

The local probe passed all six checks. Its actual private DLL hash is
`85d69baa913c642b5bc01d8e73c9d24a23f2b2513741580896b448f70f27d271`;
support library hash is
`5e5899a661ca7fe6e0b562989709e77a4a75b9b2ac1a5dfdebd61d9f81a45988`.
Commands, compiler/linker output and the receipt stay in the output directory.
Repeating the command exercises the real build cache, not a mocked runner.
This is stronger than the STL probe but is not full-game/clean-machine proof.
Local repeated invocation with a reused plugin took 28.05 seconds, versus
69.89 seconds before sharing the build-local verification. These are individual
measurements of this probe, not a claimed speedup for full title generation.

## Remaining Work

- Acquire the dependencies on the user's machine without requiring an existing
  Visual Studio installation; pin sources, versions and hashes.
- Review the acquisition/licensing route before including Microsoft payloads
  in any public package. Keep the private snapshot outside release allowlists.
- The full title now builds and boots locally from the user's ROM; see
  [full title proof](TRIAEVUM_FULL_TITLE_BUILD_PROOF_2026-09-05.md). This is not
  a clean-machine qualification.
- Execute the final package on Windows without installed development tools.
  Use the [clean Windows procedure](TRIAEVUM_CLEAN_WINDOWS_QUALIFICATION.md).
- Include the resulting compiler/sysroot/profile receipts in release readiness.

Microsoft's [redistribution list](https://learn.microsoft.com/en-us/visualstudio/releases/2026/redistribution)
and [licensing guidance](https://www.microsoft.com/licensing/guidance/Visual-Studio)
distinguish tool use from redistributable components. This document makes no
new claim that all headers/libraries may be redistributed. The existing project
legal approval is not treated as evidence of permission for new SDK payloads.
