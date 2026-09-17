[CmdletBinding()]
param([switch]$Apply, [switch]$HistoricalTitleCacheOnly)
$ErrorActionPreference='Stop'
$cutoff=[datetime]'2026-09-17'
$scopes=[Collections.Generic.List[object]]::new()
function Add-Scope($Path,$Kind){
 if(Test-Path -LiteralPath $Path -PathType Container){$scopes.Add([pscustomobject]@{Path=[IO.Path]::GetFullPath($Path);Kind=$Kind})}
}
# Only historical build products, not the current runtime or its shared donors.
foreach($d in Get-ChildItem I:/oot3dre_work -Directory){
 if($d.Name -match '^triaevum-.*(build|cache|probe|proof|pilot|smoke)' -and $d.Name -ne 'triaevum-full-title-proof') {Add-Scope $d.FullName 'build'}
 if($d.Name -match '^(grass-|outline-|native-outline-|audio-costs-|native-costs-|native-performance-|f2-|freecam-|canonical-costs-)'){Add-Scope $d.FullName 'trace'}
}
foreach($p in @('I:/oot3dre-baseline-97c/build-codex','I:/oot3dre-vulkan/build-renderer-gate0','I:/TriAevum-pica-build','I:/oot3dre_work/shader-cache-windows')){Add-Scope $p 'build'}
foreach($p in @('I:/oot3d-native-renderer/.renderer-dev/build','I:/oot3dre-aot-state/build-codex','I:/oot3dre_worktrees/oot3d_mixed_upstream/build-oot3d-mixed-codex-vs','I:/oot3dre_worktrees/oot3d_mixed_upstream/x64')){Add-Scope $p 'build'}
foreach($p in @('I:/oot3dre_work/native_game','I:/oot3dre_work/visual-parity','I:/oot3d-native-renderer/.renderer-dev/artifacts')){Add-Scope $p 'trace'}
foreach($d in Get-ChildItem C:/Users/xander/AppData/Local/Temp -Directory -Filter 'TriAevum-*'){
 if($d.Name -ne 'triaevum-ui-title-cache'){Add-Scope $d.FullName 'trace'}
}
foreach($d in Get-ChildItem I:/TriAevum-source-native-repo -Directory -Filter 'build-*'){Add-Scope $d.FullName 'build'}
foreach($base in @('C:/Users/xander/AppData/Local/Temp','C:/Users/xander','I:/','J:/')){
 foreach($d in Get-ChildItem -LiteralPath $base -Directory -Filter 'TriAevum-*'){
  if($d.Name -match 'diagnostics|verify|flow-|pacing-|castle-|shader-|pipeline-|toon-|outline-|temporal-|spacing-|architecture-|fullscreen-|pass-|vertex-|sampler-|texture-|upload-|replay-|tail-|module-|gpl-|fragment-|alias-|bounded-|compat-|effective-|fixed-|impact-|lazy-|normalized-|owned-|retained-|uniform-|tev-'){Add-Scope $d.FullName 'trace'}
 }
}
if($HistoricalTitleCacheOnly){$scopes.Clear()}
# Verified absent from active CMakeCache/build.ninja; the live plugin is on I:.
Add-Scope 'J:/TriAevum-verify-20260910/title-cache/native-objects' 'build'
$protected=@('J:\TriAevum-verify-20260910\runtime','I:\TriAevum-public','I:\TriAevum-aot-lab-evidence','C:\Users\xander\triaevum-issues-40-43','C:\Users\xander\triaevum-verify-20260911\epona-user')
function Within($Path,$Root){$Path.Equals($Root,[StringComparison]::OrdinalIgnoreCase) -or $Path.StartsWith($Root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)}
function Assert-NoLink($Path){
 while($Path){$item=Get-Item -LiteralPath $Path -Force;if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Linked path: $Path"};$parent=[IO.Directory]::GetParent($Path);$Path=if($parent){$parent.FullName}else{$null}}
}
$selected=@{}
foreach($s in $scopes){
 Assert-NoLink $s.Path
 $args=@('--files','--hidden','--no-ignore','-g','!**/.git/**','-g','!**/_deps/**','-g','!**/third_party/**','-g','!**/savedata/**','-g','!**/savestates/**','-g','!**/*ghidra*/**')
 if($s.Kind -eq 'build'){
  foreach($g in @('*.obj','*.o','*.pch','*.pdb','*.ilk','*.lib','*.a')){$args+=@('-g',$g)}
 }else{foreach($g in @('*.bmp','runtime.json','gpu.json','*.jsonl','raw_events.csv','*.etl','*.dmp')){$args+=@('-g',$g)}}
 $paths=& rg @args -- $s.Path
 if($LASTEXITCODE -gt 1){throw "Enumeration failed: $($s.Path)"}
 foreach($p in $paths){
  $full=[IO.Path]::GetFullPath($p)
  if(-not(Within $full $s.Path)){throw 'Out of scope'}
  if(@($protected|Where-Object {Within $full $_}).Count){continue}
  $f=Get-Item -LiteralPath $full -Force
  if($f.LastWriteTime -ge $cutoff){continue}
  if($s.Kind -eq 'trace' -and $f.Extension -in @('.json','.jsonl','.csv') -and $f.Length -lt 64MB){continue}
  $selected[$full]=[pscustomobject]@{Path=$full;Root=$s.Path;Kind=$s.Kind;Bytes=$f.Length;LastWriteUtc=$f.LastWriteTimeUtc.ToString('o')}
 }
}
$files=@($selected.Values|Sort-Object Path)
$manifest=Join-Path $env:TEMP ('triaevum-expanded-cleanup-'+(Get-Date -Format yyyyMMdd-HHmmss)+'.csv')
$files|Export-Csv -LiteralPath $manifest -NoTypeInformation
$files|Group-Object {[IO.Path]::GetPathRoot($_.Path)}|ForEach-Object {[pscustomobject]@{Drive=$_.Name;Files=$_.Count;GiB=[math]::Round(($_.Group|Measure-Object Bytes -Sum).Sum/1GB,3)}}|Format-Table
Write-Host "Manifest: $manifest"
if(-not $Apply){return}
if(@(Get-Process TriAevum,TriAevumForge,ninja,cmake,clang-cl,lld-link -ErrorAction SilentlyContinue).Count){throw 'Game/build running; refusing cleanup'}
$before=@{};foreach($d in Get-PSDrive C,I,J){$before[$d.Name]=$d.Free}
$removed=0L
foreach($f in $files){
 Assert-NoLink $f.Path
 $now=Get-Item -LiteralPath $f.Path -Force
 if(-not(Within $now.FullName $f.Root) -or $now.Length -ne $f.Bytes -or $now.LastWriteTimeUtc.ToString('o') -ne $f.LastWriteUtc){throw "Changed since audit: $($f.Path)"}
 Remove-Item -LiteralPath $f.Path -Force
 $removed+=$f.Bytes
}
Write-Host ('Removed {0:N3} GiB of obsolete products' -f ($removed/1GB))
Get-PSDrive C,I,J|ForEach-Object {[pscustomobject]@{Drive=$_.Name;FreeGiB=[math]::Round($_.Free/1GB,3);RecoveredGiB=[math]::Round(($_.Free-$before[$_.Name])/1GB,3)}}|Format-Table
