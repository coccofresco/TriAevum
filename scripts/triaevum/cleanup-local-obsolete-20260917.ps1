# Local, machine-specific inventory. No deletion unless explicitly invoked with -Apply.
[CmdletBinding()]
param([switch]$Apply, [switch]$IncludeOldCaptures)
$ErrorActionPreference = 'Stop'
$cutoff = [datetime]'2026-09-17T00:00:00'
$scopes = [System.Collections.Generic.List[object]]::new()
function Add-Scope([string]$Path, [string[]]$Extensions, [string]$Reason) {
    $scopes.Add([pscustomobject]@{ Path=$Path; Extensions=$Extensions; Reason=$Reason })
}
$objects = @('.obj', '.o', '.pch', '.ilk')
foreach ($path in @(
    'I:\TriAevum-aot-lab-debug\native-objects',
    'I:\TriAevum-aot-lab-o3\native-objects',
    'C:\Users\xander\AppData\Local\Temp\triaevum-ui-title-cache\native-objects'
)) { Add-Scope $path ($objects + '.lib') 'Regenerable diagnostic AOT objects; keep generated source and plugin DLLs' }
foreach ($path in @(
    'I:\TriAevum-pica-build\CMakeFiles',
    'I:\oot3dre-baseline-97c\build-codex',
    'I:\oot3dre-vulkan\build-renderer-gate0',
    'I:\oot3dre_work\triaevum-direct-module-build\CMakeFiles',
    'I:\oot3dre_work\triaevum-game-module-build\CMakeFiles',
    'I:\oot3dre_work\triaevum-game-module-build-release\CMakeFiles',
    'I:\oot3dre_work\triaevum-module-build-release\CMakeFiles',
    'I:\oot3dre_work\shader-cache-windows\CMakeFiles'
)) { Add-Scope $path $objects 'Inactive build intermediates; keep dependencies, executables and source' }
$temp = 'C:\Users\xander\AppData\Local\Temp'
foreach ($dir in Get-ChildItem -LiteralPath $temp -Directory -Filter 'TriAevum-*') {
    if ($dir.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
    Add-Scope $dir.FullName $objects 'Old project temporary compilation products'
    if ($IncludeOldCaptures) { Add-Scope $dir.FullName @('.bmp') 'Old temporary framebuffer captures (not saves)' }
}
if ($IncludeOldCaptures) {
    foreach ($dir in Get-ChildItem -LiteralPath 'I:\oot3dre_work' -Directory) {
        if ($dir.Name -match '^(grass-|audio-costs-|f2-|freecam-|canonical-costs-|native-outline-)' -and
            -not ($dir.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            Add-Scope $dir.FullName @('.bmp') 'Historical renderer test captures; keep numerical reports and inputs'
        }
    }
    foreach ($path in @(
        'C:\Users\xander\triaevum-verify-20260911',
        'J:\TriAevum-verify-20260910',
        'J:\TriAevum-diagnostics',
        'I:\TriAevum-diagnostics'
    )) { Add-Scope $path @('.bmp') 'Historical captures; preserve reports, configuration and checkpoints' }
}
$protected = @(
    'J:\TriAevum-verify-20260910\runtime',
    'C:\Users\xander\triaevum-verify-20260911\epona-user'
)
$baseline = Get-Content -LiteralPath 'I:\TriAevum-aot-lab-evidence\baseline\whole-aot-plugin.json' -Raw | ConvertFrom-Json
$objectRoot = 'C:\Users\xander\AppData\Local\Temp\triaevum-ui-title-cache\native-objects'
$archive = Join-Path $objectRoot ('archives\' + $baseline.archive_cache_key)
$record = Get-Content -LiteralPath ($archive + '.build.json') -Raw | ConvertFrom-Json
if (-not $record.objects -or $record.archive_cache_key -ne $baseline.archive_cache_key) { throw 'Cannot establish baseline object protection' }
$keep = @{}
$keep[$archive + '.lib'] = $true
foreach ($key in $record.objects) {
    if ($key -notmatch '^[a-f0-9]{64}$') { throw 'Invalid baseline object key' }
    $keep[(Join-Path $objectRoot ('objects\' + $key.Substring(0,2) + '\' + $key + '.obj'))] = $true
}
function Test-Within([string]$Path, [string]$Root) {
    return $Path.Equals($Root, [StringComparison]::OrdinalIgnoreCase) -or
        $Path.StartsWith($Root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)
}
function Assert-NoLink([string]$Path) {
    $cursor = $Path
    while ($cursor) {
        $item = Get-Item -LiteralPath $cursor -Force
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked path rejected: $cursor" }
        $parent = [IO.Directory]::GetParent($cursor)
        $cursor = if ($parent) { $parent.FullName } else { $null }
    }
}
$selected = @{}
foreach ($scope in $scopes) {
    if (-not (Test-Path -LiteralPath $scope.Path -PathType Container)) { continue }
    $root = [IO.Path]::GetFullPath($scope.Path).TrimEnd('\')
    Assert-NoLink $root
    $pending = [System.Collections.Generic.Stack[string]]::new()
    $pending.Push($root)
    while ($pending.Count) {
        $dir = $pending.Pop()
        foreach ($item in Get-ChildItem -LiteralPath $dir -Force) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
            $full = [IO.Path]::GetFullPath($item.FullName)
            if (-not (Test-Within $full $root)) { throw "Outside scope: $full" }
            if (@($protected | Where-Object { Test-Within $full $_ }).Count) { continue }
            if ($item.PSIsContainer) {
                if ($item.Name -notin @('.git', '_deps', 'savedata', 'savestates', 'checkpoints')) { $pending.Push($full) }
                continue
            }
            if ($item.Extension -notin $scope.Extensions -or $item.LastWriteTime -ge $cutoff) { continue }
            if ($keep.ContainsKey($full)) { continue }
            $selected[$full] = [pscustomobject]@{
                Path=$full; Root=$root; Bytes=$item.Length
                LastWriteUtc=$item.LastWriteTimeUtc; Reason=$scope.Reason
            }
        }
    }
}
$files = @($selected.Values | Sort-Object Path)
$manifest = Join-Path $env:TEMP ('triaevum-cleanup-manifest-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.csv')
$files | Export-Csv -LiteralPath $manifest -NoTypeInformation -Encoding UTF8
$files | Group-Object { [IO.Path]::GetPathRoot($_.Path) } | ForEach-Object {
    [pscustomobject]@{ Drive=$_.Name; Files=$_.Count; GiB=[math]::Round(($_.Group | Measure-Object Bytes -Sum).Sum/1GB,3) }
} | Format-Table -AutoSize
Write-Host "Manifest: $manifest"
if (-not $Apply) { Write-Host 'Preview only. Use -Apply to delete this selection.'; return }
$busy = @(Get-Process TriAevum,TriAevumForge,clang-cl,ninja,cmake,lld-link -ErrorAction SilentlyContinue)
if ($busy.Count) { throw 'Close game, Forge and build processes before cleanup.' }
$removed = 0L
foreach ($entry in $files) {
    if (-not (Test-Path -LiteralPath $entry.Path)) { continue }
    Assert-NoLink $entry.Path
    if (-not (Test-Within ([IO.Path]::GetFullPath($entry.Path)) $entry.Root)) { throw 'Scope changed' }
    $now = Get-Item -LiteralPath $entry.Path -Force
    if ($now.Length -ne $entry.Bytes -or $now.LastWriteTimeUtc -ne $entry.LastWriteUtc) { throw "File changed: $($entry.Path)" }
    Remove-Item -LiteralPath $entry.Path -Force
    $removed += $entry.Bytes
}
Write-Host ('Deleted logical size: {0:N3} GiB' -f ($removed/1GB))
Get-PSDrive C,I,J | Select-Object Name,Free | Format-Table -AutoSize
