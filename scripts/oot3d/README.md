# OoT3D Recomp Scripts

These scripts build, run, and validate the native OoT3D product. Run them from
the repository root in PowerShell.

## Product Build

```powershell
.\scripts\oot3d\Configure-Oot3dWholeAotProduct.ps1 -Force
.\scripts\oot3d\Build-Oot3dWholeAotProduct.ps1
```

The product target is `oot3d_native_game`. The retained A32 implementation is
a game-code execution path, not a dependency on the Ship of Harkinian product.

## Run

```powershell
.\scripts\oot3d\Invoke-Oot3dNativeGame.ps1
```

Use the script parameters for backend, profile, savestate, and diagnostics.
Generated builds, captures, extracted game data, and savestates remain outside
version control.

## Validation

```powershell
.\scripts\oot3d\Test-Oot3dOperationalInputs.ps1
.\scripts\oot3d\Invoke-Oot3dNativeGameRendererParity.ps1
.\scripts\oot3d\Compare-Oot3dTitleIntroRendererParity.ps1
python tools\oot3d\renderer_integration\audit_renderer_dependencies.py --policy tools\oot3d\renderer_integration\renderer_dependency_policy.json
```

Historical scripts that installed `.o2r` replacement packages into `soh.exe`
were removed. Their implementation remains available in Git history.
