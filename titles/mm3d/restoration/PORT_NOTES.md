# Project Restoration on the original EUR v1.1 image

This directory ports the guest-side logic of leoetlino/project-restoration
revision `8bb2a7b8`, as included by tgacquin/mm3d-dekui. It does not apply the
donor BPS, rewrite v1.1 as v1.0, or install appended ARM code. The source and
generated instruction contracts are GPL-2.0-or-later; see COPYING. No game
messages, textures, layouts or archives are supplied here.

The title adapter is `titles/mm3d/adapter/mm3d_restoration.{h,cpp}`.
It includes `engine_impl.h`, so these implementation fragments do not need
separate build targets. The existing include paths must include both this
directory and `titles/mm3d/dekui`. The shared executor and renderer are unchanged.

## Source coverage and ownership

| Files | Responsibility |
| --- | --- |
| `input.h`, `main.h` | Existing input remapping, assignment, gear and ocarina shortcuts |
| `engine_main.inc` | Schedule/menu/minimap precedence, notebook callbacks, widgets, message getter and caller-owned edits |
| `engine_hud.inc` | HUD2 allocation/lifecycle, native helper calls, visibility, D-pad, counters, magic, B action icon |
| `engine_link.inc` | Item usability, transformations, Zora swim state, Goron roll tracking, fast arrows, Elegy |
| `engine_fixes.inc` | Camera state, free-camera selection/reset, inverted time, NPC fractional movement, ice/platform/Bombers/hint/owl fixes |
| `engine_state.inc` | Full Calc sequence, extended Song of Time save state, ocarina replay handling |
| `engine_boss.inc` | Odolwa collision/phase/animation behavior, Goht, Gyorg and Twinmold |
| `engine_dispatch.inc` | HKS entry contracts, trampoline registers/flags/stack, package loading and extension callbacks |
| `routine.h`, `session.h` | Nested coroutine calls and execution-context state |
| `sites.h`, `substitute.h` | Verified native instructions/data, branches and supported replacement opcodes |
| `layout.h`, `enums.h`, `item_tables.h` | Compiler-proven offsets and attributed donor source constants |
| `port_tests.inc` | Fixtures included by the existing standalone Restoration tests |

The inventory accounts for all 107 HKS records. Multiword patches expand to
108 instruction descriptors plus 15 guarded data patches. `_start` and
`LoaderMain` are represented by host C++ construction and compilation; a CTR
extension loader, fake heap and appended executable storage are unnecessary.
The five extension callbacks are host dispatch sentinels at `0x000fff00` through
`0x000fff10`, not registry blocks or executable guest memory.

Soft post hooks run their body and resume at the original site through the
existing SkipOnce mechanism. The one pre hook replays STRH before UpdateTime.
Calls preserve the trampoline-specific register, VFP, CPSR and stack results.
Return hooks use the original LR. The source contracts target this A32 image.
HUD native helper reentrancy declines the nested HUD hook. Guest allocations
and cached strings have session lifetime, corresponding to donor static data.

## Integration API

Namespace `Oot3dNativeGame`, class `Mm3dRestorationPort`:

```cpp
explicit Mm3dRestorationPort(Mm3dGuestCalls&);
void Register(Mm3dGuestPatches&);
std::size_t ApplyDataPatches(MemoryBus&);
void SetDekuiBridge(Mm3dRestoration::DekuiBridge);
bool SetMessageEdits(std::vector<Mm3dRestoration::MessageEdit>);
Mm3dRestoration::PortCapabilities Capabilities(MemoryBus&) const;
static std::vector<std::uint32_t> RegistryBoundaries();
std::uint32_t HudHeartWidget() const noexcept;
```

`MemoryBus` above abbreviates `oot3d::recomp::a32::MemoryBus`. One port belongs
to one guest execution context; its calls and patch collection must remain
alive while it is used. Install `Mm3dGuestCalls` once, register the Restoration
sites, and combine `RegistryBoundaries()` with the other title boundaries
before generating the registry. Verify against the mounted image and actual
registry. Apply the guarded data patches after mounting, before guest use.
An original-byte mismatch leaves that data patch alone; repeated application
returns zero. Instruction selection never writes patched instructions.

The final manifest has **267 boundaries / 86 explicit targets** in
`W/analysis/dekui/restoration_registry.json` and `RESTORATION_REGISTRY.md`.
This includes Claude's observed `0x0060AEE8` continuation: the second instruction
of the native indexed-load accessor at `0x0060AEE4`, followed by BX LR. Its
runtime caller still needs a trace if regeneration does not resolve the fault.
Ordinary indirect guest function/vtable calls continue using native registry
entries; the manifest does not invent addresses for runtime object pointers.

`DekuiBridge` is
`std::function<void(uint32_t svc, GuestState&, MemoryBus&)>`. Forward it through
the existing CPU/Memory adapters to the **same Core** that handles native
FormCamera sites. Round-trip CPU registers, CPSR and VFP words. It receives:

- `0xFFFFAF`: FormCamera lock/recovery test on a temporary CPU snapshot. A
  return to its supplied LR indicates that free-camera selection is locked.
- `0xFFFFB3`: CameraLive change-mode behavior on the actual trampoline state.
- `0xFFFE90`: SingleScreenText message-entry behavior on a temporary CPU state,
  before validated caller-owned edits and native message decoding.

The temporary LR is a marker, not a dispatched callback. The bridge must not
execute the guest. Set bridge/configuration while execution is quiescent.

`Capabilities` returns `{message_getter, camera_bridge}`. Both require their
registered site to be enabled by patch verification and their original native
guard words to match on the current memory. Camera also requires a bound bridge.
Unregistered or unverified ports report false. Verify with the actual registry
in production; verification without a registry is only used by fixtures.

## Required Core adapter proposal

Core is read-only in this task. The review-only
`W/analysis/dekui/RESTORATION_CORE_PROPOSAL.diff` proposes three optional virtual
methods on the dekui Memory adapter, all disabled by default:

```cpp
bool RestorationMessagePort() const; // port.Capabilities(bus).message_getter
bool RestorationCameraPort() const;  // port.Capabilities(bus).camera_bridge
uint32_t RestorationHeartWidget() const; // port.HudHeartWidget()
```

Only appended-code proofs in SingleScreenText, CameraLive and FormCamera accept
these capabilities as an alternative. Every native Prices and FormCamera guard
remains checked. The actual mixed helper is `FormCamera::CodeMatches` (there is
no `LockCodeMatches`). This makes the 39 Prices and 3 FormCamera sites eligible
without pretending that appended donor instructions exist.

NativeMinimap also needs the dynamic heart-widget pointer because HUD2 is
allocated by the guest allocator, not placed at donor static address 0x7f8104.
`HudHeartWidget()` is zero before successful widget lookup. The proposal retains
name, bounds, visibility and ancestor-opacity validation and its legacy fallback.

Do not simply set a global unconditional "Restoration present" flag. Bind the
same Core instance and use the verified capability getters above.

## All 17 appended dekui entries

These behaviors now live in source. Appended PCs remain excluded from Core's
instruction candidates; no synthetic bytes or second hook owner are installed.

| Module / reference PC | Source behavior |
| --- | --- |
| SingleScreenText `0x7d1498` | `Engine::MessageGet`, bridge plus caller-owned edits, Odolwa ID remap retained |
| MenuShortcuts `0x7d06c4` | `ScheduleTrigger`: ZR+Start disabled; ZR+Select map behavior |
| MinimapShortcut `0x7d06c4`, `0x7d06dc` | `ScheduleTrigger`: minimap toggle takes precedence in the supported gameplay context; consumes Select and plays its cue, otherwise map fallback |
| FormCamera `0x7cf16c` | `ChangeCamera`: shared Core lock/recovery probe |
| CameraLive `0x7d0910`, `0x7cf174` | Shared change-mode bridge; dialogue mode 11 excluded from free-camera selection |
| HudButtons `0x7d42fc`, `0x7d430c`, `0x7d4348`, `0x7d4374`, `0x7d4380`, `0x7d43bc`, `0x7d42c8`, `0x7d44e4` | `HudCalc`/`HudDpad`: all four item icons shown; normal A/D-pad presentation; ocarina button hidden, without changing physical ZR input |
| NativeNotebook `0x7d04c4`, `0x7d04b0` | `ScheduleUpdate`: Event Notes X map trigger and donor Y handling through `MapInputFlags` |

## User-owned messages

No withheld US English text table is restored. `MessageEdit` carries an ID,
language index 0..13, caller-provided equal-length original/edited byte vectors,
64-bit FNV-1a hashes, a `price` selector and optional original/edited metadata.
The setter rejects invalid hashes, unequal/empty/oversized vectors, duplicate
ID/language keys and unpaired metadata. It replaces the edit list atomically
only after validation. A single edit should combine changes for an ID/language;
overlapping separate price/presentation edits are intentionally rejected.

The getter validates the user's GMSG header, entries, language bitmask, stride,
bounds and exact message hash before writing. It edits only the selected
language and restores original bytes when the associated setting is disabled.
`price` follows Prices::enabled; other edits follow SingleScreenText::enabled.
Metadata is guarded along with message bytes. The donor field decoding, binary
search, all 14 language-reader calls and `0x195e -> 0x1962` lookup remap remain.

**TODO for integration:** obtain/review edits from the user's own EUR messages
per language. Native price logic can run without text edits, but its displayed
text will not be corrected until matching edits are supplied. The port does
not guess translations or ship game strings. Missing/corrupted data rejects an
edit; memory write-fault policy remains with the existing MemoryBus adapter.

## Offline evidence and limits

`W/analysis/dekui/RESTORATION_HOOK_PROOFS.md` and its JSON account for all 107
positions, including every one of the 62 previously low-confidence mappings.
Instruction cases use unique normalized eight-word anchors on both sides,
excluding HKS-modified words, plus original neighbors. Two data positions use
independent xrefs and unchanged table evidence. No HKS position is unproved.

`restoration_layout_probe.py` compiles the actual donor headers for i686 with
two-word member-function pointers and passes their size/offset assertions.
`restoration_layouts.json` retains 131 records. `layout.h` emits only referenced
fields; the full 8,963-field evidence remains in analysis. Fields and bit widths
come from those records, not host 64-bit C++ structure layouts.

`test_restoration_port.py` runs clang-cl C++20 syntax validation and builds/runs
the standalone MinGW executable under the exclusive `coord/build.lock`.
Coverage includes all 40 source actions, all 108 instruction descriptors,
15 original-image data guards, 648 corrupt/missing guard checks, schedule and
touch ABI, message languages/reversal, camera bridge, player actions, time/NPC
fixes, bosses, Song of Time state transitions, positive HUD lifecycle/reentrancy,
HUD visibility/magic and ocarina stack unwinding. Original EUR image SHA-256 is
`6eec27b1827eab2c87744ac38066d569ffe6fe75790b1eb3300bd91f5099038d`.

`test_restoration_core_proposal.py` compiles an analysis-only Core copy. It
verifies 80 baseline and 122 capable native sites, 19,918 corrupt/missing native
guard rejections, separate capability ownership, no instruction-selection writes,
excluded appended sites and dynamic HUD opacity. It does not modify Core.

Fixtures mock native function results; these are not live game or renderer
tests. The full game target, resource loading, actual callback traces, boss
encounters and visual layout acceptance remain integration work. HUD2 requires
the donor `Joker.Main.New.gar`/Hud2 resource and matching common layouts mounted
by the host; no archive is copied here.

Small deliberate bounds fixes reject invalid actor/form/language/arrow indices,
cycles in widget traversal and non-play HUD state. SpawnArrow rejects index 4
instead of the donor's one-past-end access. Valid-data behavior follows the
donor. No git, CMake, game or host-build-dev operation is part of this port's
generation or tests. Generated evidence and authored text use LF.
