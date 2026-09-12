# Read-only loader diagnostics. No ROM, GPU initialization or game execution.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Installation,
    [string]$Output = (Join-Path $PWD 'TriAevum-loader-report.json')
)
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) {
    throw 'Run this script using 64-bit Windows PowerShell.'
}
$root = (Resolve-Path -LiteralPath $Installation).Path
if (-not (Test-Path -LiteralPath (Join-Path $root 'TriAevum.exe'))) {
    throw 'Installation must be the folder containing TriAevum.exe.'
}
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class TriAevumLoaderProbe {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll", EntryPoint="GetProcAddress", CharSet=CharSet.Ansi, ExactSpelling=true)]
    public static extern IntPtr GetNamedExport(IntPtr module, string name);
    [DllImport("kernel32.dll", EntryPoint="GetProcAddress", ExactSpelling=true)]
    public static extern IntPtr GetOrdinalExport(IntPtr module, IntPtr ordinal);
    [DllImport("kernel32.dll")]
    public static extern bool FreeLibrary(IntPtr module);
    [DllImport("kernel32.dll")]
    public static extern uint SetErrorMode(uint mode);
}
"@
function Read-Imports([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    function U16([int]$o) { [BitConverter]::ToUInt16($bytes, $o) }
    function U32([int]$o) { [BitConverter]::ToUInt32($bytes, $o) }
    function ZString([int]$o) {
        $end = $o
        while ($end -lt $bytes.Length -and $bytes[$end] -ne 0) { $end++ }
        if ($end -eq $bytes.Length) { throw 'Unterminated PE string' }
        [Text.Encoding]::ASCII.GetString($bytes, $o, $end - $o)
    }
    if ((U16 0) -ne 0x5A4D) { throw 'Not a PE image' }
    $pe = [int](U32 0x3c)
    if ((U32 $pe) -ne 0x4550) { throw 'Invalid PE signature' }
    $optional = $pe + 24
    if ((U16 $optional) -ne 0x20b) { throw 'Expected a 64-bit PE image' }
    $count = U16 ($pe + 6)
    $sectionStart = $optional + (U16 ($pe + 20))
    $headers = U32 ($optional + 60)
    function Offset([uint32]$rva) {
        if ($rva -lt $headers) { return [int]$rva }
        for ($s = 0; $s -lt $count; $s++) {
            $base = $sectionStart + 40 * $s
            $va = U32 ($base + 12)
            $size = U32 ($base + 16)
            if ($rva -ge $va -and $rva -lt ([uint64]$va + $size)) {
                return [int]((U32 ($base + 20)) + $rva - $va)
            }
        }
        throw "Unmapped PE RVA $rva"
    }
    $importRva = U32 ($optional + 120)
    if (-not $importRva) { return }
    $descriptor = Offset $importRva
    $descriptorStart = $descriptor
    for ($d = 0; $d -lt 4096; $d++) {
        $descriptor = $descriptorStart + 20 * $d
        $name = U32 ($descriptor + 12)
        if (-not $name) { return }
        $dll = ZString (Offset $name)
        $thunk = U32 $descriptor
        if (-not $thunk) { $thunk = U32 ($descriptor + 16) }
        $at = Offset $thunk
        $symbols = @()
        $thunkStart = $at
        for ($t = 0; $t -lt 65536; $t++) {
            $at = $thunkStart + 8 * $t
            $low = U32 $at
            $high = U32 ($at + 4)
            if (-not $low -and -not $high) { break }
            if ($high -band 0x80000000) { $symbols += "#$($low -band 65535)" }
            else { $symbols += ZString ((Offset $low) + 2) }
        }
        if ($t -eq 65536) { throw 'Unterminated PE import thunks' }
        [pscustomobject]@{dll=$dll; symbols=$symbols}
    }
    throw 'Unterminated PE import descriptors'
}
$report = [ordered]@{
    format='triaevum_windows_loader_diagnostic_v1'
    windows=[Environment]::OSVersion.VersionString
    powershell=$PSVersionTable.PSVersion.ToString()
    scope='Static eager imports of packaged EXE/root DLLs against local/system providers. Not a GPU capability test or an exact Windows loader trace. Delayed and dynamic imports are excluded.'
    modules=@()
    problems=@()
}
# Prevent error dialogs. DONT_RESOLVE_DLL_REFERENCES maps providers without
# executing their entry points; returned function addresses are NEVER called.
$previous = [TriAevumLoaderProbe]::SetErrorMode(0x8003)
try {
    $files = @((Get-Item -LiteralPath (Join-Path $root 'TriAevum.exe')))
    $files += @(Get-ChildItem -LiteralPath $root -Filter '*.dll' -File)
    foreach ($file in $files) {
        $sha = [Security.Cryptography.SHA256]::Create()
        $stream = [IO.File]::OpenRead($file.FullName)
        try { $hash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
        finally { $stream.Dispose(); $sha.Dispose() }
        $module = [ordered]@{name=$file.Name; sha256=$hash; imports=@()}
        foreach ($import in @(Read-Imports $file.FullName)) {
            $local = Join-Path $root $import.dll
            $system = Join-Path ([Environment]::SystemDirectory) $import.dll
            $provider = if (Test-Path -LiteralPath $local) { $local }
                elseif (Test-Path -LiteralPath $system) { $system } else { $import.dll }
            $handle = [TriAevumLoaderProbe]::LoadLibraryExW($provider, [IntPtr]::Zero, 1)
            $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
            $entry = [ordered]@{dll=$import.dll; provider=$provider; checked=$import.symbols.Count; missing=@(); load_error=$null}
            if (Test-Path -LiteralPath $provider) {
                $entry.file_version = [Diagnostics.FileVersionInfo]::GetVersionInfo($provider).FileVersion
            }
            try {
                if ($handle -eq [IntPtr]::Zero) {
                    $entry.load_error = $errorCode
                    $report.problems += "$($file.Name): cannot map $($import.dll) (Win32 $errorCode)"
                } else {
                    foreach ($symbol in $import.symbols) {
                        $address = if ($symbol.StartsWith('#')) {
                            [TriAevumLoaderProbe]::GetOrdinalExport($handle, [IntPtr][int]$symbol.Substring(1))
                        } else { [TriAevumLoaderProbe]::GetNamedExport($handle, $symbol) }
                        if ($address -eq [IntPtr]::Zero) {
                            $entry.missing += $symbol
                            $report.problems += "$($file.Name) requires $($import.dll)!$symbol (not resolved)"
                        }
                    }
                }
            } finally {
                if ($handle -ne [IntPtr]::Zero) { $null = [TriAevumLoaderProbe]::FreeLibrary($handle) }
            }
            $module.imports += $entry
        }
        $report.modules += $module
    }
} catch {
    $report.problems += "Diagnostic incomplete: $($_.Exception.Message)"
} finally {
    $null = [TriAevumLoaderProbe]::SetErrorMode($previous)
    $report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Output -Encoding UTF8
}
Write-Host "Report: $Output"
Write-Host 'Review paths for personal information before sharing this JSON. No ROM or save data is included.'
if ($report.problems.Count) {
    $report.problems | ForEach-Object { Write-Host $_ }
    exit 1
}
Write-Host 'Checked imports resolved. This does not certify GPU/driver compatibility.'
