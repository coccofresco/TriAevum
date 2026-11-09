# Public-tree privacy sweep

Every file that reaches the public tree (code, tests, docs, scripts, CMake,
JSON) describes the project, never a maintainer machine.
`tools/triaevum_release/privacy_sweep.py` enforces this. It runs in the release
Python suite (`test_privacy_sweep.py`) and on its own:

```
python -I tools/triaevum_release/privacy_sweep.py            # exit 0 clean, 1 findings
python -I tools/triaevum_release/privacy_sweep.py --json     # machine-readable list
```

It refuses:

- absolute paths on local work drives (any drive letter except the system drive
  `C:`, the example drive `D:` and the placeholder drives `X:` and `Z:`);
- Windows profile paths `C:/Users/<name>` and POSIX homes `/home/<name>` that
  name a real account;
- the names of the private work and scratch directories;
- the current OS account name and host name. These are read when the sweep
  runs and are never written into the tree. Extra private terms can be passed
  with `--term` or `TRIAEVUM_PRIVACY_TERMS` (comma separated). A finding prints
  `term#N`, never the term itself;
- private key blocks and RFC 1918 host addresses.

## Placeholders

These placeholders are allowed and stand for locations that exist only on a
maintainer machine. Local tools and the operational-inputs manifest
(`tools/oot3d/operational_inputs.json`) use them; a maintainer substitutes the
real location locally, or passes it through the documented argument or
environment variable.

| Placeholder | Meaning |
| --- | --- |
| `W` | the local MM3D work directory (analysis, receipts, run harness) |
| `<work>` | the local TriAevum work directory |
| `<scratch>` | a local scratch directory for raw temporary files |
| `<oot3d-work>`, `<oot3d-build>` | local OoT3D evidence and build stores |
| `<tools>` | local toolchain store (pinned LLVM, Android tools) |
| `<oot3d-decomp>`, `<legacy-recomp>`, `<ppssppvr>`, `<azahar-pcvr>` | local research checkouts |
| `<public-work>`, `<verify-20260910>`, `<diagnostics>`, `<worktrees>` | local release and verification stores |
| `<local>/...` | any other location on a maintainer drive |
| `C:/Users/<user>`, `/home/<user>`, `%USERPROFILE%`, `$HOME`, `~` | the user's home |
| `X:/...`, `Z:/...` | fake drives used by tests |

Paths used by tools are never built in. For example the CATALOGS lab launcher
takes `--gpu-lock` (default `TRIAEVUM_GPU_LOCK`, else
`<TRIAEVUM_WORK_DIR>/gpu-run.lock`), the LLVM toolchain file needs
`OOT3D_LLVM_ROOT`, and the Azahar coverage generator needs `--decomp-root` (or
`TRIAEVUM_OOT3D_DECOMP_ROOT`) and writes only placeholder paths into its
provenance.

The run harness, GPU lock and build-slot scripts of the maintainer lab are
private lab tooling. They are not shipped and are not part of this repository;
docs that mention them say so.

A line may carry `privacy-sweep: allow` to document a deliberate exception.
`ALLOWED_FILES` lists embedded binary data (font blobs) whose base64 text can
look like a path.
