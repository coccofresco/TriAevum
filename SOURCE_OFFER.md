# Corresponding source

Every TriAevum binary release must include the exact corresponding source used
to build it under `source/`, together with its source commit and dependency
identities in `release-manifest.json`. Build and installation instructions must
be sufficient to recreate the distributed runtime and Forge binaries without
access to a game dump.

The generic runtime source is separate from the translated title source. Under
the precompiled release model, `source/titles/` also contains the exact generated
C++ and a source snapshot for the title's support/wrapper/build tools. The
catalog binds both to the shipped title DLL. ROM inputs, extracted assets and
local user data remain excluded. Original-game rights are not relicensed by
including translated source; see `LICENSE_SCOPE.md`.

The source archive is independently buildable without Git metadata or any
decompiled title source. CMake reads the recorded source commit from
`SOURCE_ARCHIVE_MANIFEST.json`, excludes development-only evidence targets when
their inputs are absent, and still builds the distributed runtime, Forge-facing
generic module and their verification tests. Developer title rebuilding is
documented in `docs/TRIAEVUM_PRECOMPILED_RELEASE.md`; users never perform it.

Public releases and their matching source archives are published at
https://github.com/coccofresco/TriAevum/releases. Source is supplied directly
inside every binary package under `source/`; no separate request is required.
The public source repository is https://github.com/coccofresco/TriAevum.

The public repository starts from a policy-filtered source snapshot rather than
the private development history. `SOURCE_ARCHIVE_MANIFEST.json` records the
original runtime/dependency revisions; `recipes/precompiled-titles.json` records
the separate title-build revision and source hashes. Use these manifests, not
the new public Git commit ID, to identify the code paired with a binary release.
