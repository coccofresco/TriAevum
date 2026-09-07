param(
    [Parameter(Mandatory=$true)][string]$Capture,
    [string]$RootSymbol = 'TriAevum!main',
    [string]$Output
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $Capture).Path)
function Read-Entry([string]$Name) {
    $reader = [IO.StreamReader]::new($archive.GetEntry($Name).Open())
    try { $reader.ReadToEnd() } finally { $reader.Dispose() }
}
try {
    $symbols = @{}
    (Read-Entry 'Symbols.txt') -split "`n" | ForEach-Object {
        if ($_ -match '^(0x\w+) "([^"]*)" "([^"]*)"') {
            $symbols[$matches[1]] = $matches[2] + '!' + $matches[3]
        }
    }
    $exclusive = @{}
    $inclusive = @{}
    $total = 0.0
    (Read-Entry 'Callstacks.txt') -split "`n" | ForEach-Object {
        $parts = $_.Trim() -split ' '
        if ($parts.Length -gt 1) {
            $weight = [double]::Parse($parts[0], [Globalization.CultureInfo]::InvariantCulture)
            $stack = @($parts[1..($parts.Length-1)] | ForEach-Object { $symbols[$_] })
            if ($stack -contains $RootSymbol) {
                $total += $weight
                $exclusive[$stack[0]] += $weight
                $stack | Select-Object -Unique | ForEach-Object { $inclusive[$_] += $weight }
            }
        }
    }
    if ($total -le 0) { throw "No samples rooted at $RootSymbol" }
    function Get-Rows($Counts) {
        @($Counts.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object {
            [ordered]@{ symbol=$_.Name; sampledSeconds=$_.Value; percent=100.0*$_.Value/$total }
        })
    }
    $result = [ordered]@{
        capture=(Resolve-Path -LiteralPath $Capture).Path
        stats=(Read-Entry 'Stats.txt').Trim()
        rootSymbol=$RootSymbol
        sampledSeconds=$total
        warning='Statistical attribution, not benchmark FPS. Inclusive rows overlap. Unavailable symbols may resolve to the nearest exported name.'
        exclusive=(Get-Rows $exclusive)
        inclusive=(Get-Rows $inclusive)
    }
    $json = $result | ConvertTo-Json -Depth 6
    if ($Output) { [IO.File]::WriteAllText($Output, $json) }
    $result.exclusive | Select-Object -First 30 | ForEach-Object {
        '{0,6:F2}% {1,7:F3}s {2}' -f $_.percent,$_.sampledSeconds,$_.symbol
    }
} finally { $archive.Dispose() }
