# TriAevum provenance policy

> Distribution update (2026-09-05): [Precompiled release contract](TRIAEVUM_PRECOMPILED_RELEASE.md)
> supersedes this document's user-side compilation and no-translated-title-code
> requirements. The material below remains developer/history context; users now
> import their ROM into a package containing precompiled title logic, without SDKs.

## Rule

Every imported source, binary observation, algorithm, generated artifact and
runtime dependency needs a provenance record before it can enter a public
release path. The record identifies origin, exact revision or digest, license,
files affected, method of use and whether redistribution is allowed.

Renaming or rewriting a component does not erase provenance. Clean-room or
semantic reimplementation claims must identify the evidence and preserve a
reviewable separation between evidence and distributable source.

## Evidence classes

| Class | Examples | Repository policy |
| --- | --- | --- |
| Redistributable source | NRI, libultraship, approved Azahar subsets | Retain notices and license; record revision |
| Behavioral reference | emulator traces, PICA documentation | Store only project-authored facts/tests when raw evidence is restricted |
| Private title input | ROM, `code.bin`, RomFS, save states | Never commit or package |
| Generated title output | AOT shards/archive, `game.tam`, shader cache | Local only; never package publicly |
| Third-party mod evidence | TopScreen IPS, CTXB, injected payload | Offline/private unless author grants redistribution |
| Proprietary optional SDK | NGX/DLSS SDK or DLL | Excluded by default; separate review/discovery path |

## Import checklist

Before adding or updating donor material:

1. Pin the upstream repository and exact commit or archive digest.
2. Copy the applicable license and preserve file-level headers.
3. Record whether files were copied, adapted, translated or used only as
   behavioral reference.
4. Record local paths and the product targets that consume them.
5. Check license compatibility for source and combined binaries.
6. Add the item to `THIRD_PARTY_NOTICES.md` and release inventory tests.
7. Keep inaccessible/private evidence out of Git and document how a maintainer
   can reproduce the observation lawfully.

## Known constraints

- The vendored audio subset was imported from Azahar revision
  `beb5681ee7f85586501b16b083a961b707092cd7`; exact and adapted-file evidence
  is recorded in `AZAHAR_AUDIO_PROVENANCE.md`.
- The original TopScreen archive does not currently carry a verified
  redistributable-source license in this repository. Do not publish its IPS,
  payload, CTXB files or the derived `atlas_overrides.o3tu`.
- Original TriAevum contributions use GPL-3.0-or-later. Donor files retain
  their existing terms and notices.
- Proprietary NGX/DLSS components are outside the clean public baseline.

## Release evidence

For each release, retain the source commit, dependency lock, toolchain identity,
allowlist manifest, audit report and corresponding-source archive. Do not retain
private user inputs in CI artifacts or logs. Hashes may be retained when they
contain no recoverable title data.
