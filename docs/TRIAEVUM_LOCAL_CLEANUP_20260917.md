# Local cleanup inventory, 2026-09-17

## Expanded cleanup completed later on September 17

The initial user-operated audit below is historical. A subsequent, broader
cleanup was executed directly using
`scripts/triaevum/cleanup-expanded-local-20260917.ps1`.

| Drive | Deleted logical GiB | Free GiB after cleanup |
| --- | ---: | ---: |
| C: | 0.36 | 7.53 |
| I: | 17.76 | 19.68 |
| J: | 1.65 | 4.57 |

Total approximately 19.78 GiB logical, across three deletion batches. Actual
allocated space differs slightly because some historical diagnostics were
compressed and other processes can write concurrently.

Removed only allowlisted obsolete build intermediates/archives/debug symbols,
historical BMP captures, and large raw diagnostic traces. In particular,
`oot3dre_work/native_game` contains old July/August traces that the initial
object-only cleanup missed. Historical numerical trace files selected here
are no longer available; small reports, configuration, code and Ghidra exports
remain. No repository was recursively removed.

The historical J: title cache contributed 1109 object/archive files (1.65 GiB).
It is absent from the active CMake cache/build graph; its plugin DLLs, generated
source and metadata remain. Reusing that old cache requires regeneration of
its removed build products, not restoration of the current runtime.

Exact file manifests, including sizes and timestamps, are in the user's TEMP:

- `triaevum-expanded-cleanup-20260917-135246.csv` (1.701 GiB)
- `triaevum-expanded-cleanup-20260917-135723.csv` (16.421 GiB)
- `triaevum-expanded-cleanup-20260917-135839.csv` (1.653 GiB)

Post-cleanup existence checks passed for the current TriAevum executable,
baseline title DLL, active Ninja build, shared NRI source and the Hyrule Field,
Sages and user-created race checkpoints. No ROMs, saved games, source code,
current issue captures, current shader corpus or current build objects were
selected. This was a filesystem/dependency check, not a fresh gameplay test.

The broad audit was narrowed to known project-product scopes rather than
deleting directories by name. Remaining space on C:/J: is not claimed to be
entirely necessary or exhaustively audited.

## Earlier limited audit

This is a machine-specific, user-operated cleanup, not a release script.
No deletion was performed during the audit. The deletion tool was previously
blocked; the user requested an inspected command to execute personally.

## Inspected selection

`scripts/triaevum/cleanup-local-obsolete-20260917.ps1` defaults to preview.
Preview with `-IncludeOldCaptures` selected:

| Drive | Files | Logical GiB |
| --- | ---: | ---: |
| C: | 2518 | 5.19 |
| I: | 1236 | 3.27 |
| J: | 210 | 0.79 |

Approximately 9.25 GiB logical total. Actual freed disk allocation can differ.
The full inventory is outside Git at
`C:/Users/xander/AppData/Local/Temp/triaevum-cleanup-manifest-20260917-105957.csv`.
Each invocation regenerates a dated inventory before doing anything destructive.

Selection includes old diagnostic object/archive variants, object intermediates
in explicitly named inactive build trees, and optionally historical BMP captures.
Removing objects makes those old experiments rebuild if used again. Removing
captures loses historical images, but keeps numerical reports and reproduction
inputs. This is not a claim that every unselected directory is necessary.

## Preserved

- Active `J:/TriAevum-verify-20260910/runtime` and
  `I:/TriAevum-public/ui-dependencies`.
- Shared dependencies in `I:/oot3dre_work/whole-aot-product-consumer/_deps`;
  verified against the active CMake cache, not inferred from directory dates.
- Git repositories, external decompilation, source, compiler and SDK directories.
- ROMs, extracted data, shader caches/corpora, DLLs, saves and settings.
- Current issue evidence in `C:/Users/xander/triaevum-issues-40-43` and mounted
  checkpoint directory `C:/Users/xander/triaevum-verify-20260911/epona-user`.
- AOT recovery experiments and their surviving source/evidence.
- Baseline archive `1110f696861ab75ecd0e9a489e78c57b1919b957511cd9ae7d304c43bd933699`
  and all 257 objects declared by its build record, even though stored in TEMP.
- The separate `I:/TriAevum-aot-lab-cache/native-objects` (4.223 GiB) is not
  selected without resolving its retained experimental dependencies.

## Execution

Close game/Forge/builds, then invoke from PowerShell:

```powershell
& 'I:\TriAevum-public\source-repository-clean\scripts\triaevum\cleanup-local-obsolete-20260917.ps1' -Apply -IncludeOldCaptures
```

Omit `-Apply` for preview. Omit `-IncludeOldCaptures` to retain historical images.
The script rejects linked paths, protects current work, only selects files
last written before September 17, and checks size/timestamp again before removal.
It removes individual literal files, not recursively selected project trees.
If baseline metadata is unavailable or invalid it fails rather than deleting
unclassified baseline objects. Source and build directories themselves remain.
