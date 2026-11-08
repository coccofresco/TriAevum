# dekui guest-side core port

The baseline account below is historical. For the partial 2026-10-25 update,
current counts, tested behavior and the required translation stop, see
[UPDATE_PORT_NOTES.md](UPDATE_PORT_NOTES.md).

This component ports the guest hooks reached by mm3d-dekui's `MemoryReadCode`
and `CallSVC` in `src/core/arm/dynarmic/arm_dynarmic.cpp`. It targets the original
EUR v1.1 image. It does not install a Restoration image, change guest code bytes,
register runtime sites, or render anything itself.

The port contains the 55-header dependency closure plus the requested
`inventory_menu.h` settings interface. These modules are header-only; `core.cpp`
contains the extracted adapter. Donor structure, atomic settings, helper names,
hook call order, scratch reservations and reverse restoration order are retained.
Each ported file retains SPDX/donor attribution; the adapter also retains the
Citra/Azahar copyright notice. Namespace `MM3D` becomes `Mm3dDekui`.

## Integration contract

- Include `core.h` and compile `core.cpp` as C++20. No Azahar, Dynarmic, Qt,
  Vulkan, ImGui or third-party formatting library is required.
- Supply a `Memory` implementation from `host_interfaces.h`: optional
  little-endian `Read(U32)` and best-effort `Write(U32,U32)`. Convenience
  `Read32OrNullopt`, `Read32` (zero on inaccessible memory), `Write32` and
  overridable `Read8` retain the donor adapter's access pattern. The existing
  `dekui_guest.h` settings-based runtime wrapper remains independent.
- Supply a `Cpu` implementation returning references from `Regs()` and
  `ExtRegs()`, and implementing `Cpsr()` / `SetCpsr(U32)`. Extended registers
  are **64 single-precision words S0..S63**, not double-precision registers.
  The current core accesses only **S0, S1 and S16**, including FormCamera's
  helper calls. A runtime with 32 words can copy S0..S31 into its adapter and
  copy those words back after dispatch; never alias a 32-word array as 64.
- Own one `Core` per guest execution context. Its hook objects retain state
  across calls and nested draws. Atomic settings retain the donor's shared
  ownership; do not reset the core between Begin/End or Before/After calls.
- `Core::CandidatePcs()` returns **122 sorted, unique native PCs**. Four donor
  alternative entries collapse onto the same vanilla PCs. Register both each
  candidate and its following instruction as block boundaries.
- Ask `core.Instruction(memory, pc, original_word)` for the replacement. It
  preserves the entire ordered donor chain and every `CodeMatches` dependency.
  The title adapter selects the supported image; the donor USA title-ID gate
  is deliberately absent. There is no guard cache or guest-code write.
- The executor checks the replacement word's A32 condition. For an SVC,
  preset `r15=pc+4`, then call `core.CallSvc(word & 0x00ffffff, cpu, memory)`
  and continue at `r15`. Non-SVC replacements belong to the integrating A32
  executor. `CallSvc` does not implement OS services; unknown numbers are inert.
- `HostFacts::global_time_us` supplies the donor CoreTiming clock in
  microseconds (default zero). `HostFacts::widescreen_active` supplies the
  current presentation fact (default `Widescreen::Active()`). These callbacks
  are stored per Core. OcarinaPrompt's original fade clock and NotebookRows'
  diagnostic capture clock still use `std::chrono::steady_clock`; they are
  separate from the emulated global clock, as in the donor.
- Existing `Difficulty::Settings`, `kDamagePc`, `kHealthCode`, `kSpawnCode`,
  `SuppressDrop(...,Settings)` and `Spawn(...,Settings)` remain compatible with
  the already integrated adapter. The complete donor atomic settings API is
  now available alongside them. Do not register its two sites twice.

## Address and guard translation

Changed addresses, re-encoded branch words, data pointers and FNV signatures
use literal v1.1 values with trailing `// ref` annotations. Same-address data
and BSS values stay unchanged. Scratch-stack floors and packed vertex/string
values stay unchanged because they are not code pointers. Both CameraLive
projection variants now select the same vanilla routine.

Reproduction/evidence lives in `W/analysis/dekui`, where
`W` = the local work directory (not in this repository):

- `port_guest_core.py`: donor closure, source conversion, Core extraction and
  independent candidate-test manifest. It preserves manual host/test/notes
  files, and writes only this port directory and the analysis directory.
- `guest_port_translation.json`: source hashes, literal decisions, excluded
  sites and native sites that retain Restoration dependencies.
- `patches.json`, `v100_to_v110_map.json`, `ADDRESS_MAP.md`: donor contracts and
  full mapping evidence. The MenuMemory SVC extraction was corrected during
  executable comparison: its own `FirstSVC=0xfffe80` shadows the imported
  MenuPageSwitch value `0xffff90`.

**80 candidates activate on vanilla. 42 retain unavailable Restoration guards:**

| Native entries | Dependency |
| --- | --- |
| FormCamera: `0x0046fc30`, `0x0046ffc4`, `0x004700a0` | Lock, ChangeMode and DialogueCamera appended code |
| All 39 Prices entries | `SingleScreenText::CodeMatches`, the appended message getter |

Their own native addresses and words are translated. Keeping the donor guards
means they return the input word on vanilla. Enabling them requires the later
Restoration source integration and an explicit replacement for those proofs;
this port does not silently weaken the checks. Other retained helpers that
consult appended Hud2 state use their donor guarded fallback paths.

## Excluded Restoration substitutions

Core rejects substitutions at or above `0x007cf000` and never includes them in
the registry candidates. The module helpers and SVC handlers remain available
for the later source port. There are 17 entries at 16 distinct reference PCs;
the shared shortcut PC intentionally appears twice. The original chain retains
MinimapShortcut precedence over MenuShortcuts.

| Module | Reference PC | Restoration source |
| --- | --- | --- |
| SingleScreenText | `0x007d1498` | `game::MessageData::Get`, through `rst::MessageDataGet` |
| MenuShortcuts | `0x007d06c4` | `rst::UiScheduleTriggerHook` |
| MinimapShortcut | `0x007d06c4` | `rst::UiScheduleTriggerHook` |
| MinimapShortcut | `0x007d06dc` | `rst::UiScheduleTriggerHook` |
| FormCamera | `0x007cf16c` | `rst::ShouldSwitchToFreeCam` |
| CameraLive | `0x007d0910` | `rst_CameraChangeModeExHook` |
| CameraLive | `0x007cf174` | `rst::ShouldSwitchToFreeCam` |
| HudButtons | `0x007d42fc` | `Hud2::Calc`, inlined action buttons/D-pad |
| HudButtons | `0x007d430c` | `Hud2::Calc`, inlined action buttons/D-pad |
| HudButtons | `0x007d4348` | `Hud2::Calc`, inlined action buttons/D-pad |
| HudButtons | `0x007d4374` | `Hud2::Calc`, inlined action buttons/D-pad |
| HudButtons | `0x007d4380` | `Hud2::Calc`, inlined action buttons/D-pad |
| HudButtons | `0x007d43bc` | `Hud2::Calc`, inlined action buttons/D-pad |
| HudButtons | `0x007d42c8` | `Hud2::Calc`, inlined action buttons/D-pad |
| HudButtons | `0x007d44e4` | `Hud2::Calc`, inlined action buttons/D-pad |
| NativeNotebook | `0x007d04c4` | `rst::Calc`, inlined schedule update |
| NativeNotebook | `0x007d04b0` | `rst::Calc`, inlined schedule update |

## Host couplings and retained limits

| Donor coupling | Port boundary |
| --- | --- |
| Dynarmic / MemorySystem / process title check | `Cpu` / `Memory`; title selection remains outside Core |
| CoreTiming / Widescreen::Active | Per-Core `HostFacts` callbacks |
| `Common::Rectangle` | Small `HostRectangle` value type used by the unchanged widescreen-fit helper |
| `PauseCapture::Backend` | Original abstract callback API and null atomic backend retained; no capture/bind/reset renderer work until a backend is registered |
| Minimap renderer | Atomic settings/observations retained; `LegacyCopy=false`; guest primitive hooks perform their original memory edits without a framebuffer-copy backend |
| `LOG_INFO` | Inert variadic `HostLog`, retaining argument evaluation and diagnostic state changes without an emulator logger |
| Qt / FileUtil legacy inventory restoration | `InventoryMenu::Options` and `Apply` retained; `RestoreLegacy()` returns an explicit unsupported-operation string and performs no file access |
| Profiles / Textures / Trace / overlay | No calls in this guest dependency closure; their renderer/UI implementations are not linked or emulated by a fake success path |
| CameraWriter / NotebookInspector | Run-loop diagnostics outside MemoryReadCode/CallSVC; not part of this component |

Settings persistence, shipped JSON defaults, F1 controls, renderer callbacks,
additional donor GAR transformations and runtime registration belong to the
integrator. In particular, several modules expose `Options` / `Apply`: retain
the donor settings-loading sequence instead of assuming default-constructed
pose atomics reproduce shipped settings. CameraLive's optional inspection
`Service` remains a header helper but is not scheduled by Core.

Donor prompt/price wording is retained. US-English message byte guards do not
establish compatibility with Italian or other EUR translations. No RomFS,
texture, save, image or other extracted asset is bundled by this port.

## Standalone validation

`core_tests.cpp` accepts `code.bin` as its first argument, or reads
`MM3D_CODE_BIN`. If the file is absent, it reports SKIP for image contracts and
still runs its synthetic helper/replay tests. A present incompatible image
fails the contracts.

`W/analysis/dekui/test_guest_core.py` acquires `W/coord/build.lock` with an
exclusive mkdir, compiles only `core.cpp` and `core_tests.cpp`, runs image and
missing-file cases, and removes its lock in `finally`. Outputs stay under
`W/analysis/dekui/guest_core_build`. A busy lock causes no compiler/test launch.
No CMake, host-build-dev, game or git invocation is used.

Verified: 80 vanilla replacements; 42 native dependency rejections; all 122
expected replacements when the blocked cases receive a separate synthetic
guard-only fixture; 20,818 corrupt/missing guard rejections; 17 appended-entry
exclusions. Synthetic fixture words exist only in test memory and are never
installed by Core. Tests also cover wrong intercepted words, unrelated PCs,
read-only instruction selection, damage/drop behavior, displaced stack stores,
CPSR preservation, S0-only projection writes, nested dialogue transform/restore
and shared Begin/End frame balancing. The existing donor warning for unused
`NotebookPrompt::Build::text_width` is retained.

These are standalone component tests. Live visual and gameplay acceptance has
not been run by this porting task.

## Withheld text tables (Claude, integration)

`single_screen_text_data.h` and `price_text_data.h` are empty in the
repository. dekui's tables carry the original US English message text next
to its edits, which is game material; the full generated tables are kept
outside the repository (`W/analysis/dekui/withheld/`). They become useful
only with Project Restoration's message getter (the modules' guards need it)
and should then be rebuilt as edits applied to the user's own messages,
checked by hash, for each EUR language.

## presentation.json settings port

`settings.h` / `settings.cpp` port dekui's Settings and
`overlay.cpp::ApplyPresentationSettings` with nlohmann/json. Add `settings.cpp`
to the title component and use the existing `nlohmann_json` include dependency.
No Qt, donor directory, asset installation or game process is required.

```cpp
namespace Mm3dDekui {
// Members of Settings:
// bool Load(const std::filesystem::path& path);
// bool LoadFromString(std::string_view text);
// bool Save(const std::filesystem::path& path) const;
// bool SaveBlocked() const noexcept;
std::string_view DefaultSettingsJson() noexcept;
void ApplySettings(const Settings& settings);
void ApplyStartupSettings(const Settings& settings);
bool MigrateLegacyBanners(Settings& settings, const LegacyBannerSettings& old);
}
```

Load the selected settings before guest execution and before the first
`Prices::Initialize`. ApplyStartupSettings clears the pending minimap visibility
request and calls ApplySettings in the donor order. Ordinary ApplySettings leaves
that request intact. Application is not an atomic multi-setting transaction;
subsequent calls require quiescent guest execution. Prices keeps the donor's
one-time startup latch, so changing `double_prices` later requires a fresh session
with that state reset by its owner.

Load requires a version-1 JSON object of at most 1 MiB. Missing and wrong-type
fields use the donor Load fallbacks, including conditional current-pose retention,
derived inventory bar placement, old brown-background keys, mirrored map prompts,
notebook prompt separation, HUD preset scale fallback and single fullscreen-draw
migration. Draw rules retain 64-bit hex precision, validation, deduplication, the
128-rule cap and exclusion of the built-in TransitionOverlay. Unknown keys are
ignored and are not re-emitted, matching the donor. Load/LoadFromString only
update settings data; they do not apply atomics.

A missing file returns false with Save allowed. An existing unreadable/invalid
file or failed LoadFromString blocks Save on that Settings object until a
subsequent successful load (or missing-file Load), matching the donor. Invalid
input leaves the prior values intact. For fallback after a damaged user file,
use a separate Settings object loaded from DefaultSettingsJson and keep the
failed object/status for persistence decisions: successfully loading defaults
into the same object clears its save protection. Do not automatically overwrite
the damaged user file. Save creates an exclusive sibling temporary file, flushes
and closes it, then atomically replaces the destination; it never falls back to
truncating the original or creates parent directories. Windows uses native wide
paths and MoveFileExW; POSIX uses open/write/fsync/rename. Only the Windows path
has been exercised here.

`defaults/presentation.json` is the unchanged donor resource (19,930 bytes,
520 keys, SHA-256 `2a5516c097a79db0779d3e79cc9f036869e6f49390dc64dac1dc3004168afa27`).
The only string values are shader/swizzle signatures. No game text or assets
are included. `DefaultSettingsJson()` embeds identical bytes using bounded
adjacent literals, so deployment does not require a resource-copy rule.

The shipped file explicitly has **song_reset_top=false**, **damage_multiplier=1**
and **double_prices=false**. Its SongReset poses are configured even while that
hook is disabled. Set intended Moonfall profile overrides on the selected Settings
before applying; the faithful donor copy is not a Moonfall difficulty/profile
policy. SongList and DoubleTime are enabled by the shipped file.

Host consumers and retained boundaries:

- `SettingsHost::accurate_hle_audio` holds the applied audio choice; the title's
  audio adapter must consume it instead of Azahar's AudioCore::HLE atomic.
- `BloomSampling::enabled` and `WidescreenProbe::{mutex,saved_draws}` receive the
  donor values. Their pure renderer helpers are ported; renderer consumption
  remains with the host. Minimap, widescreen, camera, dialogue, difficulty,
  healing, drops, death, prices and every guest Options::Apply are applied here.
- `SubmenuHeader` retains Options/Fields metadata, not the Qt GAR writer.
  `restoration_hud_scale`, `magic_bar_y`, `dpad_y`, and submenu-header options are
  archive/setup controls in the donor. Indicator/performance and overlay
  scale/opacity/x/y belong to its overlay UI. These all roundtrip as data; they
  do not acquire invented guest effects in ApplySettings.
- The donor's legacy banner migration also needs notebook.json and successful
  removal of an installed telop override. `MigrateLegacyBanners` exposes its
  exact geometry/brightness formula and applies/marks it once. The host supplies
  LegacyBannerSettings only after successful override removal, then saves the
  Settings object. Startup backups, InventoryMenu legacy archive restoration,
  Notebook/Profiles loading and archive mutation remain host setup tasks.
- Withheld message tables remain empty. Applying `single_screen_text` or
  `double_prices` does not weaken their existing guest guards or restore text.

Standalone validation: `python -B W/analysis/dekui/test_settings.py`, and the same
command with `--clang-cl` for the application's compiler. Each runner acquires
`coord/build.lock` by exclusive mkdir and removes its owned lock in finally;
all objects, executables, fixtures and receipts stay under W/analysis/dekui.
Both toolchains pass 321 checks, including all shipped dialog poses, all 524
serialized keys roundtripping, malformed-file protection, Unicode paths,
atomic replacement failure cleanup, migrations, applied atomics and price-latch
semantics. No game run or host build is part of these tests.
