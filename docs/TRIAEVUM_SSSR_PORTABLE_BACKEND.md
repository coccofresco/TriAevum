# Portable FidelityFX SSSR

## Boundary

Keep the existing NRI SSSR pass and native scene inputs. Platform differences
belong to the FidelityFX build/host adapter, not gameplay, materials or UI.
Linux can consume the same Vulkan shader permutations as Windows; it must not
run the Windows shader compiler during installation or gameplay.

Build `oot3d_ffx_sssr_shaders` on Windows first, then export its generated
headers with `tools/triaevum_release/ffx_shader_bundle.py export --sdk SDK
--generated GENERATED --bundle OUTPUT`. Configure the Linux build with
`THREE_DS_RECOMP_FFX_SHADER_BUNDLE=OUTPUT`, or set `TRIAEVUM_FFX_SHADER_BUNDLE`
when using `scripts/build-linux-runtime.sh`. The bundle is a developer/package
build input, not a per-device pipeline cache. Preserve the FidelityFX donor
notices when distributing generated shader code.

Import verifies every header and the shader sources/callback overlay before
writing anything. It rejects changed inputs and preserves unchanged output
timestamps. These hashes detect mismatches; they are not independent proof
that the publisher compiled those sources correctly. Never export stale build
outputs as a new bundle after changing compiler flags or shader recipes.

## Host Corrections

- SDK 1.1.4 assumes Windows secure-string helpers: a target-private POSIX
  adapter supplies the required bounded operations.
- Opaque SSSR/denoiser context budgets assume 16-bit `wchar_t`. POSIX headers
  use a conservative width-scaled budget; donor private-size assertions remain
  enabled. The overlay propagates to consumers so allocation and use agree.
- The Vulkan backend's `EffectContext` is `alignas(32)`, but its scratch layout
  concatenated arrays with only four-byte rounding. Reserve up to 31 bytes
  extra and align the actual pointer before assigning the context array.
  A checked, idempotent CMake patch applies this shared correction on both OSes.

## Evidence (2026-09-12)

Before the alignment fix, optimized Linux cold runs crashed in
`CreateBackendContextVK`: GDB identified an aligned SIMD store to an address
eight bytes past a 16-byte boundary. A sanitizer run had hidden this alignment
failure; it was not sufficient qualification.

After correction, the portable SDK Release build (sanitizer disabled) completed
two 720-frame runs: 663/662 SSSR dispatches, zero fallbacks, 660/659 valid-history
dispatches, exit 0. Framebuffer captures were produced and one inspected.
This is execution evidence, not a visual equivalence or performance benchmark:
capture, diagnostics and interpolation were enabled, and Vulkan validation
layers were not enabled. A diagnostic material selection exercised SSSR.

Six bundle/alignment tests pass on Windows and Linux. They cover import
integrity, line endings, no-op timestamps, traversal rejection, donor patch
idempotence/contract drift, and padding bounds for all pointer offsets.
The revised Windows backend binary still requires rebuilding and runtime
qualification; its build drive was full during this tranche.

Debug configurations and other GPUs remain unqualified. Keep the prior
installed candidate available until package-update and gameplay checks pass.

The candidate Flatpak updated its installed receipt and booted through normal
Forge. A 40-second external process-group termination subsequently logged
allocator corruption; do not confuse the harness's SIGTERM status with a clean
runtime exit. A follow-up using the candidate executable, the actual user
configuration and `--max-seconds 35` exited 0. It reused all 23 pass modules and
140 PICA modules without compilation. This narrows the unresolved case to the
package/external-shutdown conditions, but does not prove its root cause or
exclude timing-dependent corruption. The previous Flatpak was restored.

Follow-up: the shutdown fault was reproduced under Flatpak GDB and traced to
the donor's signal-time `exit()`, not SSSR. The corrected signal path now exits
normally in the same test. See `TRIAEVUM_DESKTOP_SHUTDOWN.md`; this supersedes
the earlier unlocalized shutdown observation, not the remaining Windows/GPU
qualification requirements.
