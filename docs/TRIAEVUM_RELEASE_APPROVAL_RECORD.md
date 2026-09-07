# TriAevum release approval record

> Distribution update (2026-09-05): [Precompiled release contract](TRIAEVUM_PRECOMPILED_RELEASE.md)
> supersedes this document's user-side compilation and no-translated-title-code
> requirements. The material below remains developer/history context; users now
> import their ROM into a package containing precompiled title logic, without SDKs.

## Maintainer decision

On 2026-09-02, the project maintainer reported that qualified legal counsel had
approved the release model described in `TRIAEVUM_RELEASE_ARCHITECTURE.md` and
selected GPL-3.0-or-later for original TriAevum contributions.

No confidential legal opinion or personal information is stored in this
repository. This record closes the project-policy approval gate; it does not
replace compliance with donor licenses, corresponding-source requirements,
the public-package audit or the prohibition on distributing private title and
mod artifacts.

## Attribution decision

Attribution is implemented by retaining source-file copyright headers, the
root GPL notice, `THIRD_PARTY_NOTICES.md` and every applicable donor license.
No custom restriction is added to GPL-3.0-or-later.

## Release effect

Legal approval does not bypass technical gates. The packager remains disabled
until every required item in
`tools/triaevum_release/release_readiness.json` is complete.
