# OptiScaler-DLSS5 (DLSS 5 Neural Rendering) with TriAevum

2026-10-12. [OptiScaler-DLSS5](https://github.com/Skynizz/optiscaler-dlss5)
(GPL-3.0, unofficial) runs NVIDIA's DLSS 5 Neural Rendering model
(`nvngx_dlssnr.dll`, NGX feature 18, not redistributed) over the output of a
game's DLSS. It is a user-installed mod, not part of TriAevum; nothing of it
is packaged.

## How it attaches

- It loads as `winmm.dll` beside `TriAevum.exe` (the executable imports
  `WINMM.dll`) and intercepts NGX. TriAevum's DLSS runs on Vulkan in the
  separately licensed DLSS module (`triaevum_dlss.dll`, the only binary that
  links NGX; `src/fast/dlss/LICENSE.md`); after each
  `NVSDK_NGX_VULKAN_EvaluateFeature` the mod runs the model on the same
  command buffer (`EvaluateAfterUpscaleVk`), with the colour, depth and motion
  vectors of TriAevum's DLSS parameters.
- The two NVIDIA extensions the model loads through (`VK_NVX_binary_import`,
  `VK_NVX_image_view_handle`) are already enabled by TriAevum for NGX; the
  mod adds `VK_KHR_buffer_device_address`.
- The pass runs before UI composition, so the HUD is never shown to the model.

## What TriAevum needed

The scene came out black, HUD intact, even at `TransferStrength=0`. NRI makes
every Vulkan image view usable only for its view type, so the DLSS output view
NRI hands NGX was storage-only; the mod samples that view (`gSource`, a
combined image sampler) to read the frame, which reads black, and composes the
result from that copy. `cmake/patches/oot3d_nri_storage_view_sampled.cmake`
keeps the sampled usage on a storage view whose texture is also a shader
resource (the image itself already allows it). Nothing else in TriAevum uses
the extra usage.

## Install

Beside `TriAevum.exe`, which already has `triaevum_dlss.dll` and NVIDIA's
`nvngx_dlss.dll` from the package: `OptiScaler.dll` renamed `winmm.dll`, `OptiScaler.ini`
with `[DlssNr] Enabled=true`, the mod's `nvngx.dll_dlssnr.dll` forwarder, and
`nvngx_dlssnr.dll`. In F1 set anti-aliasing to Upscaler with the DLSS provider
(the mod does nothing without the game's DLSS running). Insert opens the
OptiScaler menu (DLSS 5 tab); F6 cycles optimised / vanilla / off.

## Observations

- 2026-10-13, beta.1b candidate (DLSS module), RTX 3060, Kokiri savestate,
  3840x2160 DLSS Ultra Performance: the mod proxies the module's
  `NVSDK_NGX_VULKAN_Init_Ext2`, the DLSS feature and the model come up
  (`feature up at 3840x2160`, `running natively`). With the mod loaded the
  process ends with an access violation after NGX releases the feature at
  exit, with this runtime and with beta.1 alike; without the mod both exit
  cleanly.

- DLSS reconstructs the frame upright (below), so the model is created at the
  presented size (log: `feature up at 1280x720`) and sees the picture as the
  screen shows it. Before, it ran on the stored quarter-turned surface
  (`720x1280`).
- The output is R16G16B16A16 already tone-mapped and not flagged HDR, so the
  mod takes its display-referred path (`passthrough`).
- TriAevum's D3D12 NGX provider probe at startup also passes through the mod's
  D3D12 path once; harmless.

Lab (Kokiri savestate, warp to Hyrule Field, 1280x720, DLSS Quality): with the
patch the frame is the model's picture (cooler, more saturated grass and sky);
OptiScaler loaded with DLSS 5 off is identical to the game without it.

## Upright DLSS (2026-10-12)

PICA surfaces are stored a quarter turn from the screen, mirrored when the
display transfer flips Y (`PicaSurfaceCoordinates`). `UpscalerOrientationPass`
runs inside the upscaler pass for DLSS: its colour, depth, motion and reactive
inputs are turned upright into private images (motion vectors and the jitter
turned with them, `UprightOffset`), DLSS reconstructs into a private upright
output, and that output is turned back onto the `UpscaledColor` surface the
graph declared; nothing after the upscaler changes. The texel mapping is
`PresentationToStorage` at texel centres, for both flips, and the output turn
is its exact inverse (foundation tests). The two compute programs are in the
built-in pass catalogue. If the turn is unavailable, or with
`TRIAEVUM_DLSS_UPRIGHT=0`, DLSS reconstructs the stored surface as before;
`TRIAEVUM_DLSS_ORIENTATION` reports which on the first dispatch.

Lab, 1280x720 DLSS Quality, Hyrule Field: upright against stored differ by a
mean of 6/255 still (reconstruction and wind), against 26 and 54 for the
mirrored images; mid-pan of the free camera the edge energy is 13.03 against
12.98 (no added ghosting from the turned motion). With OptiScaler-DLSS5 the
model is created at 1280x720.
