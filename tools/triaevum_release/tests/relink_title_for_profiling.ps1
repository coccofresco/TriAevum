param(
    [Parameter(Mandatory=$true)][string]$SourceArchive,
    [Parameter(Mandatory=$true)][string]$Generated,
    [Parameter(Mandatory=$true)][string]$Archive,
    [Parameter(Mandatory=$true)][string]$Support,
    [Parameter(Mandatory=$true)][string]$Compiler,
    [Parameter(Mandatory=$true)][string]$Sysroot,
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$LtoCache,
    [ValidateRange(1,4)][int]$Jobs = 3
)
$ErrorActionPreference = 'Stop'
$Output = [IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $Output) { throw 'Use a new diagnostic output directory.' }
if (!$LtoCache) { $LtoCache = Join-Path $Output 'thinlto-cache' }
$LtoCache = [IO.Path]::GetFullPath($LtoCache)
$inputs = [ordered]@{}
foreach ($name in @('SourceArchive','Archive','Support','Compiler')) {
    $file = (Resolve-Path -LiteralPath (Get-Variable $name -ValueOnly)).Path
    Set-Variable $name $file
    $inputs[$name] = @{path=$file; sha256=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash}
}
$Generated = (Resolve-Path -LiteralPath $Generated).Path
$Sysroot = (Resolve-Path -LiteralPath $Sysroot).Path
$manifest = Get-Content -LiteralPath "$Sysroot/sysroot.json" -Raw | ConvertFrom-Json
$sdk = $manifest.sdk_version
$compatibility = '19.' + ($manifest.msvc_version -split '\.')[1]
[IO.Directory]::CreateDirectory($Output) | Out-Null
[IO.Directory]::CreateDirectory($LtoCache) | Out-Null
Expand-Archive -LiteralPath $SourceArchive -DestinationPath "$Output/source"
$runtime = "$Output/source/tools/oot3d/native_game_runtime"
$a32 = "$Output/source/tools/oot3d/native_a32_runtime"
$wrapper = "$runtime/triaevum_title_whole_aot_plugin.cpp"
if (!(Test-Path -LiteralPath "$a32/upstream/recomp/a32_runtime.h")) {
    throw 'Source archive does not contain the paired A32 headers.'
}
$common = @('--target=x86_64-pc-windows-msvc', '/nologo', '/MT',
    '/vctoolsdir', "$Sysroot/msvc", '/winsdkdir', "$Sysroot/sdk", '/winsdkversion', $sdk,
    "-resource-dir=$Sysroot/clang", "-fms-compatibility-version=$compatibility")
$compile = $common + @('/TP','/EHsc','/O2','/Ob2','/DNDEBUG','/std:c++20',
    '/bigobj','/fp:strict','/w','/Zc:preprocessor','/Brepro','/DNOMINMAX',
    '/DOOT3D_NATIVE_GENERATED_WHOLE_AOT=1',"/I$Generated","/I$runtime","/I$a32","/I$a32/upstream",
    '/c',"/Fo$Output/wrapper.obj",$wrapper)
$link = $common + @('/LD',"/Fe$Output/triaevum_title_aot.dll","$Output/wrapper.obj",
    $Archive,$Support,'-fuse-ld=lld','/link','/NOIMPLIB','/NOEXP','/OPT:REF','/OPT:ICF',
    '/INCREMENTAL:NO','/Brepro',"/MAP:$Output/title.map",'/DEBUG:FULL',"/PDB:$Output/title.pdb",
    "/OPT:LLDLTOJOBS=$Jobs","/threads:$Jobs","/lldltocache:$LtoCache")
$watch = [Diagnostics.Stopwatch]::StartNew()
& $Compiler @compile
if ($LASTEXITCODE -ne 0) { throw 'Diagnostic wrapper compilation failed.' }
Write-Output "Linking cached title objects, maximum $Jobs workers. No generated source recompilation."
& $Compiler @link
if ($LASTEXITCODE -ne 0) { throw 'Diagnostic title link failed.' }
$receipt = [ordered]@{inputs=$inputs; generated=$Generated; sysroot=$Sysroot; ltoCache=$LtoCache;
    compileArguments=$compile; linkArguments=$link; seconds=$watch.Elapsed.TotalSeconds;
    outputSha256=(Get-FileHash "$Output/triaevum_title_aot.dll" -Algorithm SHA256).Hash;
    diagnosticOnly=$true}
[IO.File]::WriteAllText("$Output/relink.json",($receipt | ConvertTo-Json -Depth 6))
Write-Output "Diagnostic DLL/map/PDB ready in $Output ($($watch.Elapsed.TotalSeconds) seconds)."
