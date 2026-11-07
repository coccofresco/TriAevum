# Title-bound Forge backups

The archive format remains `triaevum_backup_v1`. `game` is a required identity,
not a display label. Backup plans, results and restore previews expose it.

## Recipe contract

A title-aware package declares `game` on **every** supported revision in its
recipe catalog. Values are nonempty lowercase ASCII keys (letters, digits and
underscore, at most 64 characters). Recipe IDs must be unique. There is no
inference from title names, recipe-ID prefixes, texture overrides or save bytes.
The historical `recipes/oot3d.json` filename is a packaging convention, not the
title identity of a title-aware catalog.

An entirely unannotated legacy catalog retains its original OoT-only meaning.
Partly annotated catalogs are rejected. A new title must not be added to an
unannotated legacy catalog. This metadata does **not** approve a revision,
provide a whole-AOT module, or qualify compiler-free installation.

`active-title.json` selects a revision in that catalog. If it also declares
`game`, the values must agree. Missing/malformed catalogs, malformed active
records and unknown active revisions fail closed. With no active title,
recovery is permitted only for an unambiguous single-game catalog. A multi-title
package still needs separate data roots for different games; switching the
active revision does not migrate or identify arbitrary files already in a root.

## Backup and restore

Creation resolves identity before taking a lock or writing a temporary archive.
The archive is re-opened and verified against that same game before publication.
Restore verifies both game and membership in the destination game's recipe set
before taking a lock or creating journals/staging files. Supporting both games
in a package never permits cross-game restoration, even if a manifest's game
label is changed while retaining the other game's recipe ID. The public direct
restore-plan API applies the same check. Revisions within the same game retain
the existing warning/portability policy.

The original `open_backup` API remains OoT-only. The explicit
`open_backup_for_game` API requires a trusted expected game from the caller;
installation frontends resolve it from their own recipe contract. They must not
copy the expected game out of an untrusted archive.

Existing hash/member/path checks, session exclusion, durable restore journal,
rollback snapshots and cross-platform portable subsets remain in use. An
unreadable package identity requires repair of its metadata, not a guessed
backup label. These checks prevent accidental title mixing; archives and local
recipe metadata are not cryptographically authenticated title attestations.

## Qualification boundary

`forge-core/tests/backup.rs` exercises both title directions, wrong-recipe
relabeling, missing/ambiguous/inconsistent identity, legacy OoT archives,
non-OoT round trips and interrupted restore rollback using disposable fixtures.
`TRIAEVUM_TEST_NATIVE_SAVE` optionally supplies read-only opaque save bytes for
the non-OoT round trip; its digest is reported with `--nocapture`. This proves
storage preservation, not a native game's save/restart or an installed product.
Physical platform qualification remains separate.
