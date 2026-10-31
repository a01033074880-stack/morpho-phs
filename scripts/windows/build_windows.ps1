# SPDX-License-Identifier: AGPL-3.0-only
<#
  MORPHO-PHS - Windows build with CMake and MSVC (Phase 4: two plugins).
    MORPHO-PHS     (instrument)  VST3 + Standalone + CLAP
    MORPHO-PHS FX  (effect)      VST3 + Standalone + CLAP
    morpho_cli.exe (render / bench / validate)

  Usage (from any folder, normal PowerShell; no admin needed):
    powershell -ExecutionPolicy Bypass -File scripts\windows\build_windows.ps1
    powershell -ExecutionPolicy Bypass -File scripts\windows\build_windows.ps1 -Generator Ninja -Native
    powershell -ExecutionPolicy Bypass -File scripts\windows\build_windows.ps1 -VsPath C:\SDKs\AetherBuildTools -Generator Ninja
  Parameters:
    -Generator  VS (default; auto-detects Visual Studio 2022/2026) | Ninja (runs vcvars64 first)
    -VsPath     use this Visual Studio / Build Tools folder (contains VC\Auxiliary\Build\vcvars64.bat) instead of
                asking vswhere; works for Build Tools copies that are not registered with the VS installer
    -Native     tune for this CPU (/arch:AVX2 is always on; -Native also defines MORPHO_NATIVE for GCC/Clang builds only)
    -NoClap     skip the CLAP target
    -Clean      delete build\win before configuring
    -Install    copy the VST3 to %CommonProgramFiles%\VST3 and the CLAP to %CommonProgramFiles%\CLAP (needs admin)
  Output: dist\windows\{MORPHO-PHS.vst3, MORPHO-PHS.clap, MORPHO-PHS.exe,
                        MORPHO-PHS FX.vst3, MORPHO-PHS FX.clap, MORPHO-PHS FX.exe, morpho_cli.exe,
                        morpho_stress.exe (Phase 5b thread-pool stress test: dist\windows\morpho_stress.exe 20 1)}
  Log:    logs\build_windows_<timestamp>.txt
#>
param(
    [ValidateSet('VS', 'Ninja')] [string]$Generator = 'VS',
    [string]$VsPath = '',
    [switch]$Native, [switch]$NoClap, [switch]$Clean, [switch]$Install
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $Root
New-Item -ItemType Directory -Force -Path logs, dist\windows | Out-Null
$Stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
Start-Transcript -Path "logs\build_windows_$Stamp.txt" | Out-Null

function Need($exe, $hint) { if (-not (Get-Command $exe -ErrorAction SilentlyContinue)) { throw "$exe not found. $hint" } }

# ---- third-party sources (pinned; not shipped in the zip) ----
$JuceDir = Join-Path $Root 'third_party\JUCE'
$ClapDir = Join-Path $Root 'third_party\clap-juce-extensions'
New-Item -ItemType Directory -Force -Path third_party | Out-Null
if (-not (Test-Path "$JuceDir\CMakeLists.txt")) {
    Need git 'Install Git for Windows: winget install Git.Git' 
    git clone --depth 1 --branch 8.0.9 https://github.com/juce-framework/JUCE.git $JuceDir
}
if (-not $NoClap -and -not (Test-Path "$ClapDir\CMakeLists.txt")) {
    Need git 'Install Git for Windows: winget install Git.Git' 
    git clone https://github.com/free-audio/clap-juce-extensions.git $ClapDir
    git -C $ClapDir checkout 7adee3a1bd4684d4caa5601100e364abccff4b4d
    git -C $ClapDir submodule update --init --recursive
}

# ---- Visual Studio ----
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsVer = $null   # note: PowerShell variables are case-insensitive, so do not reset $vsPath here (it is the -VsPath parameter)
if ($VsPath) {
    $vsPath = (Resolve-Path $VsPath).Path
    if (-not (Test-Path (Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'))) { throw "-VsPath ${vsPath}: VC\Auxiliary\Build\vcvars64.bat not found" }
    if (Test-Path $vswhere) { $vsVer = & $vswhere -path $vsPath -property installationVersion 2>$null }
    if (-not $vsVer) {   # unregistered copy: derive the VS major version from the MSVC toolset (14.3x/14.4x = VS 17, 14.5x = VS 18)
        $tf = Join-Path $vsPath 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt'
        $tv = if (Test-Path $tf) { (Get-Content $tf -TotalCount 1).Trim() } else { '' }
        $vsVer = if ($tv -match '^14\.(\d+)' -and [int]$matches[1] -ge 50) { '18.0' } else { '17.0' }
        Write-Host "MSVC toolset $tv (VS version not registered; assuming $vsVer)"
    }
} else {
    if (-not (Test-Path $vswhere)) { throw 'Visual Studio not found. Install VS 2022+ with "Desktop development with C++", or pass -VsPath <folder>.' }
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $vsVer  = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
    if (-not $vsPath) { throw 'No VS install with the C++ x64 toolset (component VC.Tools.x86.x64). Pass -VsPath <folder> for a Build Tools copy.' }
}
Write-Host "Visual Studio: $vsPath ($vsVer)"

$BuildDir = Join-Path $Root 'build\win'
if ($Clean -and (Test-Path $BuildDir)) { Remove-Item -Recurse -Force $BuildDir }
$cfg = @('-S', 'plugin', '-B', $BuildDir, "-DMORPHO_CLAP=$(if ($NoClap) {'OFF'} else {'ON'})")
if ($Native) { $cfg += '-DMORPHO_NATIVE=ON' }

if ($Generator -eq 'Ninja') {
    # import the x64 MSVC environment into this PowerShell session (also puts the bundled cmake / ninja on PATH)
    $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
    cmd /c "`"$vcvars`" >nul && set" | ForEach-Object { if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] } }
    Need cmake 'Install CMake >= 3.22: winget install Kitware.CMake'
    Need ninja 'Install Ninja: winget install Ninja-build.Ninja (or use -Generator VS)'
    Need cl    'vcvars64.bat did not provide cl.exe - check -VsPath'
    $cfg += @('-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=cl', '-DCMAKE_CXX_COMPILER=cl')
} else {
    Need cmake 'Install CMake >= 3.22: winget install Kitware.CMake'
    $major = [int]($vsVer.Split('.')[0])
    $gen = switch ($major) { 17 { 'Visual Studio 17 2022' } 18 { 'Visual Studio 18 2026' } default { throw "Unsupported VS major version $major" } }
    $cfg += @('-G', $gen, '-A', 'x64')
    if ($VsPath) { $cfg += "-DCMAKE_GENERATOR_INSTANCE=$vsPath" }
}

cmake @cfg
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
$targets = @('MorphoPHS_VST3', 'MorphoPHS_Standalone', 'MorphoPHSFX_VST3', 'MorphoPHSFX_Standalone', 'morpho_cli')
if (-not $NoClap) { $targets += @('MorphoPHS_CLAP', 'MorphoPHSFX_CLAP') }
cmake --build $BuildDir --config Release --parallel --target $targets
if ($LASTEXITCODE -ne 0) { throw 'Build failed (see log)' }

# ---- collect artefacts ----
$dist = Join-Path $Root 'dist\windows'
foreach ($p in @(@{ t = 'MorphoPHS'; n = 'MORPHO-PHS' }, @{ t = 'MorphoPHSFX'; n = 'MORPHO-PHS FX' })) {
    $art = Join-Path $BuildDir "$($p.t)_artefacts\Release"
    if (Test-Path "$dist\$($p.n).vst3") { Remove-Item -Recurse -Force "$dist\$($p.n).vst3" }
    Copy-Item -Recurse -Force "$art\VST3\$($p.n).vst3" $dist
    Copy-Item -Force "$art\Standalone\$($p.n).exe" $dist
    if (-not $NoClap) { Copy-Item -Force "$art\CLAP\$($p.n).clap" $dist }
}
Copy-Item -Force (Join-Path $BuildDir 'morpho_cli_artefacts\Release\morpho_cli.exe') $dist

# Phase 5b: thread-pool stress test (optional; a failure here does not stop the build)
cmake --build $BuildDir --config Release --parallel --target morpho_stress
$st = Join-Path $BuildDir 'morpho_stress_artefacts\Release\morpho_stress.exe'
if ($LASTEXITCODE -eq 0 -and (Test-Path $st)) { Copy-Item -Force $st $dist } else { Write-Warning 'morpho_stress (thread-pool stress test) did not build - plugins are unaffected' }

& "$dist\morpho_cli.exe" validate presets
if ($LASTEXITCODE -ne 0) { throw 'Preset validation failed' }

if ($Install) {
    foreach ($n in @('MORPHO-PHS', 'MORPHO-PHS FX')) {
        Copy-Item -Recurse -Force "$dist\$n.vst3" "$env:CommonProgramFiles\VST3\"
        if (-not $NoClap) { New-Item -ItemType Directory -Force "$env:CommonProgramFiles\CLAP" | Out-Null; Copy-Item -Force "$dist\$n.clap" "$env:CommonProgramFiles\CLAP\" }
    }
}
Write-Host "`nBUILD OK -> $dist"
Get-ChildItem $dist | Format-Table Name, Length, LastWriteTime
Stop-Transcript | Out-Null
