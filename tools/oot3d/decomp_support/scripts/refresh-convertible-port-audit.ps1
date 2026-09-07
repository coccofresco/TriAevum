param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$Python = "python"

try {
    if ($SkipBuild) {
        & $Python "scripts\structured_c_match_gate.py" "--skip-build"
    } else {
        & $Python "scripts\structured_c_match_gate.py"
    }

    & $Python "scripts\audit_convertible_ports.py"
    & $Python "scripts\audit_c_conversion_readiness.py"
    & $Python "scripts\apply_direct_shape_anchors.py"
    & $Python "scripts\audit_c_reconstruction_frontier.py"

    Write-Host "Convertible port audit refreshed."
} catch {
    Write-Error $_
    exit 1
}
