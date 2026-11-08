# MM3D static-shadow catalog qualification — partial

G14 / F11 requires **populated, proved eligibility**, not just an empty safe
policy. No MM actor is newly admitted by this change. The runtime still uses its
existing empty default. No profiles, shader packs, scene lists or guest code were
changed. This is the missing offline qualification-tooling step of P2.4, not a
claim that static shadows, room shells or full parity are complete.

## Tool and title facts

`tools/renderer/static_shadow/qualify.py` shares one tracer/evidence mechanism
across titles. See its README for the exact schemas, checks and exit codes.
`titles/mm3d/catalogs/static-shadow-policy.json` owns MM's revision, exclusions,
minimum stability interval and required interaction/lifecycle cases. The tool
has no embedded MM IDs or addresses and cannot accidentally consume OoT evidence
with a colliding numeric actor ID. Existing OoT catalog bytes are not regenerated.

The policy pins EUR v1.1 decompressed code SHA256
`6eec27b1827eab2c87744ac38066d569ffe6fe75790b1eb3300bd91f5099038d`,
loaded at `0x00100000`. It excludes:

- **0x0000 Player**, all forms: dynamic body/pose, own local grass-shadow grid.
- **0x000D En_Horse**, all variants: animated/moving skin, with child Epona's
  separate local refinement. Donkey/bandit variants are not made static merely
  because they do not receive Epona's special grid role.

These are existing guarded MM GRASS facts, not copied N64 IDs. Proof is in the
read-only GRASS lane's `titles/mm3d/gameplay/mm3d_actor_interactions.h` and
`MM3D_GRASS.md`, plus `W/analysis/parity/GRASS/collider_proof.json`. The player's
form/body-cylinder instructions at `1F5FC8`, `204318`, `20431C`, and the horse's
native type read/compare/write at `2B5D04`, `2B5D14`, `39B508`, `39B50C` were
rechecked against the exact EUR code image: **7/7 match**. Those checks validate
the previously established reader evidence; they are not a new proof for any
other native actor ID. No source from the GRASS clone was changed or merged.

Semantic references, read-only under `W/external`:

- `zeldaret-mm/src/overlays/actors/ovl_En_Horse/z_en_horse.c`: init variant
  handling around720–780 and object-ready skin/animation setup around946–968;
- `2ship2harkinian/mm/src/overlays/actors/ovl_En_Horse/z_en_horse.c`: corresponding
  native skin/SkelAnime initialization and update, not enhancement policies;
- corresponding `ovl_player_actor/z_player.c` and existing GRASS form/body proof.

Source file hashes and guard results are retained locally in
`W/analysis/parity/CATALOGS/wave5/retained-summary.json`. Semantic files provide
meaning, not addresses or numeric ID translation. No extracted assets enter Git.

## Archived evidence audit — no new run

The retained LIGHTPERF night log contains **24 actor segments / 19 native profile
IDs**, plus37 world-owner-zero records. All24 actor segments have **lifetime0**;
maximum comparable pairs is **0**, observations range1–120, with warmup120. This
is discovery, not a stationary-actor proof. A no-input run alone would also not
supply all-variant or interaction evidence.

`wave5/retained-evidence.json` binds the original log, original reported source
`3fb5b0c2d369aef717868e59c2f2ce59327815d1`, and original captured binary SHA256
`34884a66ae5f6d69f2e0bb975804843f4a019bb9248f3506c8872344069df9c9`.
This does **not** relabel that historical execution as a CATALOGS run or a current
build. No timestamped idle-window manifest existed, so none was manufactured.
Unknown observed actor IDs are not given guessed N64 names.

Production qualifier result: **0 ACCEPT, 18 INSUFFICIENT, 2 REJECT; exit2**.
The rejects are the mandatory Player/horse exclusions (horse was not observed).
The generated local `.inc` has no entries and is **not installed or committed**.
Zero eligibility does not close G14/F11. The expected refusal itself is a valid
offline negative test of the tool, not a passed publication gate.

## Qualification / next boundary

26 CPU tests exercise synthetic acceptance, title/revision separation, artifact
and native-word checks, required cases, malformed/corrupt/duplicate inputs,
cumulative regressions, tracer expiry/reappearance, windows, lifetime0, churn,
skeletal/temporary rejection, construction failure, and no-output/no-overwrite
behavior. Synthetic accepted profiles are not MM coverage. Actual archived
records independently exercise the parser and insufficient-evidence path.

No game/GPU execution, C++/shader build, cache preparation or new O2/O5 gate was
needed: only offline Python and title policy/docs changed. The existing dual-title
MM build was not reconfigured. Earlier headless/O2 receipts remain historical.

Before any populated MM catalog can be adopted:

1. Integrate the COMPOSITION lifetime producer through the coordinator. This
   CATALOGS branch currently attributes actor identity but not TemporalIdentity;
   a longer run of that implementation cannot repair lifetime0. Do not weaken
   the qualifier or treat an actor pointer as a spawn generation.
2. Review candidate **native** profiles across every parameter/state variant,
   bind zeldaret/2Ship semantics and independently found EUR guard words. Reject
   moving, skeletal and temporary classes; never copy OoT eligibility numbers.
3. Obtain one budgeted functional stability/interaction acceptance on that
   integrated implementation, with actual idle windows and enough consecutive
   shadow-pass observations. Required global cases are destroy/address reuse,
   room unload/reload, savestate restore and changed static pages over budget;
   interactive profiles additionally declare their own cases.
4. Only then emit/adopt accepted rows at the existing title-policy seam, preserve
   content-version refresh and coarse/local grass ownership, and run the owning
   targeted/smoke gates. No discovery run is justified merely to reconfirm the
   current missing-lifetime blocker.

G15's rigid opaque room-shell producer, selected setup/content identity, interior
masks and Forge consumer remain separate missing implementation. The emitter
parser and this actor qualifier do not implement room shells.
