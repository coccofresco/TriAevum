# Dusklight UI and Input Donor Plan

Status: F1/F12 ownership split implemented and Windows widget-tested. Standard
pages still use the existing ImGui widgets as a migration bridge; the Dusklight
frontend/controller import and RmlUi/NRI adapter are not implemented yet.
Decision: use Dusklight as the primary product-level donor for standard,
cross-platform application menus and controller configuration, preserving the
shared 3DS input contract and NRI. This is not a wholesale F1 UI replacement.
Native OOT3D HUD, menus and TopScreen composition are outside this replacement.

## Scope Clarification

- Preserve the existing advanced/custom renderer controls and their behavior:
  Grass, CACAO, Toon shading and outlines, reflections, and other custom effects.
  Do not migrate or redesign these panels as part of the donor integration.
- Candidate standard surfaces: controller/keyboard/mouse configuration, device
  discovery and navigation, window/fullscreen/display settings, audio, language,
  general preferences, profile management and application dialogs. Adapt donor
  mechanisms only where they fit our existing services and platform needs.
- F1 opens standard settings; F12 opens retained advanced ImGui panels. Both
  start closed. Opening either closes the other; repeated keydown and key release
  do not toggle them. F2 retains its existing native-presentation override.
- Share one application entry/navigation layer. Standard pages and retained
  advanced panels must use one focus/capture arbiter; never let both consume the
  same gesture. Reuse settings and profile persistence, including advanced values.
- Preserve F1 access, F2 native-presentation override/status, display confirmation
  outside tabs, and persistence errors. Any retained UI toolkit uses the same
  host event owner and explicit composition boundary, not a second event loop.
- TopScreen layout/behavior remains title-owned; native HUD and game menus are
  not replaced with Dusklight content. This scope does not authorize gameplay,
  renderer, effect or native input semantic changes.

## Pinned Evidence

- Dusklight: `TwilitRealm/dusklight`, commit
  `158abde3816042eeb52f9bda5a7fac50487d4451`.
- Its Aurora submodule: `encounter/aurora`, commit
  `d0c931da2ed3f41d0e42736c2ab52a78c7cf1a9d`.
- Dusklight `LICENSE.md`: CC0-1.0. Aurora `LICENSE`: MIT, copyright
  Luke Street. These licenses permit adaptation into the GPLv3 project for
  covered original contributions. They do not clear third-party rights.
- Before importing: inspect every selected file and transitive dependency,
  including RmlUi, fonts, icons, styles and input helpers. Do not infer their
  licenses from the repository root. Exclude game-derived graphics/sounds.
- Record imported paths, upstream commits, licenses, authors and local changes
  in donor attribution metadata. Preserve required MIT notices, and credit
  Dusklight/TwilitRealm even where CC0 does not require attribution.

Sources:
[Dusklight](https://github.com/TwilitRealm/dusklight/tree/158abde3816042eeb52f9bda5a7fac50487d4451),
[Aurora](https://github.com/encounter/aurora/tree/d0c931da2ed3f41d0e42736c2ab52a78c7cf1a9d).

## Reuse Map

| Surface | Donor evidence | Integration boundary |
| --- | --- | --- |
| Application settings and navigation | `src/dusk/ui/{settings,window,pane,tab_bar,nav_group,input}.*` | Reusable application UI, not native HUD |
| Widgets | `src/dusk/ui/{button,bool_button,number_button,select_button,color_input}.*` | Existing setting owners and persistence |
| Binding capture and device configuration | `src/dusk/ui/controller_config.*` | Shared input service, native 3DS endpoints |
| Controller mappings | Aurora `lib/dolphin/pad/pad.cpp`, `lib/input.*` | Extract host mechanisms; exclude GameCube PAD ABI and global port model |
| UI rendering | Aurora `lib/rmlui/*` | Reference only for backend integration; implement NRI adapter, not WebGPU renderer import |

Observed controller UI mechanisms include waiting for neutral input before
capture, suppressing navigation until release, cancelling pending bindings on
close, device-change refresh, deadzone controls and capability-gated rumble.
These are reusable behaviors, not proof of compatibility with every controller.

## Target Architecture

Physical devices -> one host device/event owner -> shared 3DS input mapper
-> title adapter. UI capture arbitrates delivery before gameplay; a captured
gesture cannot also activate the game. F1/F2 remain application commands.

Dusklight-derived application UI -> existing typed setting owners -> NRI,
window host, input service and TopScreen. No second profile database, duplicate
setting variables, competing SDL event pumps or copied Twilight Princess logic.
Keep saved profiles compatible through explicit schema migration where needed.

RmlUi is a candidate for substantial UI reuse, not an already approved dependency
import. Prove one NRI-rendered, controller-navigable settings page before moving
the selected standard pages. Keep the existing advanced panels intact and all
application UI rendering after scene effects at the declared UI boundary.
The donor uses SDL3 directly; SDL2-compat in our packages is not an API migration.

## Delivery Order

1. Inventory current F1 settings/actions and persistence; map each to one owner
   and mark standard migration candidates versus retained advanced panels.
   Select and license-audit a minimal donor dependency closure.
2. Implement the isolated application UI adapter and one settings/controller
   page. Bind real current settings, without duplicating their state.
3. Port capture/navigation/device behaviors into the common input boundary.
   Decide direct SDL3 migration from the required closure; never run two owners.
4. Migrate only selected standard pages with a coverage checklist. Preserve F1 display
   confirmation, F2 status, profile management, capability checks and error UI.
5. Remove replaced UI/input paths only after parity. Keep native game UI and
   existing advanced effect panels intact; verify navigation between both UI
   surfaces and profile round-tripping without losing advanced settings.

Acceptance: controller-only and keyboard/mouse navigation, remapping with neutral
release, unplug/replug, focus loss, no gameplay leakage, save/reload profiles,
real-widget checks, framebuffer/UI composition checks and Windows/Linux tests.
Android touch and Steam Deck behavior require separate platform qualification;
reuse of donor code alone does not establish it.

Related: `THREE_DS_RECOMP_INPUT_ARCHITECTURE.md`,
`OOT3D_NRI_FIDELITY_EXTENSION_ARCHITECTURE.md`, `TRIAEVUM_F1_MENU_REVIEW.md`.

## First Implementation Tranche

- `fast/ApplicationMenuRouting.h`: toolkit-independent exclusive toggle policy.
  `Fast3dGui.cpp` consumes F1/F12 SDL events once, rather than polling once per
  rendered frame. Both shortcuts are withheld from ImGui widget input.
- Standard surface: Display, Antialiasing and registered application-owned tabs
  (Controls, TopScreen, language and profiles when provided by the frontend).
- Advanced surface: Lighting/CACAO/shadows, Reflections, Toon/outline, Grass,
  Textures and renderer presets. Existing panel implementations and storage stay
  unchanged. No native HUD, effect math, title module or GPU scheduling changes.
- Shared display confirmation remains outside both windows. Hidden panels do
  not flush the visible panel's slider edits on every frame.
- `GraphicsSettingsPanel::Draw()` remains a combined diagnostic entry for the
  existing widget fixture. Production calls only `DrawStandard`/`DrawAdvanced`.
- Windows incremental `triaevum_public_runtime` build passes, without rebuilding
  title/AOT. Actual-widget smoke: 3,969 assertions (including per-frame invariants),
  exclusive routing and standard/advanced control segregation. No live-game
  framebuffer or physical controller qualification performed for this tranche;
  Linux/Android/macOS builds remain unverified for these new changes.

Next: license-audit/import the minimal Dusklight UI dependency closure and add
the actual standard frontend through an NRI adapter. Do not describe the current
ImGui migration bridge as a completed Dusklight port or controller rewrite.
