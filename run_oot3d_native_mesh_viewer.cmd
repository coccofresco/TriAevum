@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\oot3d\Invoke-Oot3dNativeMeshViewer.ps1" -Launch
if errorlevel 1 (
    echo.
    echo OOT3D native mesh viewer failed. See the output above.
    pause
    exit /b %errorlevel%
)
