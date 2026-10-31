# SPDX-License-Identifier: AGPL-3.0-only
<#
  Phase 6b2 sync helper. Run once in the project root AFTER unzipping morpho-phs-phase6b2-sync.zip over it:
    powershell -ExecutionPolicy Bypass -File scripts\windows\apply_phase6b2_sync.ps1
  The zip cannot delete files. Phase 6b2 renamed "Big Spiral 32" (presets\029_big_spiral_32.morpho, 32x32) to
  "Big Spiral" (presets\029_big_spiral.morpho, 16x16). This script removes the old file so it is not compiled into the
  plugin next to the new one, then checks that the bank still has 57 factory presets.
#>
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$pre = Join-Path $Root 'presets'
$old = Join-Path $pre '029_big_spiral_32.morpho'
if (Test-Path $old) { Remove-Item -Force $old; Write-Host 'removed: presets\029_big_spiral_32.morpho' }
else { Write-Host 'presets\029_big_spiral_32.morpho not present (already removed)' }
if (-not (Test-Path (Join-Path $pre '029_big_spiral.morpho'))) { Write-Warning 'presets\029_big_spiral.morpho is missing; extract the zip over the project root first' }
$new = @(Get-ChildItem -Path $pre -Filter '*.morpho' -File | Where-Object { $_.Name -match '^\d{3}_' })
Write-Host ("factory presets: {0} (expected 57)" -f $new.Count)
if ($new.Count -ne 57) { Write-Warning 'expected 57 factory presets' }
Write-Host 'Next: rebuild with scripts\windows\build_windows.ps1, then: dist\windows\morpho_cli.exe validate presets --factory 1'
