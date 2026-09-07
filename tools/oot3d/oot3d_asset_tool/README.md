# OoT3D Asset Tool

`oot3d-asset-tool` parses, audits, indexes, and exports native Ocarina of Time
3D assets outside the game runtime. It consumes files extracted locally from a
user-provided RomFS and never includes copyrighted game data in this repository.

## Install

```powershell
python -m pip install -e tools\oot3d\oot3d_asset_tool
oot3d-assets --help
```

The native paths cover CMB models, CSAB animation, CMAB material animation,
ZSI scenes, QDB cutscenes, CTXB textures, collision, environment data, and Room
Compilation Units.

Some older subcommands can still emit Fast resource XML or `.o2r` archives for
regression comparison. Those are isolated legacy export adapters, not runtime
dependencies and not the preferred input path for the native renderer.

## Tests

```powershell
python -m unittest discover -s tools\oot3d\oot3d_asset_tool\tests
```

Generated assets and extracted game files must be written outside the repo.
