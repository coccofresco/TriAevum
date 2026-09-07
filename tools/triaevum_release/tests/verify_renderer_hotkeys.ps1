param(
    [Parameter(Mandatory=$true)][string]$Exe,
    [Parameter(Mandatory=$true)][string]$Profile,
    [string]$State,
    [string]$Overrides,
    [Parameter(Mandatory=$true)][string]$Output,
    [switch]$Boot,
    [switch]$Stress
)
$ErrorActionPreference = 'Stop'
if (($Boot -and $State) -or (!$Boot -and !$State)) { throw 'Choose State or Boot.' }
$exePath = (Resolve-Path -LiteralPath $Exe).Path
if (Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exePath }) {
    throw 'Close the existing game before running the isolated hotkey test.'
}
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class RendererHotkeyTest {
    private delegate bool EnumWindowCallback(IntPtr hwnd, IntPtr data);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowCallback callback, IntPtr data);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetClassName(IntPtr hwnd, StringBuilder value, int length);
    public static IntPtr FindGameWindow(uint pid) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hwnd, data) => {
            uint owner; GetWindowThreadProcessId(hwnd, out owner);
            if (owner != pid) return true;
            var name = new StringBuilder(256); GetClassName(hwnd, name, name.Capacity);
            if (name.ToString() != "SDL_app") return true;
            found = hwnd; return false;
        }, IntPtr.Zero);
        return found;
    }
    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool PostMessage(IntPtr hwnd, uint msg, UIntPtr key, IntPtr data);
}
'@
$runner = Join-Path $PSScriptRoot 'benchmark_gameplay.mjs'
$arguments = @($runner, "exe=$exePath", "profile=$Profile", "output=$Output",
    'frames=1000', 'warmup=60', 'timeout=140', 'seconds=125', 'mode=play', 'vsync=true',
    'menu=true', 'capture=true', 'captureStart=90', 'captureEvery=90',
    'diagnostics=true', 'validation=true', 'diagnosticFrames=1000')
if ($Boot) { $arguments += 'boot=true' } else { $arguments += "state=$State" }
if ($Overrides) { $arguments += "overrides=$Overrides" }
if ($Stress) { $arguments += @('frames=0', 'seconds=100', 'diagnosticFrames=12000', 'captureEvery=180') }
$quotedArguments = $arguments | ForEach-Object { '"' + $_ + '"' }
$runnerProcess = Start-Process -FilePath (Get-Command node).Source -ArgumentList $quotedArguments `
    -WindowStyle Hidden -PassThru
$game = $null
$gameWindow = [IntPtr]::Zero
$deadline = (Get-Date).AddSeconds(145)
function Wait-Capture([int]$frame) {
    $file = Join-Path $Output ('framebuffer_{0:D6}.bmp' -f $frame)
    while (!(Test-Path -LiteralPath $file)) {
        $runnerProcess.Refresh()
        if ($runnerProcess.HasExited -or (Get-Date) -gt $deadline) { throw "Missing capture $file" }
        Start-Sleep -Milliseconds 100
    }
}
function Send-Key([int]$key, [int]$scan, [bool]$up = $false, [bool]$repeat = $false) {
    $message = if ($up) { 0x0101 } else { 0x0100 }
    [long]$data = 1 -bor ($scan -shl 16)
    if ($up) { $data = $data -bor 0xC0000000L }
    elseif ($repeat) { $data = $data -bor 0x40000000L }
    if (![RendererHotkeyTest]::PostMessage($gameWindow, $message, [UIntPtr]::new([uint32]$key), [IntPtr]::new($data))) {
        throw 'Unable to inject renderer shortcut into the test window.'
    }
}
try {
    Wait-Capture 90
    $game = Get-Process | Where-Object { $_.Path -eq $exePath } | Select-Object -First 1
    if (!$game) { throw 'Test game process missing.' }
    $gameWindow = [RendererHotkeyTest]::FindGameWindow($game.Id)
    if ($gameWindow -eq [IntPtr]::Zero) { throw 'Test SDL game window missing.' }
    Write-Output "Test SDL window: $gameWindow (PID $($game.Id)), process main window: $($game.MainWindowHandle)"
    if ($Stress) {
        $presses = 0
        while (!$runnerProcess.HasExited -and (Get-Date) -lt $deadline) {
            $game.Refresh()
            if ($game.HasExited) { break }
            Send-Key 0x71 0x3C
            Send-Key 0x71 0x3C -up $true
            ++$presses
            Start-Sleep -Milliseconds (110 + ($presses % 7) * 23)
            $runnerProcess.Refresh()
        }
        $runnerProcess.WaitForExit(10000) | Out-Null
        $runnerProcess.Refresh()
        if (!$runnerProcess.HasExited -or $runnerProcess.ExitCode -ne 0) { throw 'F2 stress game run failed.' }
        & node (Join-Path $PSScriptRoot 'verify_renderer_hotkey_frames.mjs') $Output stress
        if ($LASTEXITCODE -ne 0) { throw 'F2 stress rendering failed.' }
        Write-Output "Stress completed: $presses physical F2 presses. Evidence: $Output"
        return
    }
    Wait-Capture 180
    Send-Key 0x71 0x3C
    for ($i = 0; $i -lt 3; ++$i) { Send-Key 0x71 0x3C -repeat $true }
    Wait-Capture 360
    Send-Key 0x71 0x3C -up $true
    # A tap shorter than the guest tick must still restore the configured rendering.
    Send-Key 0x71 0x3C
    Send-Key 0x71 0x3C -up $true
    Wait-Capture 540
    Send-Key 0x70 0x3B
    Start-Sleep -Milliseconds 350
    Send-Key 0x70 0x3B -up $true
    Wait-Capture 630
    Send-Key 0x71 0x3C
    Send-Key 0x71 0x3C -up $true
    Wait-Capture 810
    Send-Key 0x70 0x3B
    Start-Sleep -Milliseconds 350
    Send-Key 0x70 0x3B -up $true
    Send-Key 0x71 0x3C
    Send-Key 0x71 0x3C -up $true
    $runnerProcess.WaitForExit(40000) | Out-Null
    $runnerProcess.Refresh()
    if (!$runnerProcess.HasExited -or $runnerProcess.ExitCode -ne 0) { throw 'Bounded game run failed.' }
    & node (Join-Path $PSScriptRoot 'verify_renderer_hotkey_frames.mjs') $Output
    if ($LASTEXITCODE -ne 0) { throw 'Renderer did not execute the expected F2 states.' }
    Write-Output "Hotkey test completed. Inspect framebuffer captures and Vulkan diagnostics in $Output"
} finally {
    if ($game) {
        $game.Refresh()
        if (!$game.HasExited) {
            $game.CloseMainWindow() | Out-Null
            if (!$game.WaitForExit(5000)) { $game.Kill() }
        }
    }
    $runnerProcess.Refresh()
    if (!$runnerProcess.HasExited) { $runnerProcess.Kill() }
}
