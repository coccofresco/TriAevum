# Direct AOT release vertical

> Historical ABI-v1 evidence. Normal Forge setup now uses the direct
> generated-C++ whole-AOT ABI-v2 path documented in
> `TRIAEVUM_FORGE_WHOLE_AOT_V2.md`; the packed-operation backend below is not
> performance-qualified for play.

## Product path

The installed Forge verifies the user's supported dump, derives and audits the
structural program, then compiles it directly to a private x86-64 title AOT
companion with LLVM. Generated C++ and the historical monolithic whole-AOT
executable are not part of this path.

The public `forge/oot3d_game_module.dll` is a title-neutral process and service
adapter. TAM format 1.1 stores that generic module, the private title companion
and metadata binding their complete SHA-256 identities. The loader extracts
both into one content-addressed bundle, verifies every byte and loads the title
companion before resolving the generic module import. The runtime still sees
only the stable TriAevum module and host-service ABIs.

## Complete-title evidence

The supported OoT3D revision was built on 2026-09-02 with the packaged backend
contract:

| Measurement | Result |
| --- | ---: |
| Selected title functions | 12,419 |
| Native dispatch entries | 161,332 |
| LLVM object shards | 129 |
| Private AOT companion size | 24,982,016 bytes |
| Cold direct build | 36.44 seconds |
| Identical cached rebuild | 0.13 seconds |
| Companion SHA-256 | `4cb375065988f87576e1c1303b627294cf8c9fa7b347e2a8e13dadb393067717` |

The final TAM passed the strict 120-frame module lifecycle/state smoke with the
historical oracle counters: 992 PICA calls, 68 register writes, 819 GSP
commands, 104 framebuffer updates, 409 audio submissions, 65,440 audio frames,
120 input reads, and 63 filesystem reads totaling 19,269,907 bytes.

The same private title then ran for 600 presentation frames in the public
`TriAevum.exe` NRI/Vulkan host. It submitted 293 PICA batches and 8,231 draws,
accepted 327,200 audio frames, performed 600 input polls/reads, reached a
non-black top framebuffer and produced an open-title night-scene capture from
the renderer framebuffer. The host exited normally at the requested bound.

An isolated package-shaped directory outside every source/build tree then ran
the frozen Forge from the original verified user inputs with no seed program.
Input preparation took 2.7 seconds; complete structural-IR generation, direct
compilation, TAM packaging and activation completed in under 95 seconds. A
second installed `build-title` invocation completed in 1.9 seconds overall,
including frozen-process startup, with 0.13 seconds in the direct backend. The
isolated `TriAevum.exe` then repeated the 600-frame NRI/Vulkan result using only
its install-relative `resources/` directory and the generated active-title
state.

## Release boundary

The public package contains the generic runtime, frozen Forge, generic game
module, `llc`, `lld-link`, title-neutral resources and notices. It contains no
ROM, `code.bin`, ExHeader, RomFS, generated title object, TAM, TAP, save, shader
capture, decompiled title source or private cache. All generated title outputs
remain under the user's local data root and are marked non-redistributable.

The public package audit and corresponding-source archive remain mandatory;
neither is weakened by this vertical.

## Corresponding-source closure

The public runtime no longer links the title-derived fog scalar helper. The
projection-adjusted linear PICA fog LUT is built by the generic renderer from
the public matrix, fog-range and PICA packing contracts. Development-only
decompilation and UI-evidence targets are omitted automatically when their
private inputs are absent, while the product targets remain complete.

The filtered corresponding-source archive was extracted without Git metadata,
configured offline against the pinned dependency cache and used to build
`TriAevum.exe`, `oot3d_game_module.dll`, the module smoke and ABI tests. The
rebuilt host repeated the 120-frame module counters and 600-frame NRI/Vulkan
vertical. Its framebuffer capture was byte-identical to the installed baseline
(`SHA-256 0426d9642686222af50af45e8925535f956c65c962a4a17020c944eec206f220`).
