# Update from ZIP and publisher signatures

Windows portable packages update in place: Forge's Home offers **Update >
Choose the downloaded TriAevum ZIP...** (command line: `forge/triaevum-forge.exe
update <zip>`). Owner decision 10 makes the package's publisher signature a
precondition: release manifests already hash every file, which proves a package
is internally consistent, not who made it.

## How a package is signed

- The release manifest (`docs/release-manifest.json` on Windows) is signed with
  Ed25519 over the message `TriAevum release manifest signature v1\n` followed by
  the manifest's exact bytes. The detached signature sits beside it as
  `docs/release-signature.json`:
  `{"format": "triaevum_release_signature_v1", "algorithm": "ed25519",
  "key_id", "manifest_sha256", "signature"}`. It is not listed in the manifest
  (it signs it); the audit and the archive accept it as the manifest's companion.
- Forge embeds the public keys it trusts:
  `tools/triaevum_forge/update-signing-keys.json` (`key_id` = first 16 hex digits
  of the key's SHA-256). Several keys may be listed for rotation.
- Forge verifies with `ring` (already linked for HTTPS); the release tooling
  signs and the audit verifies with the Python `cryptography` package. Both
  directions are tested (`forge-core/tests/python_oracle_signature.rs`).
- **The private key never enters the repository or a package.** The tooling
  refuses a key path inside the repository or the package.

## One-time step for the owner

1. On the publishing machine, outside the repository (an encrypted volume or
   a password manager attachment is a good home):

   ```
   pip install cryptography
   set TRIAEVUM_RELEASE_SIGNING_PASSPHRASE=<a long passphrase>   (optional, encrypts the key file)
   python tools/triaevum_release/release_signature.py generate-key --private-key D:\keys\triaevum-release.pem --add
   ```

   `--add` writes the public key into `update-signing-keys.json`; the command
   prints the same entry.
2. Back up the private key file (and the passphrase) in a second safe place.
   Losing it means users can update only to packages signed by a key their
   Forge already knows: publish one release signed by the old key that adds the
   new key, before retiring the old one.
3. Commit `update-signing-keys.json`. From the next Forge build on, that Forge
   accepts packages signed with this key. Packages built before carry an empty
   key list: their Forge refuses every update ZIP, so the first signed release
   must be installed from a fresh folder (or with `--update-target`, below).

## Each release

`prepare_release.py` signs the package after `package_release` when given
`--signing-key <path>` or `TRIAEVUM_RELEASE_SIGNING_KEY=<path>`, and refuses a
non-candidate release without one. By hand:

```
python tools/triaevum_release/release_signature.py sign --package <package folder> --key D:\keys\triaevum-release.pem
python tools/triaevum_release/release_signature.py verify --package <package folder>
python tools/triaevum_release/archive_release.py --package <package folder> --output <zip>
```

The release audit verifies a present signature against the embedded keys.

## Status before beta.2 (release review G3-F1)

`tools/triaevum_forge/update-signing-keys.json` lists **no key** (`"keys": []`).
What that means for the next releases:

- **A candidate release** (`prepare_release.py --candidate`, as beta.1.2 was)
  needs no signature. It passes the audit unsigned, and users install it from
  a fresh folder or with `--update-target`. Its Forge embeds the empty list, so
  it refuses every later update ZIP: the release after it must be installed
  the same way.
- **A final (non-candidate) release** must be signed. `prepare_release.py`
  refuses it without `--signing-key`, and `audit_release.py` checks the
  signature against the embedded keys, so with an empty list no final package
  can pass the audit.
- The keys are compiled into the Forge. A Forge built **before** the key is
  committed can never accept a signed update, so the key must be committed
  before the release Forge is built.

Owner steps, once, before building a signed release Forge (no key is generated
by the tooling on its own):

1. On the publishing machine, outside the repository:
   `python tools/triaevum_release/release_signature.py generate-key --private-key <path outside the repository> --add`
   (optionally with `TRIAEVUM_RELEASE_SIGNING_PASSPHRASE` set to encrypt the key
   file). The private key never enters the repository or a package; the tool
   refuses a path inside either.
2. Back up the private key and its passphrase in a second safe place.
3. Commit the updated `update-signing-keys.json` (public key only) on the
   release line, then build the Forge and the package from that commit.
4. Release with `--signing-key <path>` (or `TRIAEVUM_RELEASE_SIGNING_KEY`), and
   check that `release_signature.py verify --package <package>` and the release
   audit pass, and that the package receipt names the key id.

## What Forge refuses

- No signature, or a key this Forge does not embed: "This update is not signed
  by the publisher" (`update.unsigned`, `update.bad_signature`). Nothing is
  written.
- A signature that does not verify under a trusted key: always refused.
- An older package than the installed one: asked first (decision 10), as for
  Update in place (WP-04A).
- Developer override, impossible to trigger by accident: the environment
  variable `TRIAEVUM_FORGE_ALLOW_UNSIGNED_UPDATE=unsigned-developer-package`
  **and** the request flag (`update --allow-unsigned-developer-package`, job
  field `allow_unsigned`). The result reports
  `authenticity: unsigned_developer_override`. A bad signature by a trusted
  key is still refused.

## Updating from beta.1.2

The Python Forge of beta.1.2 has no Update button. Extract the new release
anywhere, then run its Forge against the old folder:

```
<new folder>\TriAevumForge.exe --update-target <old folder>
<new folder>\forge\triaevum-forge.exe update --update-target <old folder>
```

The new package is the source; its own Forge checks its signature with the keys
it embeds (whoever runs an executable trusts it already).

## How the update runs (UX plan WP-04B)

1. **Plan** without writing: one package folder in the ZIP, signature, manifest
   members exactly the archive's, target and distribution model, the installed
   revision in the new catalogue, no running game or held package file, free
   space for the staged files plus the old files kept until commit.
2. **Stage** into `.update-<version>-<pid>/package` inside the installation,
   hashing every file; the catalogue's title-module and runtime bindings are
   checked; the package's `forge/triaevum-update-helper.exe` is copied beside it.
3. **Hand off**: an exclusive `proposal.json`, the helper started with an
   inherited handle of Forge, its readiness acknowledgement, then Forge exits.
4. The helper waits for Forge to end, takes the update gate
   (`data/.forge-activation/update-gate.lock`: shared by install, refresh,
   repair, restore and cleanup, exclusive for the helper), takes the
   installation and game-session locks, rechecks every file, then creates
   `.forge-update/journal.json`. It moves the root `TriAevum.exe` aside first
   (a legacy runtime knows nothing of the journal), keeps every replaced file's
   old bytes in `.forge-update/old/`, backs up the activation files, and runs
   the new engine's `refresh` and `status`. Any failure before the commit puts
   the old, validated version back; afterwards only the cleanup remains.
   Nothing is ever killed: "Close TriAevum before updating".
5. While the journal exists, Forge's Play and `TriAevum.exe` itself refuse to
   start ("An update is in progress; open Forge"). Forge's Repair, or
   `triaevum-forge update --recover`, or the helper started on its own from the
   `.update-*` folder, finishes or undoes an interrupted update.
6. Forge reopens with `--update-result` and shows "Updated to x.y. Your saves and
   settings were kept." (or what was rolled back, and why).
