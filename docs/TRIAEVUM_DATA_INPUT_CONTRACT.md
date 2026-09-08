# Container-independent player inputs

## Implemented

Forge can import an extracted directory through `Use extracted data...`, or
the existing install worker's `--rom` argument pointing to that directory.
It requires `code.bin` (decompressed), `exheader.bin` (2048 bytes), and
`romfs.bin` (decrypted IVFC image). The cartridge container is not required.

`extracted_inputs.py` copies and hashes inputs into owned staging. It never
moves or deletes the user's directory. Existing revision matching, offline
adapters, content-addressed storage and precompiled module activation are
reused without compilation. Unrelated files in the selected folder are ignored.
Recursive guessing, ambiguous filenames and source symlinks are not accepted.

Validation: 196 release-tool tests passed. Actual extracted inputs from all
four audited EUR/USA catalogue images select the same existing recipes as
their ROM imports. Those recipes were boot-tested in the preceding catalogue
qualification. This change has not yet been packaged into a new frozen Forge
or separately boot-tested through its GUI; published alpha.1b is unchanged.

## Important correction

"Precompiled" does not currently mean "RomFS assets alone are sufficient".
`forge.verify_sources`, `forge.prepare_content`, the generated process manifest
and `precompiled_titles.install_precompiled_title` still require code and
ExHeader alongside RomFS. The source image may contain address-dependent data
as well as instructions. Ignoring its identity is not a safe generalization.

This step removes the container dependency, not the execution-data dependency.
Encrypted CIA packages still cannot supply readable inputs through this path.

## Next compatibility boundary

1. Inventory actual runtime reads from the source image and separate initialized
   data, lookup tables, relocation/address contracts and executable-byte reads.
2. Define an explicit module-owned execution-data contract from that inventory.
   Do not infer it solely from an ExHeader section boundary.
3. Identify assets structurally by RomFS paths and verified content families;
   preserve regional payloads and use explicit offline adapters where needed.
4. Only relax whole-file identities after the replacement contracts are covered
   by regression tests and real module execution. Unknown data must produce an
   actionable compatibility report, not an unchecked installation.

No additional port or game recompilation should be needed for equivalent data.
No proprietary source data, keys or user assets may be added to the public package.
