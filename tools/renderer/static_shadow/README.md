# Offline static-shadow eligibility

`qualify.py` consumes the renderer's `shadow_caster_stability_trace.h` v1
records. It implements the title-parameterized successor to the private
`grass-cost/coord/static-shadow/aggregate_static_shadow.py` discovery workflow.
It does **not** replace OoT's existing generated catalog or install an MM catalog.
No game, GPU helper, compiler, guest write or renderer change is involved.

```text
python tools/renderer/static_shadow/qualify.py --policy POLICY.json \
  --evidence EVIDENCE.json --code-bin LOCAL_CODE.bin --out NEW_DIRECTORY
python -m unittest discover -s tools/renderer/static_shadow -p 'test_*.py'
```

Exit codes: **0** has at least one accepted profile and no insufficient profiles;
**2** valid discovery but empty/unresolved eligibility; **1** malformed, foreign,
stale, missing or inconsistent inputs. Exit 0 is only an **offline subset** gate,
not whole-title coverage or live/static-cache acceptance. Rejected profiles are
expected exclusions, not a publication failure. The report always lists them.
The output directory must not exist; failed input validation publishes nothing.
An I/O failure during output can leave a partial directory; it is not a valid
qualification without a successful process exit and both output files.

## Title/revision and provenance

All documents use `schema: "triaevum.static-shadow"`, `version: 1`, `title`
(lower-case slug), and the exact decompressed `code_sha256`. These fields occur
in the policy, evidence manifest and **each session**. Foreign title/revision
inputs fail, even where numeric profile IDs coincide. No title IDs, scene IDs,
guest addresses or filesystem defaults are hard-coded into the shared tool.

An artifact reference is `{ "path": "relative/or/absolute", "sha256": "..." }`.
Relative paths resolve against the evidence manifest directory. Every referenced
log, semantic source, case result and run provenance receipt is read and hashed.
Only the supplied `code.bin` bytes can satisfy native instruction guards. Nothing
is downloaded, executed or written at those input paths.

The manifests are local reviewer assertions, **not signed attestations**. Hashes
prevent stale substitutions; they do not prove that a semantic review is correct,
that a case actually exercised its acceptance test, or that a declared binary was
built from its source. The retained provenance receipt and reviewer remain the
owners of those claims. Do not bind synthetic fixtures as real game evidence.

### Policy (`kind: "policy"`)

- `image_base`: hexadecimal address used to map guards into `code.bin`.
- `min_pairs`: positive minimum number of unchanged **consecutive** comparable
  pairs in one complete idle segment. Several short segments cannot be added to
  satisfy it.
- `excluded`: array of `{ "id": "0x0000", "name": "..." }`, title-owned
  player/local-grid/moving exclusions. Duplicate IDs fail.
- `required_cases`: unique case names required for **each** candidate, including
  lifecycle and over-budget construction tests as chosen by the title owner.
  A shared construction-test receipt may be referenced by multiple candidates;
  its scope and exact source still need review.

### Evidence (`kind: "evidence"`)

- `sessions`: array of session objects, described below.
- `profiles`: reviewed profile objects, described below. An observed unreviewed
  profile is `INSUFFICIENT`, never silently omitted from the decision set.

### Session (`kind: "session"`, plus the common title/revision fields)

- `id`: unique nonempty session name.
- `source_revision`: full source commit SHA (40 or 64 hex digits).
- `binary_sha256`: the actual captured binary identity, not today's build.
- `provenance`: artifact reference to the retained run receipt documenting that
  source/binary/configuration/input boundary, including any pre-commit diff.
- `log`: artifact reference to the original log with the v1 trace header.
- `windows`: explicit idle windows, each `{ "begin_ms": integer, "end_ms":
  integer }` in the trace's `unix_ms` clock. Windows must not overlap. An empty
  list is valid discovery with no qualified idle observations. Do not derive
  idle windows from an assumed presentation/guest-frame ratio or wall duration.

A file may contain only one trace session header; concatenated processes fail.
A log hash may occur only once. World-owner zero records are counted separately
and never become Player evidence. Actor owner high bits must equal the profile
ID; the address must be nonzero. A zero lifetime cannot qualify an actor.

### Profile

- `id`: native hexadecimal profile ID; `name`: semantic identifier (C token).
- `draw`: `rigid`, `skeletal`, or `unknown`.
- `motion`: `stationary`, `interaction_only`, `flag_state`, `mixed`,
  `continuous`, or `temporary`.
- `all_variants_reviewed`: literal boolean `true`, and `variant_review`: nonempty
  explanation of the complete native parameter/state domain. The current runtime
  eligibility key has no params: reviewing a single idle variant is insufficient.
- `sources`: nonempty artifact references to the native/semantic proof records.
  Use both required semantic references and re-found native meaning/guard evidence;
  N64 IDs/addresses are never a substitute for the running title's facts.
- `guards`: nonempty `{ "address": "0x...", "word": "0x..." }` array of unique,
  aligned native words checked against the exact code image.
- `required_cases`: additional unique case names. Interaction/flag/mixed motion
  requires a nonempty profile-specific manifest, even if global cases exist.
- `cases`: `{ "case": "name", "passed": true|false, "evidence": artifact }`.
  Missing cases are insufficient; **any** failed case rejects; duplicate cases or
  nonboolean outcomes fail validation. Do not replace an old failure with a later
  pass under the same manifest.

## Conservative differences from the old private aggregator

- No guessed idle prefix from `first_change_frame - frame_first - warmup`:
  presentation frames are not consecutive shadow-pass executions. A segment
  crossing a visit is insufficient rather than trimmed into a passing window.
- No material-blip waiver. All idle membership/version changes reject eligibility.
- Cumulative records are deduplicated within `(target, owner, lifetime, t_first,
  frame_first)`. Counter regressions and a rewritten first change fail. Tracer
  expiry/reappearance segments remain separate, so a later stable segment cannot
  erase an earlier violation for the same owner/lifetime.
- Missing/malformed/future-version records are fatal, not silently skipped.
  Inconsistent pair/caster totals and partial version decomposition are fatal.
- Skeletal/temporary/continuous semantic classes or nonrigid observed casters are
  rejected. Unknown draw/ownership/variant proof remains insufficient.
- An `unbounded` construction event rejects the campaign's candidates. Absence of
  that event alone is **not** the >budget progress test; the required external
  case receipt must prove that test separately.

The generated `<title>_static_shadow_actor_catalog.inc` contains ACCEPT rows only,
using the existing `TRIAEVUM_STATIC_SHADOW_ACTOR` macro and binding the policy,
evidence and qualifier hashes. No empty catalog can pass the publication exit
code. Runtime adoption, content-version refresh, exact temporal identities,
coarse/local grass shadow ownership, bounded construction and a positive target
plus smoke remain distinct gates owned by the title/integration lanes.
