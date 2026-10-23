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

## iMMERSE MXAO permission

On 2026-10-12 the maintainer reported the terms of Pascal Gilcher's permission
for MXAO (iMMERSE): it expressly allows the distribution and modification of
MXAO for the exclusive use of this repository of the TriAevum project, on the
condition that all rights in it, and the license for any further use, remain
the author's. No correspondence is stored in this repository.

The module stays under its original license (`runtime/three_ds_recomp/src/fast/immerse/LICENSE.md`).
It is built and shipped as its own shared library beside the GPL runtime, which
reaches it only through the MIT module interface and runs without it; see
`LICENSE_SCOPE.md`. This record does not extend the permission to recipients,
forks or other projects.

## NVIDIA DLSS module

On 2026-10-13, after reviewing the NVIDIA RTX SDKs licence that accompanies
NVIDIA's public DLSS SDK (version of 14 March 2024, shipped with SDK 310.7.0),
the maintainer approved distributing DLSS with TriAevum through a separately
licensed module. The review found that the licence permits distributing the
SDK's runtime and libraries in object code inside an application with material
functionality of its own, under terms at least as protective as NVIDIA's,
with NVIDIA attributed where DLSS is used and NVIDIA notified before any
commercial release (TriAevum is not commercial). Because TriAevum's runtime is
GPL, the NGX SDK library is linked only into the DLSS module
(`runtime/three_ds_recomp/src/fast/dlss/LICENSE.md`), which ships beside
NVIDIA's unmodified, signed DLSS runtime and NVIDIA's licence text; the GPL
runtime links nothing of NGX, and the release audit rejects any other binary
that contains NGX code. Before this decision the beta.1 runtime had NGX
statically linked through NRI without NVIDIA's runtime; beta.1b replaces it.

## Release effect

Legal approval does not bypass technical gates. The packager remains disabled
until every required item in
`tools/triaevum_release/release_readiness.json` is complete.
