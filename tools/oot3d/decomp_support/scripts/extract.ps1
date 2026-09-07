param(
    [string]$RomPath = "..\oot3d.cci",
    [string]$CtrTool = "..\tools\ctrtool-v1.3.0\ctrtool.exe",
    [string]$OutDir = ".\work\extract",
    [string]$LogDir = ".\work\logs"
)

$ErrorActionPreference = "Stop"

$RomPath = (Resolve-Path $RomPath).Path
$CtrTool = (Resolve-Path $CtrTool).Path
New-Item -ItemType Directory -Force -Path $OutDir, $LogDir | Out-Null
New-Item -ItemType Directory -Force -Path "$OutDir\contents", "$OutDir\ncch0", "$OutDir\exefs", "$OutDir\romfs" | Out-Null

& $CtrTool --info $RomPath 2>&1 | Tee-Object -FilePath "$LogDir\oot3d_ctrtool_info.txt"
& $CtrTool --contents="$OutDir\contents" $RomPath 2>&1 | Tee-Object -FilePath "$LogDir\extract_contents.txt"

$mainApp = Join-Path $OutDir "contents\00_0004000000033600.app"
& $CtrTool `
    --exheader="$OutDir\ncch0\exheader.bin" `
    --plainrgn="$OutDir\ncch0\plain.bin" `
    --exefs="$OutDir\ncch0\exefs.bin" `
    --romfs="$OutDir\ncch0\romfs.bin" `
    $mainApp 2>&1 | Tee-Object -FilePath "$LogDir\extract_ncch0_parts.txt"

& $CtrTool --decompresscode --exefsdir="$OutDir\exefs" "$OutDir\ncch0\exefs.bin" 2>&1 | Tee-Object -FilePath "$LogDir\extract_exefs.txt"
& $CtrTool --romfsdir="$OutDir\romfs" "$OutDir\ncch0\romfs.bin" 2>&1 | Tee-Object -FilePath "$LogDir\extract_romfs.txt"

$files = Get-ChildItem "$OutDir\romfs" -Recurse -File
$files | Select-Object FullName, Length | ConvertTo-Json -Depth 3 | Set-Content -Encoding UTF8 "$LogDir\romfs_manifest.json"

Write-Host "Extracted RomFS files: $($files.Count)"
