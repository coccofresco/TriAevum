[CmdletBinding()]
param(
    [string]$ClangFormat = "clang-format-14"
)

$ErrorActionPreference = "Stop"
$repoRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$roots = @(
    (Join-Path $repoRoot "tools\oot3d"),
    (Join-Path $repoRoot "runtime\three_ds_recomp")
)

$files = Get-ChildItem -LiteralPath $roots -Recurse -File |
    Where-Object {
        $_.Extension -in @(".c", ".cpp", ".h", ".hpp") -and
        $_.FullName -notmatch '[\\/](generated|third_party|extern)[\\/]'
    }

foreach($file in $files) {
    & $ClangFormat -i $file.FullName
    if($LASTEXITCODE -ne 0) {
        throw "clang-format failed for $($file.FullName)"
    }
}
