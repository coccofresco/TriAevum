# D3D12 NGX compatibility

## Objective

Expose a normal, documented D3D12 DLSS-SR evaluation contract alongside the Vulkan/NRI renderer. This gives external NGX tooling a legitimate integration point without making the renderer depend on a private neural-rendering ABI.

## What the contract actually requires

- A D3D12 device and direct queue on the same physical adapter as Vulkan, matched by LUID.
- A regular `NVSDK_NGX_D3D12_CreateFeature` and `NVSDK_NGX_D3D12_EvaluateFeature` flow. Loading a DLL or creating a D3D12 device alone is insufficient.
- Per-frame scene color without HUD, hardware depth, dense motion vectors, output, jitter, motion-vector scale and history reset.
- Correct resource formats and dimensions. The current canonical bridge formats are `RGBA16_FLOAT` color/output, `D32_FLOAT` or `R32_FLOAT` depth, `RG16_FLOAT` motion and `R8_UNORM` reactive mask.
- Exposure or explicit NGX auto-exposure, plus correct HDR/sRGB declaration.
- GPU-side Vulkan/D3D12 ownership transfer and a shared timeline fence at the exact evaluation point. A late copy captures the previous frame; a CPU readback is not an acceptable bridge.
- The D3D12 result must be returned to the Vulkan effect graph before UI composition and present.

Authoritative references: [NVIDIA Streamline programming guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuide.md), [NVIDIA DLSS programming guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/DLSS%20Programming%20Guide.pdf), and the [official DLSS SDK](https://github.com/NVIDIA/DLSS).

Community implementations independently confirm the same ordinary DLSS contract: [DLSS5-Feeder](https://github.com/jlrouzies-fr/DLSS5-Feeder) and [dlss5-bridge](https://github.com/NIGos/dlss5-bridge). Reddit reports are useful for compatibility observations but are not treated as ABI documentation; the current [unofficial neural-rendering megathread](https://www.reddit.com/r/nvidia/comments/1w1817k/megathread_unofficial_dlss_5_neural_rendering/) consistently reports that DLL replacement alone does not activate the feature.

## Implemented state

The renderer now owns a complete opt-in D3D12 DLSR frame path rather than a startup-only probe:

- the D3D12 adapter is matched to Vulkan by LUID and wrapped by NRI;
- two persistent frame slots own shared `RGBA16_FLOAT` color/output, `R32_FLOAT` depth, `RG16_FLOAT` motion and `R8_UNORM` reactive images;
- a Vulkan compute conversion pass fills those canonical resources directly from the typed effect graph;
- a shared D3D12 fence imported as a Vulkan timeline semaphore transfers ownership in both directions;
- the Vulkan frame is split immediately before temporal upscaling, D3D12 evaluates DLSR, and the Vulkan continuation reacquires the output;
- scanout samples that returned image directly, while HUD/UI and presentation remain in the continuation after upscaling;
- resources and the DLSR feature are recreated only when dimensions, quality or scene encoding change;
- the existing Vulkan DLSR path remains the automatic fallback and default.

Set `OOT3D_GRAPHICS_DLSS=1` and `OOT3D_NGX_D3D12_PER_FRAME=1` to opt in. `OOT3D_NGX_D3D12_PROVIDER=0` disables creation of the compatibility provider entirely.

The JSON diagnostics now distinguish every frame stage: `d3d12_ngx_frame_requested`, `d3d12_ngx_frame_contract_ready`, `d3d12_ngx_frame_prepared`, `d3d12_ngx_frame_split_submitted`, `d3d12_ngx_frame_queued` and `d3d12_ngx_output_acquired`. Provider totals are exposed through `frame_bridge_ready`, `frame_dispatch_requested` and `frame_dispatch_count`; `dlss_nri_dispatched` remains the final indication that a DLSR output was selected for display.

## Validation

On the local RTX 3060 with the official NGX 310.7 runtime, a 240-frame capture produced four startup frames before temporal inputs existed, followed by `236/236` successful contract, preparation, split, queue, acquire and displayed-output events. The process continued rendering normally for the complete capture.

With the external ReShade add-on enabled, its post-swapchain hook observed the real `960x853 -> 1440x1280` frame contract. The renderer still completed `222/222` requested frames through official DLSR fallback. The add-on separately reported `feature 18 create failed with 0xbad00002`. The NGX headers classify this value as `NVSDK_NGX_Result_FAIL_PlatformError`, not `FeatureNotSupported`.

The failing local `nvngx_dlssnr.dll` reported version `310.8.0.0` but Windows Authenticode returned `HashMismatch` (SHA-256 `CEB6432F6FBDF44D886014BCD47241932BF8B67439FEEF9BBDD0961436662650`). This explains why feature creation failed before any per-frame renderer input could matter. The add-on's preceding `signed DLSSNR ... runtime initialized` message is therefore not sufficient evidence of a valid signature. The add-on itself matched the current public rolling-release payload at the time of validation.

Run `scripts/oot3d/Test-Oot3dDlss5Deployment.ps1` before add-on tests. It validates the add-on and both NGX runtimes, records the latest feature-18 failure, and can replace an invalid neural runtime only from a user-supplied, NVIDIA-signed source after exact version, hash, signer and Authenticode verification. The accepted build identity is also documented by the source-only [DLSSNR signature repair utility](https://github.com/kayle2203/dlssnr-signature-repair). The preflight never downloads, patches, re-signs or bypasses checks in proprietary binaries.

The old standalone 16x16 Vulkan/D3D12 interop probe has been removed. The startup DLSR evaluate probe remains intentionally: it rejects an unusable provider before the renderer enables the per-frame bridge.

## Remaining product work

1. Replace the environment-only opt-in with a typed renderer setting after the compatibility path has a supported deployment target.
2. Add the 240-frame official-runtime capture to GPU regression automation on an NVIDIA runner.
3. Keep validating resolution changes, camera cuts and HDR/sRGB transitions as the effect graph evolves.
4. Supply a lawfully obtained NVIDIA-signed DLSSNR runtime before repeating feature-18 validation; an altered runtime cannot validate the add-on path.

None of these items is required for an external hook to observe a standard post-swapchain D3D12 DLSR contract or for the returned official DLSR image to be consumed by the game.

## Boundary

Do not patch device checks, CUDA instructions, signatures or caller validation. RTX 3060 support for an unofficial neural-rendering binary is not guaranteed by this integration. This work provides the correct engine contract; unsupported binary modification remains outside the renderer.
