# Post-1c Commit Inventory

Baseline `v0.6.0-alpha.1c`; audited snapshot `5cff35e`. 87 commits.

This is a complete chronological inventory, not a per-commit claim of runtime
verification. Domain labels derive from changed paths and can overlap.
See [the qualification report](TRIAEVUM_POST_1C_REGRESSION_AUDIT.md) for
tested behavior, deployment discrepancies and remaining gaps.

| Commit | Change | Affected Domains |
| --- | --- | --- |
| `5008724` | port: build native Linux NRI runtime and validate module services | Distribution |
| `b58c6ae` | port: compile verified title as ELF and confirm native Linux title boot | Distribution, Title/AOT |
| `977e0c6` | diagnostics: isolate Linux grass stalls and Windows SSSR material coverage | Distribution, Grass, Title/AOT |
| `80a9448` | Port Forge installer to Linux with native GUI and ELF catalog support | Distribution, Title/AOT |
| `0d77f58` | Qualify Linux Forge ROM installation and native game boot | Distribution, UI |
| `c5d8534` | Publish required UI contract and reject incomplete source packages | Distribution, Shaders/cache, UI |
| `97fa817` | Record successful GCC build from the repaired public source archive | Docs/build |
| `1813090` | Build Linux product in Steam Runtime SDK and diagnose DPMS launch stalls | Distribution, Title/AOT |
| `7c3ffda` | Add reversible controller swap coverage and platform-bound release validation | Distribution, Input/language, Title/AOT, UI |
| `f1c05a8` | Adapt Linux AOT tooling and isolated loader tests from PR #6 | Distribution, Title/AOT |
| `fae0757` | Carry native targets through title publishing and installation migration | Distribution, Title/AOT |
| `6f07726` | Remove unsupported Windows LLD export suppression option | Distribution, Title/AOT |
| `bde0f02` | Define portable desktop releases and audited platform archives | Distribution |
| `ab43d4e` | Adopt unified Linux Flatpak delivery and record host qualification | Docs/build |
| `8cbba28` | Add Flatpak Forge storage, portal selection and shared launcher | Distribution, Title/AOT |
| `170382f` | Harden Escape capture and Linux Vulkan presentation | Distribution, Input/language, Renderer/window |
| `5fe39f4` | Isolate Vulkan present dispatch for native Wayland diagnosis | Distribution, Renderer/window |
| `76d7e44` | Use ordered shared-queue Vulkan presentation on native Wayland | Distribution, Renderer/window |
| `87603fa` | Add Linux-hosted Android ARM64 foundation and shared F2 policy | Android, Distribution, Renderer/window, Title/AOT |
| `1e89241` | Add isolated scrcpy device capture workflow and local diagnostics | Android |
| `2640d58` | Cross-build and link the shared PICA Vulkan renderer for Android | Android, Renderer/window |
| `e05eb0a` | Import isolated Azahar Android controls with native 3DS input adapter | Android, Input/language |
| `fa7da7b` | Distinguish Android module ABI and record device qualification scope | Android |
| `2a36244` | Keep mechanical overlay imports whitespace-clean and reproducible | Android, Input/language |
| `5035004` | Run native Android title through SDL and Vulkan/NRI; preserve pipeline caches | Android, Renderer/window, Shaders/cache |
| `777d39a` | Avoid swapchain recreation for repeated logical framebuffer requests | Android, Renderer/window |
| `cf3d69e` | fix(vulkan): retain scene pipelines across unchanged Android surfaces | Android, Renderer/window |
| `26e7247` | feat(android): cap native render surface at 720p without aspect distortion | Android, Renderer/window |
| `42e0948` | feat(forge): prepare portable PICA shader seeds from transferable caches | Distribution, Shaders/cache, Title/AOT |
| `49f7689` | test(shaders): inspect and validate MMJ cache corpora without changing native seeds | Shaders/cache |
| `8af5834` | Collect native scenario shader seeds and recover reusable PICA pipeline inventories | Shaders/cache, Title/AOT |
| `17d6566` | Expand shader scenario coverage and add repeatable temporal sampling | Shaders/cache, Title/AOT |
| `d35277c` | Document complete scenario shader corpus and export compact recapture selection | Shaders/cache, Title/AOT |
| `eed1a02` | Add shared headless NRI pipeline preparation and optional Forge host | Distribution, Renderer/window, Shaders/cache, Title/AOT |
| `9920fc5` | fix(renderer): qualify prepared NRI caches against live Vulkan | Distribution, Renderer/window, Shaders/cache |
| `dd7539e` | fix(ui): retain display rejection feedback from PR #14 | Distribution, Renderer/window |
| `5c6d93f` | docs: review six community PRs against shared platform architecture | Docs/build |
| `7617c8a` | Wire product TopScreen item hooks and seed isolated native-save probes | Distribution, Input/language, Title/AOT, UI |
| `3ebef38` | Fix TopScreen native-update input cadence and observable item assignment | Distribution, Input/language, UI |
| `706a046` | Restore TopScreen ocarina guide through its native UI owner | Input/language, UI |
| `aa9fafb` | docs: lead README with TopScreen and donor credits | Docs/build |
| `cb21d7f` | Render TopScreen localized song names through native text services | UI |
| `09f6202` | Integrate PR readback fixes and NRI outline pipeline preparation | Renderer/window, Shaders/cache |
| `298a9a1` | Harden persistent SPIR-V reuse in shared renderer modules | Renderer/window, Shaders/cache |
| `1d802f5` | Connect Forge shader preparation to persistent runtime caches | Distribution, Shaders/cache, Title/AOT |
| `15cb6fb` | Prepare shared scanout shaders in Forge and audit uncached renderer passes | Distribution, Renderer/window, Shaders/cache |
| `a714dcb` | Prepare NRI pass shaders in Forge and separate capture from pacing probes | Distribution, Grass, Renderer/window, Shaders/cache, Title/AOT |
| `b86951b` | android: carry prepared shaders and qualify Adreno cache reuse | Android, Renderer/window, Shaders/cache |
| `d1f57f9` | Fix save deletion, gyro memory layout and Forge loader isolation | Distribution, Shaders/cache |
| `5694e7d` | Verify TopScreen item use through native gameplay probes | Distribution, Input/language, UI |
| `c7f9686` | Preserve double VFP memory transfers and restore native gyro aiming | Title/AOT |
| `ff444a2` | Document Windows product qualification for issue fixes | Docs/build |
| `d941550` | Redesign F1 controls with stable device-focused bindings | Distribution, Input/language, UI |
| `79d05c1` | Fix mouse recapture and add direct control binding capture | Distribution, Input/language |
| `23e44c9` | Correct virtual aiming axes and preserve coherent motion gravity | Input/language |
| `421ec8e` | Fix live display resizing, fractional render scales and fullscreen confirmation | Distribution, Renderer/window |
| `ac77f6c` | Add ROM-derived game language selection to Forge and F1 | Distribution, Input/language |
| `fa86170` | Distribute renderer shader preparation and verify PR15 admission | Distribution, Shaders/cache, Title/AOT |
| `131076d` | Fix TopScreen ocarina guide and native mapped item actions | Input/language, Renderer/window, UI |
| `3c937dd` | Restore TopScreen cycle badges and document practice-save qualification | Renderer/window, UI |
| `a9b9506` | Preserve native HUD opacity in TopScreen mapped item icons | Renderer/window, UI |
| `a52fcf4` | Fix TopScreen unknown-song marker native atlas binding | Input/language, UI |
| `94cb1c1` | test(topscreen): qualify native first-time Storms learning | Input/language, UI |
| `5c47c67` | fix(forge): preserve external Tk data and qualify Linux alignment | Distribution |
| `883904a` | fix(topscreen): decouple HUD visibility from host callback cadence | Input/language, UI |
| `d2f26dc` | Fix TopScreen contextual copy visibility against 2.1.1 predicates | UI |
| `93d0fce` | Fix mounted TopScreen action and minimap relocation and stamina pivot | UI |
| `0079a43` | Fix TopScreen ammo copy anchors and restore grass masks from savestates | Renderer/window, UI |
| `4bd045f` | Document terrain-coupled grass research and bounded patch strategy | Grass |
| `7005c3f` | Use shared 4x4 color grids with two-nearest grass sampling | Grass, Shaders/cache |
| `54383b7` | Document native terrain vertex-lighting reuse boundaries for grass | Grass |
| `9b7c3b3` | Share toon surface response and frame settings with grass | Grass, Renderer/window, Shaders/cache |
| `46ac1af` | renderer: inherit native terrain vertex lighting in Grass | Grass, Renderer/window, Shaders/cache |
| `42fa0da` | renderer: preserve native TEV scales in Grass terrain response | Grass, Shaders/cache |
| `a2248f6` | renderer: keep Grass terrain response free of grazing rim highlights | Grass, Shaders/cache |
| `ceef78b` | feat(grass): retain nearby toon rim with configurable distance fade | Distribution, Grass, Shaders/cache |
| `185cfe6` | chore(grass): promote current user settings to product defaults | Distribution, Grass |
| `e208a9b` | perf(grass): reduce redundant shading and adopt tested 1024-density preset | Distribution, Grass, Shaders/cache |
| `5d339a5` | Measure Grass density compromises on wide intro views | Distribution, Grass |
| `bf02399` | Evaluate compact Grass tufts and repeatable midrange clusters | Grass |
| `c3f7f22` | Add bounded 50-root Grass cluster preparation and shared topology | Grass |
| `c5749ae` | Add experimental grass cluster draws and record measured limits | Distribution, Grass, Shaders/cache |
| `cc5ceb3` | Select grass by immutable clusters with far LOD and density bounds | Grass, Shaders/cache |
| `4ea21b9` | Add mask-certified adaptive grass clusters and measured comparison | Distribution, Grass |
| `7368bb5` | Support mask-certified grass clusters up to 10000 roots and document coverage-first tests | Grass |
| `d3ffc1f` | Optimize adaptive grass locality and restore distant coverage within historical time target | Distribution, Grass, Shaders/cache |
| `5cff35e` | Qualify Linux adaptive Grass runtime and installed Flatpak alignment | Distribution, Grass |
