# SPDX-License-Identifier: AGPL-3.0-only
<#
  MORPHO-PHS - Phase 5 real-time benchmark on Windows (same engine code as the plugins; headless, no audio device).
  Run after build_windows.ps1. Close DAWs/browsers, plug in the laptop (AC), use the "Best performance" power mode.

    powershell -ExecutionPolicy Bypass -File scripts\windows\bench_windows.ps1                 # all variants, 3 repeats (~32 min)
    powershell -ExecutionPolicy Bypass -File scripts\windows\bench_windows.ps1 -Repeats 2      # all variants, 2 repeats (~21 min)
    powershell -ExecutionPolicy Bypass -File scripts\windows\bench_windows.ps1 -Quick          # 2 s/config, 1 repeat
    powershell -ExecutionPolicy Bypass -File scripts\windows\bench_windows.ps1 -Variants 1T,1T_csr -Repeats 2
    powershell -ExecutionPolicy Bypass -File scripts\windows\bench_windows.ps1 -Resume experiments\results\phase5\windows\<HOST>_<stamp>
        (-Resume: same folder; pass the same -Seed/-Repeats/-Variants -> same order; every (variant, repeat) already in
         runlog.csv with exit 0 is skipped, everything else (missing, TIMEOUT, failed) is run again)

  Robustness (Phase 5b): every morpho_cli invocation has a timeout (-TimeoutFactor x the expected duration, at least
  -MinTimeout s).  On timeout the process is killed, the rows that finished are kept (morpho_cli rewrites its JSON after
  every row) and a TIMEOUT row is recorded; a failing or timed-out variant never aborts the run.

  Bench hygiene (Phase 5, expert review 2.1):
    * every variant run is preceded by a cooldown (-Cooldown, default 20 s) so turbo / thermal state is comparable
    * variants run in a seeded random order (-Seed), the whole set -Repeats times (default 3), a new order per repeat
    * per variant run the log records start time, power scheme + overlay (power mode slider), AC/DC, the Windows
      "% processor performance" based effective clock, and morpho_cli adds an in-process clock estimate per row
  Variants (32x32, block 128 unless -Blocks/-Sizes are given; -Variants a,b,c selects a subset):
    p5_caller_cpuset     plugin default: host thread computes + workers on the clean P-core CPU set, MMCSS, EcoQoS off,
                         pure-spin in-block barrier, idle spin 3.5 ms (threads 2,3,4)
    p5_caller_cpuset_host  same + the bench's host thread itself gets the clean CPU set + MMCSS (= a DAW audio thread
                         with MMCSS); shows what a well-behaved host adds (threads 2,3,4)
    p5_caller_pin        host thread computes + workers hard-pinned one per clean P-core (threads 2,3,4)
    p5_dedicated_cpuset  dedicated pool on the clean CPU set, host thread blocks (threads 3,4)
    p3spin               Phase-3-style protocol (pure spin, no pinning, no MMCSS, EcoQoS untouched) (threads 4,6)
    p4_default           Phase-4 plugin default (dedicated pool on pset, hybrid barrier, 200 us idle) (threads 4,5)
    1T / 1T_csr          single thread, stencil kernel vs Phase-2..4 CSR gather (kernel speed-up, no threading)
    inst_p5              instrument mode (MPE phrase) with the plugin default, threads 2,3
    hqf_L32 / hq_L32     HQ-fast / HQ-exact potential (quartic + logcosh), plugin default protocol, threads 1,2,3
                         (hq_L32 passes --hq-exact-any-L 1: the plugin itself runs HQ-exact as HQ-fast above 16x16)
    L16 / L16_hq         16x16 lattice, quartic / HQ-exact, threads 1,2
  Plugin Auto threads since Phase 5c: L32 FX = 2, Instrument = 3, HQ-fast = 3 (host thread + workers on the clean
  P-core CPU set); L16 = 1 (HQ-exact at 16x16: 2).  HQ-exact is limited to L <= 16.
  Pass criterion (32x32, block 128): 0 overruns and p99/budget < 0.7 = PASS; <= 0.1 % overruns = NEAR; else FAIL.
  'bOSw' (barrier_os_waits) must be 0 for the p5 variants.

  Writes experiments\results\phase5\windows\<HOST>_<timestamp>\
     sysinfo.json, runlog.csv (one line per variant run), <variant>_r<k>.{json,txt}, SUMMARY.txt
#>
param([switch]$Quick, [switch]$Unpaced, [string]$Variants = '', [int]$Repeats = 3, [int]$Seed = 5, [double]$Cooldown = 20,
      [double]$Seconds = 10, [string]$Blocks = '128', [string]$Sizes = '32', [int]$Load = 0, [string]$Resume = '',
      [double]$TimeoutFactor = 3, [double]$MinTimeout = 60)
$ErrorActionPreference = 'Stop'
$CallerDir = (Get-Location).Path
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $Root
$cli = Join-Path $Root 'dist\windows\morpho_cli.exe'
if (-not (Test-Path $cli)) { throw "morpho_cli.exe not found - run scripts\windows\build_windows.ps1 first" }
if ($Quick) { $Seconds = 2; $Repeats = 1; $Cooldown = 5 }

if ($Resume -ne '') {
    # relative -Resume paths: first relative to the current directory, then to the repo root
    if ([System.IO.Path]::IsPathRooted($Resume)) { $cand = $Resume } else { $cand = Join-Path $CallerDir $Resume; if (-not (Test-Path $cand)) { $cand = Join-Path $Root $Resume } }
    if (-not (Test-Path $cand)) { throw "-Resume folder not found: $Resume" }
    $out = (Resolve-Path $cand).Path
    Write-Host "RESUME into $out"
} else {
    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
    $out = Join-Path $Root "experiments\results\phase5\windows\$($env:COMPUTERNAME)_$stamp"
    New-Item -ItemType Directory -Force -Path $out | Out-Null
}

# ---------------- power / clock probes (locale independent: WMI classes, registry, powercfg GUIDs) ----------------
$overlayNames = @{
    '961cc777-2547-4f9d-8174-7d86181b8a7a' = 'best_power_efficiency'
    '3af9b8d9-7c97-431d-ad78-34a8bfea439f' = 'better_performance'
    'ded574b5-45a0-4f42-8737-46345c09c238' = 'best_performance'
    '00000000-0000-0000-0000-000000000000' = 'balanced(none)'
}
function Get-PowerState {
    $scheme = ''; $overlay = ''; $ac = ''
    try { $scheme = ((powercfg /getactivescheme) -join ' ') -replace '\s+', ' ' } catch { $scheme = 'n/a' }
    try {
        $k = Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Power\User\PowerSchemes' -ErrorAction Stop
        $g = "$($k.ActiveOverlayAcPowerScheme)"
        if ($g -eq '') { $overlay = 'none' } elseif ($overlayNames.ContainsKey($g)) { $overlay = $overlayNames[$g] } else { $overlay = $g }
    } catch { $overlay = 'n/a' }
    try {
        $b = Get-CimInstance Win32_Battery -ErrorAction Stop | Select-Object -First 1
        if ($null -eq $b) { $ac = 'no_battery' } elseif ($b.BatteryStatus -eq 2) { $ac = 'AC' } else { $ac = "battery(status=$($b.BatteryStatus),$($b.EstimatedChargeRemaining)%)" }
    } catch { $ac = 'n/a' }
    return [ordered]@{ scheme = $scheme; overlay = $overlay; power_source = $ac }
}
function Get-ClockMHz {
    # effective clock = base clock * "% processor performance" (WMI perf class; same data as Task Manager "speed")
    try {
        $p = Get-CimInstance Win32_PerfFormattedData_Counters_ProcessorInformation -Filter "Name='_Total'" -ErrorAction Stop
        $base = (Get-CimInstance Win32_Processor | Select-Object -First 1).MaxClockSpeed
        return [math]::Round($base * $p.PercentProcessorPerformance / 100.0)
    } catch { return -1 }
}

# ---------------- system info ----------------
$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
$ps0 = Get-PowerState
$info = [ordered]@{
    host = $env:COMPUTERNAME; time = (Get-Date).ToString('o'); cpu = $cpu.Name.Trim()
    cores = $cpu.NumberOfCores; logical = $cpu.NumberOfLogicalProcessors; max_clock_mhz = $cpu.MaxClockSpeed
    os = (Get-CimInstance Win32_OperatingSystem).Caption; power_plan = $ps0.scheme; power_overlay = $ps0.overlay; power_source = $ps0.power_source
    ram_gb = [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB, 1)
    seconds_per_config = $Seconds; blocks = $Blocks; sizes = $Sizes; paced = (-not $Unpaced); load_threads = $Load
    repeats = $Repeats; seed = $Seed; cooldown_s = $Cooldown
}
$sysf = Join-Path $out 'sysinfo.json'
if (Test-Path $sysf) { $sysf = Join-Path $out ("sysinfo_resume_" + (Get-Date -Format 'yyyyMMdd_HHmmss') + ".json") }
$info | ConvertTo-Json | Set-Content -Encoding UTF8 $sysf
$info | Format-List
if ($ps0.power_source -ne 'AC' -and $ps0.power_source -ne 'no_battery') { Write-Warning "not on AC power ($($ps0.power_source)) - results will be throttled" }
if ($ps0.overlay -ne 'best_performance') { Write-Warning "power mode is '$($ps0.overlay)', recommended: Best performance" }

# ---------------- variants ----------------
$paced = $(if ($Unpaced) { '0' } else { '1' })
$common = @('--blocks', $Blocks, '--sizes', $Sizes, '--seconds', "$Seconds", '--fs', '48000', '--potential', '0', '--interp', '0',
            '--paced', $paced, '--load', "$Load")
$all = [ordered]@{
    'p5_caller_cpuset'      = @('--mode', '0', '--threads', '2,3,4', '--pin', 'cpuset', '--pool', 'caller', '--mmcss', '1')
    'p5_caller_cpuset_host' = @('--mode', '0', '--threads', '2,3,4', '--pin', 'cpuset', '--pool', 'caller', '--mmcss', '1', '--caller-cpuset', '1', '--caller-mmcss', '1')
    'p5_caller_pin'         = @('--mode', '0', '--threads', '2,3,4', '--pin', 'pcores', '--pool', 'caller', '--mmcss', '1')
    'p5_dedicated_cpuset'   = @('--mode', '0', '--threads', '3,4', '--pin', 'cpuset', '--pool', 'dedicated', '--mmcss', '1')
    'p3spin'                = @('--mode', '0', '--threads', '4,6', '--pin', 'none', '--pool', 'caller', '--mmcss', '0', '--ecoqos', '1')
    'p4_default'            = @('--mode', '0', '--threads', '4,5', '--pin', 'pset', '--pool', 'dedicated', '--mmcss', '1', '--inblock-os-wait', '1',
                                '--idle-spin-us', '200', '--adaptive', '1', '--kernel', 'csr', '--ecoqos', '1')
    '1T'                    = @('--mode', '0', '--threads', '1', '--pin', 'cpuset', '--pool', 'caller', '--mmcss', '1')
    '1T_csr'                = @('--mode', '0', '--threads', '1', '--pin', 'cpuset', '--pool', 'caller', '--mmcss', '1', '--kernel', 'csr')
    'inst_p5'               = @('--mode', '2', '--threads', '2,3', '--pin', 'cpuset', '--pool', 'caller', '--mmcss', '1')
    'hqf_L32'               = @('--mode', '0', '--threads', '1,2,3', '--pin', 'cpuset', '--pool', 'caller', '--mmcss', '1', '--potential', '1')
    'hq_L32'                = @('--mode', '0', '--threads', '1,2,3', '--pin', 'cpuset', '--pool', 'caller', '--mmcss', '1', '--potential', '2', '--hq-exact-any-L', '1')
    'L16'                   = @('--mode', '0', '--threads', '1,2', '--pin', 'cpuset', '--pool', 'caller', '--mmcss', '1', '--sizes', '16')
    'L16_hq'                = @('--mode', '0', '--threads', '1,2', '--pin', 'cpuset', '--pool', 'caller', '--mmcss', '1', '--sizes', '16', '--potential', '2')
}
# (variant options come after $common, morpho_cli takes the last occurrence -> --sizes/--potential overrides work)
# number of configs (rows) a variant produces -> expected duration and timeout
function Get-RowCount([string[]]$a) {
    $th = '1'; $sz = $Sizes; $bl = $Blocks
    for ($i = 0; $i -lt $a.Count - 1; $i++) {
        if ($a[$i] -eq '--threads') { $th = $a[$i + 1] } elseif ($a[$i] -eq '--sizes') { $sz = $a[$i + 1] } elseif ($a[$i] -eq '--blocks') { $bl = $a[$i + 1] }
    }
    return [math]::Max(1, $th.Split(',').Count * $sz.Split(',').Count * $bl.Split(',').Count)
}
function Quote-Arg([string]$x) { if ($x -match '[\s"]') { return '"' + ($x -replace '"', '\"') + '"' } else { return $x } }
if ($Variants -ne '') {
    $sel = $Variants.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' }
    foreach ($s in $sel) { if (-not $all.Contains($s)) { throw "unknown variant '$s' (known: $($all.Keys -join ', '))" } }
} else { $sel = @($all.Keys) }

$runlog = Join-Path $out 'runlog.csv'
$doneSet = @{}
if (Test-Path $runlog) {
    foreach ($l in (Import-Csv $runlog)) { if ("$($l.exit)" -eq '0') { $doneSet["$($l.variant)#$($l.repeat)"] = $true } }
    Write-Host "runlog has $($doneSet.Count) completed (variant, repeat) runs - they are skipped"
} else {
    'repeat,order,variant,start,end,power_scheme,power_overlay,power_source,clock_mhz_before,clock_mhz_after,exit' | Set-Content -Encoding UTF8 $runlog
}
$failures = New-Object System.Collections.ArrayList
$rng = New-Object System.Random($Seed)
$total = $Repeats * $sel.Count; $done = 0; $ran = 0; $t0 = Get-Date
for ($r = 1; $r -le $Repeats; $r++) {
    # Fisher-Yates shuffle with the seeded RNG -> reproducible order, different per repeat
    $order = @($sel)
    for ($i = $order.Count - 1; $i -gt 0; $i--) { $j = $rng.Next($i + 1); $tmp = $order[$i]; $order[$i] = $order[$j]; $order[$j] = $tmp }
    Write-Host "`n##### repeat $r / $Repeats  order: $($order -join ' ')"
    $k = 0
    foreach ($v in $order) {
        $k++
        if ($doneSet.ContainsKey("$v#$r")) { Write-Host "skip $v (repeat $r): already done"; $done++; continue }
        $start = (Get-Date).ToString('o')
        try {
            if ($Cooldown -gt 0) { Write-Host "cooldown $Cooldown s ..."; Start-Sleep -Milliseconds ([int](1000 * $Cooldown)) }
            $ps = Get-PowerState; $clk0 = Get-ClockMHz; $start = (Get-Date).ToString('o')
            $nrows = Get-RowCount ($common + $all[$v])
            $expect = $nrows * ($Seconds + 3.0) + 5.0
            $tmo = [math]::Max($MinTimeout, $TimeoutFactor * $expect)
            Write-Host ("=== [{0}/{1}] {2} (repeat {3})  start {4}  overlay={5} src={6} clock~{7} MHz  rows={8} timeout={9:N0} s" -f ($done + 1), $total, $v, $r, $start, $ps.overlay, $ps.power_source, $clk0, $nrows, $tmo)
            $name = "${v}_r$r"
            $jf = Join-Path $out "$name.json"
            if (Test-Path $jf) { Remove-Item $jf -Force }   # stale partial result of an earlier attempt (resume)
            $cliArgs = @('bench', (Join-Path $out $name)) + $common + @('--tag', $v, '--variant', "${v}#r$r") + $all[$v]
            $logf = Join-Path $out "$name.console.txt"; $errf = Join-Path $out "$name.stderr.txt"
            $argStr = ($cliArgs | ForEach-Object { Quote-Arg "$_" }) -join ' '
            $p = Start-Process -FilePath $cli -ArgumentList $argStr -NoNewWindow -PassThru -RedirectStandardOutput $logf -RedirectStandardError $errf
            $null = $p.Handle   # keep the process handle so ExitCode is still readable after exit
            $sw = [System.Diagnostics.Stopwatch]::StartNew(); $shown = 0; $timedOut = $false
            while (-not $p.HasExited) {
                Start-Sleep -Milliseconds 500
                if (Test-Path $logf) {
                    $lines = @(Get-Content $logf -ErrorAction SilentlyContinue)
                    if ($lines.Count -gt $shown) { $lines[$shown..($lines.Count - 1)] | ForEach-Object { Write-Host $_ }; $shown = $lines.Count }
                }
                if ($sw.Elapsed.TotalSeconds -gt $tmo) {
                    $timedOut = $true
                    Write-Warning ("{0} (repeat {1}) TIMEOUT after {2:N0} s - killing morpho_cli (pid {3})" -f $v, $r, $sw.Elapsed.TotalSeconds, $p.Id)
                    try { Stop-Process -Id $p.Id -Force -ErrorAction Stop } catch { & taskkill /F /T /PID $p.Id | Out-Null }
                    $null = $p.WaitForExit(10000)
                    break
                }
            }
            if (-not $timedOut) { $p.WaitForExit() }
            if (Test-Path $logf) {
                $lines = @(Get-Content $logf -ErrorAction SilentlyContinue)
                if ($lines.Count -gt $shown) { $lines[$shown..($lines.Count - 1)] | ForEach-Object { Write-Host $_ } }
            }
            if ($timedOut) { $code = 'TIMEOUT' } else { $code = $p.ExitCode; if ($null -eq $code) { $code = -999 } }
            $clk1 = Get-ClockMHz; $end = (Get-Date).ToString('o')
            ('{0},{1},{2},{3},{4},"{5}",{6},"{7}",{8},{9},{10}' -f $r, $k, $v, $start, $end, ($ps.scheme -replace '"', "'"), $ps.overlay, $ps.power_source, $clk0, $clk1, $code) | Add-Content $runlog
            # tag the JSON (possibly partial) with the run-level metadata; add a TIMEOUT / FAILED row when rows are missing
            if (Test-Path $jf) { $j = Get-Content $jf -Raw | ConvertFrom-Json } else { $j = [pscustomobject]@{ tool = 'bench_windows.ps1 (no morpho_cli output)'; rows = @() } }
            $j | Add-Member -NotePropertyName repeat -NotePropertyValue $r -Force
            $j | Add-Member -NotePropertyName order_in_repeat -NotePropertyValue $k -Force
            $j | Add-Member -NotePropertyName run_start -NotePropertyValue $start -Force
            $j | Add-Member -NotePropertyName power_scheme -NotePropertyValue $ps.scheme -Force
            $j | Add-Member -NotePropertyName power_overlay -NotePropertyValue $ps.overlay -Force
            $j | Add-Member -NotePropertyName power_source -NotePropertyValue $ps.power_source -Force
            $j | Add-Member -NotePropertyName clock_mhz_before -NotePropertyValue $clk0 -Force
            $j | Add-Member -NotePropertyName clock_mhz_after -NotePropertyValue $clk1 -Force
            $j | Add-Member -NotePropertyName exit -NotePropertyValue "$code" -Force
            $have = @($j.rows).Count
            if ("$code" -ne '0') {
                $verd = $(if ($timedOut) { 'TIMEOUT' } else { 'FAILED' })
                $miss = [pscustomobject]@{ tag = $v; L = -1; block = -1; threads = -1; verdict = $verd; us_per_sample_median = 0; p99_over_budget = 99;
                                           overruns = -1; blocks_measured = 0; barrier_os_waits = -1; idle_os_waits = -1; clk_ghz_est = 0; row_start = $start;
                                           note = "$verd after $have of $nrows rows (exit $code); the next config did not finish" }
                $j | Add-Member -NotePropertyName rows -NotePropertyValue (@($j.rows) + @($miss)) -Force
                [void]$failures.Add("$v r$r : $verd after $have/$nrows rows (exit $code)")
            }
            $j | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 $jf
        } catch {
            Write-Warning "variant $v (repeat $r) failed: $($_.Exception.Message) - continuing"
            [void]$failures.Add("$v r$r : script error $($_.Exception.Message)")
            try { ('{0},{1},{2},{3},{4},"{5}",{6},"{7}",{8},{9},{10}' -f $r, $k, $v, $start, (Get-Date).ToString('o'), '', '', '', -1, -1, 'ERROR') | Add-Content $runlog } catch { }
        }
        $done++; $ran++
        $el = ((Get-Date) - $t0).TotalMinutes
        Write-Host ("    elapsed {0:N1} min, ~{1:N1} min left" -f $el, ($el / $ran * ($total - $done)))
    }
}

# ---------------- summary ----------------
$sum = Join-Path $out 'SUMMARY.txt'
"MORPHO-PHS Phase 5 Windows bench  $($info.cpu)  $($info.logical) logical  $($info.time)  paced=$(-not $Unpaced)  repeats=$Repeats seed=$Seed cooldown=${Cooldown}s  $Seconds s/config" | Set-Content -Encoding UTF8 $sum
"power: $($info.power_plan) | overlay=$($info.power_overlay) | $($info.power_source)" | Add-Content $sum
$rows = foreach ($f in (Get-ChildItem $out -Filter '*_r*.json' | Where-Object { $_.Name -notlike 'sysinfo*' })) {
    $j = Get-Content $f.FullName -Raw | ConvertFrom-Json
    foreach ($x in $j.rows) { $x | Add-Member -NotePropertyName repeat -NotePropertyValue $j.repeat -Force; $x }
}
"`n## Criterion rows aggregated over repeats (PASS = 0 overruns and p99/budget < 0.7 in EVERY repeat)" | Add-Content $sum
$agg = $rows | Group-Object tag, L, block, threads | ForEach-Object {
    $g = $_.Group
    $uss = @($g | ForEach-Object { [double]$_.us_per_sample_median } | Sort-Object)
    $pb = @($g | ForEach-Object { [double]$_.p99_over_budget } | Sort-Object)
    $ov = ($g | Measure-Object -Property overruns -Sum).Sum
    $bl = ($g | Measure-Object -Property blocks_measured -Sum).Sum
    $bo = ($g | Measure-Object -Property barrier_os_waits -Sum).Sum
    $ck = @($g | ForEach-Object { [double]$_.clk_ghz_est } | Sort-Object)
    $vs = ($g | Sort-Object repeat | ForEach-Object { $_.verdict }) -join '/'
    $allPass = (@($g | Where-Object { $_.verdict -ne 'PASS' }).Count -eq 0)
    [pscustomobject]@{
        variant = $g[0].tag; L = $g[0].L; block = $g[0].block; threads = $g[0].threads; n = $g.Count
        'us/smp_med' = '{0:N3}' -f $uss[[int][math]::Floor($uss.Count / 2)]
        'p99/bud_worst' = '{0:N3}' -f $pb[$pb.Count - 1]
        'p99/bud_med' = '{0:N3}' -f $pb[[int][math]::Floor($pb.Count / 2)]
        overruns = "$ov/$bl"; bOSw = $bo; 'clkGHz_med' = '{0:N2}' -f $ck[[int][math]::Floor($ck.Count / 2)]
        verdicts = $vs; ALL = $(if ($allPass) { 'PASS' } else { '-' })
    }
}
$agg | Sort-Object L, block, variant, threads | Format-Table -AutoSize | Out-String -Width 250 | Add-Content $sum
"`n## All rows" | Add-Content $sum
$rows | Sort-Object tag, L, block, threads, repeat |
    Format-Table tag, repeat, L, block, threads, @{n='us/smp'; e={'{0:N3}' -f $_.us_per_sample_median}}, @{n='p99_us'; e={'{0:N0}' -f $_.block_p99_us}},
                 @{n='max_us'; e={'{0:N0}' -f $_.block_max_us}}, @{n='p99/bud'; e={'{0:N3}' -f $_.p99_over_budget}}, overruns, verdict,
                 @{n='clk'; e={'{0:N2}' -f $_.clk_ghz_est}}, @{n='bOSw'; e={$_.barrier_os_waits}}, @{n='iOSw'; e={$_.idle_os_waits}}, row_start -AutoSize |
    Out-String -Width 300 | Add-Content $sum
"`n## Failures / timeouts: $($failures.Count)" | Add-Content $sum
foreach ($f in $failures) { "  $f" | Add-Content $sum }
"`n## Run log" | Add-Content $sum
Get-Content $runlog | Add-Content $sum
Get-Content $sum | Select-Object -First 45
Write-Host "`nDONE -> $out"
Write-Host "Send back the whole folder (or zip it):  Compress-Archive -Path '$out' -DestinationPath '$out.zip'"
