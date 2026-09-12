# Alpha.2 ROM import coverage regression

## Confirmed cause

The published alpha.2 Windows and Linux recipe files contain only the baseline
EUR input and the strict catalogue EUR input. Both omit the USA COPY adapter
and the EUR/USA content-family contracts shipped in alpha.1c. Consequently an
otherwise supported decrypted USA cartridge is rejected before adaptation.
The same packaging regression affects both platforms, not only Flatpak.

Program ID 0004000000033500 identifies the USA title but is not a complete
content identity. Reading it successfully is not evidence of a Flatpak path
permission failure. A document-portal path can legitimately differ from the
user's original path; granting broad filesystem permissions cannot restore a
missing import contract.

The earlier alpha.2 qualification exercised the baseline EUR input. It did not
exercise USA installation, and therefore missed this regression. The README's
USA support claim was not satisfied by those distributed packages.

## Correction

The public alpha.1c package is the read-only evidence source. Recovered data:

- Two qualified input contracts, with exact code identities and logical
  filesystem/normalized ExHeader identities.
- The already published 1,825,834-byte USA COPY program, SHA-256
  3a7771a365157c6a2a26ce6916a3a3437a15384a98c39c130cd16a98d2dd9519.
- No ROM, executable game bytes, extracted assets, signatures, keys or older
  runtime binaries are imported into the source tree.

Owner: tools/triaevum_release/qualified_input_coverage.py, with reviewed data
under tools/triaevum_release/input_coverage/.

The publisher binds these identities to the current canonical execution
contract. It preserves the current platform module, translated-source
provenance, shader compiler, portable corpus and device pipeline bindings.
Changes to canonical code/process or conflicting existing input contracts are
rejected. No recompilation or regional renderer workaround is introduced.

prepare_release.py includes the recovered recipes and COPY artifact in its
allowlist. audit_release.py rejects loss or alteration of either qualified
family for this canonical title. A separately maintained Linux layout must
preserve the same input roles and pass the same audit.

For a metadata-only candidate based on existing binaries:

```text
python -m tools.triaevum_release.qualified_input_coverage --package PACKAGE --output NEW_OVERLAY
```

Merge NEW_OVERLAY/layout-overlay.json into the explicit package layout, retaining
its input_copy_adapter role. Repackage and audit; do not overwrite catalogue
files inside an already installed game or use an old title DLL.

## Verification on September 12

- Shared binding tests cover Windows/Linux targets, preservation of current
  shader and module records, idempotence, missing coverage, altered family
  contracts and incompatible canonical execution.
- Full local suite: 390 tests, 14 expected skips, no failures.
- A private Windows package with alpha.2 binaries unchanged passes release audit.
- Actual frozen alpha.2 Forge accepts the existing decrypted USA .3ds, adapts
  it with the original COPY/resource algorithm and finishes activation with
  zero compiled title objects. Initial GPU preparation retains 588 pipelines.
- The initial run was dominated by a slow TopScreen download; its launcher
  timed out while its worker later completed. A bounded repeat using the
  verified local archive/cache finished with exit 0 in 6.22 seconds. This is
  a warm repeat, not a cold-install performance claim.
- The resulting USA installation boots the real NRI intro: 300 presentations,
  zero whole-AOT memory faults, exit 0, framebuffer visually inspected.
- With explicit maintainer permission, the existing private equivalent USA
  repack was transferred to the Linux test PC. Actual frozen Forge inside the
  read-only Flatpak candidate accepts it through the content-family path:
  its ExHeader and RomFS whole-file hashes differ from the exact reference.
  Preparation completes in 27.86 seconds with the verified TopScreen archive
  already available, zero compiled title objects and 588 device pipelines.
- The resulting Linux USA installation boots 300 intro presentations with zero
  whole-AOT memory faults and exit 0. Its framebuffer was visually inspected.
  This tests the real Flatpak runtime/Forge and equivalent-dump acceptance,
  not physical Steam Deck hardware or a complete USA playthrough.
- GitHub workflow 34698471414 passes all three jobs on the source correction
  b1702ea: Windows Forge/native ABI, Linux native ABI and release/module policy.

Public alpha.2 release assets have not yet been replaced. The source fix and
private candidates do not repair existing user downloads by themselves.
