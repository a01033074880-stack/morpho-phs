# SPDX-License-Identifier: AGPL-3.0-only
<#
  Phase 6b sync helper. Run once in the project root AFTER unzipping morpho-phs-phase6b-sync.zip over it:
    powershell -ExecutionPolicy Bypass -File scripts\windows\apply_phase6b_sync.ps1
  The zip cannot delete files. Phase 6b retired the 27 old factory presets (presets\NN_*.morpho, two-digit prefix);
  the zip already contains them under presets\retired\. This script removes the old top-level copies so they are not
  compiled into the plugin next to the 57 new presets (presets\NNN_*.morpho, three-digit prefix), then checks the bank.
#>
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$pre = Join-Path $Root 'presets'
$ret = Join-Path $pre 'retired'
New-Item -ItemType Directory -Force -Path $ret | Out-Null
$old = Get-ChildItem -Path $pre -Filter '*.morpho' -File | Where-Object { $_.Name -match '^\d{2}_' }
foreach ($f in $old) {
    $dst = Join-Path $ret $f.Name
    if (Test-Path $dst) { Remove-Item -Force $f.FullName; Write-Host "removed (copy is in retired): $($f.Name)" }
    else { Move-Item -Force $f.FullName $dst; Write-Host "moved to retired: $($f.Name)" }
}
$new = @(Get-ChildItem -Path $pre -Filter '*.morpho' -File | Where-Object { $_.Name -match '^\d{3}_' })
$rest = @(Get-ChildItem -Path $pre -Filter '*.morpho' -File | Where-Object { $_.Name -notmatch '^\d{3}_' })
Write-Host ("factory presets: {0} (expected 57); other top-level .morpho files: {1}" -f $new.Count, $rest.Count)
foreach ($f in $rest) { Write-Host "  not a factory file name: $($f.Name)" }
if ($new.Count -ne 57) { Write-Warning 'expected 57 factory presets; check that the zip was extracted over the project root' }
Write-Host 'Next: rebuild with scripts\windows\build_windows.ps1, then: dist\windows\morpho_cli.exe validate presets --factory 1'
