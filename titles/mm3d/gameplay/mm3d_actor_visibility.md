# MM3D H8 visibility and foliage

## Ownership and policy

Renderer EDD owns a 3x MM range, using 2Ship's `(distance + scale) * multiplier`,
not OoT/SoH's short-range `+1000` policy. Native return classes are 0 inside,
1 outside, 2 far. Native inputs are captured at the leaf entry **after** the
native 10000-unit override. Already-visible actors are never reclassified.

The common Actor_DrawAll distance argument is no longer multiplied by dekui
when the product result contract verifies. Complementary bush/grass fade and
spawn thresholds still use the same renderer owner. Standalone dekui and the
failed-contract fallback keep their existing bounded argument implementation.

The presented extent is the spatial row norm of play+C474 divided by the
actual projection diagonal at play+2B4/2C8. Equal matrices yield exactly1 (with
rounding tolerance), preventing another aspect/FOV multiplication when MM's
native VP already contains the presented projection. Widescreen changes X only;
vertical expansion follows an actual vertical projection change, not aspect.

MM3D's padding constants are **sqrt(3)** and **(5/3)*sqrt(3)**, as encoded at
190E04/190E08. MM64's inverse constants are not interchangeable. All49 words of
the native leaf and all46 words of the native class/fade branch are guarded.
Every recomputed native class must match the original leaf's actual r0 result
before an extension is allowed; disagreement declines and is counted.

## Draw-only state

At1754E0 only r0 changes before the native store/fade/lens/room/cutscene gates.
Uninitialized, callback-less and unconditional-draw actors are not granted.
Extra draws require the verified MM light-arena protection. At175650 restore
native class, native fade progression, fade-hold and only flag0x40, preserving
other callback flag changes. Restoration is transactional; failed restoration
stops rather than allowing an extension flag into gameplay update.

Presentation fade is retained separately, keyed by actor identity/update/draw,
only across adjacent walks. Scene/play changes, frame rewind, reset, disabled
EDD and missing actors discard history. Otherwise extended far actors would
restart their visual fade at1 every frame. Identity changes never write a
freed/reused actor. Native update distances are not extended.

## Foliage

- Obj_Grass keeps existing eye-distance/fade/forward modules. Its result hook
  only aligns presented side bounds; it never multiplies that forward input again.
- En_Wood02 offspring uses the native leaf arguments and3x volume, with the
  native/live-actor pressure policy (fewer than165 actors).
- Obj_Mure's five writable clip values are known-value guarded and transactional.
  Spawn uses3x. Active uses3x plus80 because its native code adds40 afterwards:
  this gives `(native radius + 40)*3` without changing a text literal. Each
  consumer sets its own values before the test; neither has a reentrant call
  before consuming the table. Off/pressure paths restore native values. Foreign
  table values are not overwritten.
- Obj_Mure2 continues the guarded existing squared-radius module (including the
  native1/4 cutscene scalar). Obj_Mure3 scales only the squared-distance result
  by1/9 at the two proven Math3D return sites; native1150/1450 radii, child flags
  and lifetime code remain intact. Off is exact replay; pressure uses native.

## Two non-foliage leaf callers (P9)

Do NOT rewrite these callers just because they use the shared culling leaf:

- `4F4838`, return`4F48E4`: En_Horse inactive/summon action. Action table67936C
  contains4F4838 at+8 and is loaded by update2B5CCC (literal2B68F0). Profile679484
  has id0D/category1, size1204, init39B39C/update2B5CCC/draw35DD64. Type2 and the
  global Epona-song flag correspond to 2Ship `EnHorse_Inactive` (z_en_horse.c:1907).
  MM3D adds a visibility/distance decision before summon/spawn. Widening that
  decision would change horse teleport/summon behavior, not draw parity.
- `575C2C`, return`575DB8`: En_Ge1 cutscene actions, installed by init3211FC
  (literal32143C). Profile6764E8 has id70/category4, sizeB20, init3211FC,
  update3993F8/draw39938C. Cue121 and cases8/9 match 2Ship
  `EnGe1_PerformCutsceneActions` (z_en_ge1.c:256). The leaf gates animation-frame
  sound playback; native cutscene/audio behavior remains authoritative.

The source rejects these LRs explicitly by its three-consumer allowlist. Their
identity is now resolved, not a reason to broaden gameplay side effects.

## Evidence and limits

Original EUR image verification and synthetic tests cover code/block mismatches,
tri-state and strict edges, X-only widening, legacy ownership, native agreement,
register preservation, native/visual fade separation, identity changes, EDD off,
actor pressure, table ownership/rollback and squared-distance scaling.

Observer PCs are existing native function/return/loop blocks; no registry
regeneration or instruction patching is required. Runtime acceptance is reported
separately in the lane RESULTS.md. These implementations alone do not qualify all
scenes, timing, physical input, UI, MM-only room geometry/rock gates or full parity.
