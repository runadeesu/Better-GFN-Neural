# Better GFN Neural - Windows integration tests.
# Drives the real application (automation mode, UI hidden) against FakeGfn.exe
# copied as "GeForceNOW.exe", and checks detection, game profiles, window
# changes, GFN exit/restart, crash recovery and settings persistence.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$FakeGfn,
    [Parameter(Mandatory = $true)][string]$OutDir
)
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$results = [ordered]@{}
$failures = @()

function Check($name, $cond, $detail) {
    $results[$name] = [ordered]@{ pass = [bool]$cond; detail = "$detail" }
    if (-not $cond) { $script:failures += $name }
    Write-Host ("[{0}] {1} {2}" -f ($(if ($cond) { "PASS" } else { "FAIL" })), $name, $detail)
}

# FakeGfn as GeForceNOW.exe
$fakeDir = Join-Path $OutDir "fakegfn"
New-Item -ItemType Directory -Force -Path $fakeDir | Out-Null
$gfnExe = Join-Path $fakeDir "GeForceNOW.exe"
Copy-Item $FakeGfn $gfnExe -Force

# ---------------------------------------------------------------- 1. detection lifecycle
$data = Join-Path $OutDir "data_detect"
Remove-Item -Recurse -Force $data -ErrorAction SilentlyContinue
$report = Join-Path $OutDir "detect.json"
$app = Start-Process -FilePath $Exe -ArgumentList @("--automation", "detect", "--seconds", "62", "--data-dir", "`"$data`"", "--output", "`"$report`"") -PassThru
Start-Sleep -Seconds 5
# launcher -> game (Cyberpunk, (R) via escape) -> resize -> fullscreen -> windowed -> other game -> exit
$script1 = "6:title=Cyberpunk 2077® on GeForce NOW;12:resize=1600x900;16:fullscreen;21:windowed;24:minimize;26:restore;28:title=Fortnite® on GeForce NOW;34:exit"
$gfn1 = Start-Process -FilePath $gfnExe -ArgumentList @("--title", "`"GeForce NOW`"", "--size", "960x540", "--script", "`"$script1`"") -PassThru
$gfn1.WaitForExit(60000) | Out-Null
Start-Sleep -Seconds 6
# GFN restarts directly into a game
$gfn2 = Start-Process -FilePath $gfnExe -ArgumentList @("--title", "`"Apex Legends™ on GeForce NOW`"", "--size", "1920x1080", "--script", "`"10:exit`"") -PassThru
$gfn2.WaitForExit(30000) | Out-Null
$app.WaitForExit(120000) | Out-Null
Check "app_exit_code" ($app.ExitCode -eq 0) "exit code $($app.ExitCode)"
Check "report_written" (Test-Path $report) $report
if (Test-Path $report) {
    $r = Get-Content $report -Raw | ConvertFrom-Json
    $tl = $r.timeline
    $first = $tl | Select-Object -First 4
    Check "initial_waiting" (($first | Where-Object { $_.gfn -eq "Waiting" }).Count -ge 1) "first samples: $(($first | ForEach-Object { $_.gfn }) -join ',')"
    Check "launcher_connected" (($tl | Where-Object { $_.gfn -eq "Connected" }).Count -ge 1) "Connected samples: $(($tl | Where-Object { $_.gfn -eq 'Connected' }).Count)"
    $cp = $tl | Where-Object { $_.gfn -eq "Enhancing" -and $_.game -eq "Cyberpunk 2077" }
    Check "game_detected_cyberpunk" ($cp.Count -ge 1) "samples: $($cp.Count)"
    $fn = $tl | Where-Object { $_.game -eq "Fortnite" }
    Check "game_switch_fortnite" ($fn.Count -ge 1) "samples: $($fn.Count)"
    $afterExit = $tl | Where-Object { $_.t -gt 42 -and $_.t -lt 44.5 }
    Check "gfn_exit_waiting" (($afterExit | Where-Object { $_.gfn -eq "Waiting" }).Count -ge 1) "states 42-44.5s: $(($afterExit | ForEach-Object { $_.gfn }) -join ',')"
    $apex = $tl | Where-Object { $_.game -eq "Apex Legends" }
    Check "gfn_restart_reconnect" ($apex.Count -ge 1) "samples: $($apex.Count)"
    $keys = $r.profiles | ForEach-Object { $_.key }
    Check "profiles_created" (($keys -contains "cyberpunk2077") -and ($keys -contains "fortnite") -and ($keys -contains "apexlegends")) "profiles: $($keys -join ',')"
    $captured = ($tl | Measure-Object -Property captured -Maximum).Maximum
    $presented = ($tl | Measure-Object -Property presented -Maximum).Maximum
    $backends = ($tl | Where-Object { $_.capture_backend } | ForEach-Object { $_.capture_backend } | Select-Object -Unique) -join ','
    $sizes = ($tl | Where-Object { $_.capture -ne "0x0" } | ForEach-Object { $_.capture } | Select-Object -Unique) -join ','
    $errors = ($tl | Where-Object { $_.error } | ForEach-Object { $_.error } | Select-Object -Unique) -join ' | '
    # Informational: GitHub runners may not allow capture/foreground changes
    $results["capture_info"] = [ordered]@{ pass = $true; detail = "max captured=$captured presented=$presented backends=$backends capture sizes=$sizes errors=$errors" }
    Write-Host "[INFO] capture: captured=$captured presented=$presented backends=$backends sizes=$sizes errors=$errors"
}
Check "settings_saved" (Test-Path (Join-Path $data "settings.json")) "settings.json"
Check "log_written" ((Get-ChildItem (Join-Path $data "logs") -Filter "*.log" -ErrorAction SilentlyContinue).Count -ge 1) "logs folder"
$logText = (Get-ChildItem (Join-Path $data "logs") -Filter "*.log" | Get-Content -Raw)
Check "log_privacy" (-not ($logText -match [regex]::Escape($env:USERNAME))) "user name not present in logs"

# ---------------------------------------------------------------- 2. crash recovery
$cdata = Join-Path $OutDir "data_crash"
Remove-Item -Recurse -Force $cdata -ErrorAction SilentlyContinue
for ($i = 0; $i -lt 2; $i++) {
    $p = Start-Process -FilePath $Exe -ArgumentList @("--automation", "crash", "--simulate-crash", "--data-dir", "`"$cdata`"") -PassThru
    $p.WaitForExit(30000) | Out-Null
}
$dumps = Get-ChildItem (Join-Path $cdata "logs") -Filter "crash_*.dmp" -ErrorAction SilentlyContinue
Check "crash_minidump" ($dumps.Count -ge 2) "dumps: $($dumps.Count) (expected one per crash)"
$creport = Join-Path $OutDir "after_crash.json"
$p = Start-Process -FilePath $Exe -ArgumentList @("--automation", "recover", "--seconds", "4", "--data-dir", "`"$cdata`"", "--output", "`"$creport`"") -PassThru
$p.WaitForExit(30000) | Out-Null
$cr = Get-Content $creport -Raw | ConvertFrom-Json
Check "safe_mode_after_crashes" ($cr.safe_mode -eq $true) "safe_mode=$($cr.safe_mode)"
$p = Start-Process -FilePath $Exe -ArgumentList @("--automation", "recover2", "--seconds", "3", "--data-dir", "`"$cdata`"", "--output", "`"$creport`"") -PassThru
$p.WaitForExit(30000) | Out-Null
$cr = Get-Content $creport -Raw | ConvertFrom-Json
Check "normal_after_clean_exit" ($cr.safe_mode -eq $false) "safe_mode=$($cr.safe_mode)"

# ---------------------------------------------------------------- 3. corrupt settings recovery
$sdata = Join-Path $OutDir "data_corrupt"
Remove-Item -Recurse -Force $sdata -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $sdata | Out-Null
Set-Content -Path (Join-Path $sdata "settings.json") -Value "{ this is not json"
$p = Start-Process -FilePath $Exe -ArgumentList @("--automation", "corrupt", "--seconds", "3", "--data-dir", "`"$sdata`"", "--output", "`"$(Join-Path $OutDir 'corrupt.json')`"") -PassThru
$p.WaitForExit(30000) | Out-Null
Check "corrupt_settings_survived" ($p.ExitCode -eq 0) "exit code $($p.ExitCode)"
$fixed = Get-Content (Join-Path $sdata "settings.json") -Raw
Check "corrupt_settings_rewritten" ($fixed -match '"schema_version"') "settings.json valid again"
Check "corrupt_settings_kept_for_diagnostics" (Test-Path (Join-Path $sdata "settings.json.corrupt")) "settings.json.corrupt"

# ---------------------------------------------------------------- 4. UI screenshots
$shots = Join-Path $OutDir "screenshots"
New-Item -ItemType Directory -Force -Path $shots | Out-Null
$p = Start-Process -FilePath $Exe -ArgumentList @("--automation", "screenshots", "--seconds", "3", "--data-dir", "`"$(Join-Path $OutDir 'data_shots')`"", "--output", "`"$(Join-Path $shots 'report.json')`"") -PassThru
$p.WaitForExit(120000) | Out-Null
$pngs = Get-ChildItem $shots -Filter "*.png" -ErrorAction SilentlyContinue
Check "ui_screenshots" ($pngs.Count -ge 24) "png files: $($pngs.Count) (English, Japanese, Japanese + English)"
$jaShots = @($pngs | Where-Object { $_.Name -like "*_ja.png" })
Check "ui_screenshots_japanese" ($jaShots.Count -ge 8) "Japanese png files: $($jaShots.Count)"

$results | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $OutDir "integration_results.json")
if ($failures.Count -gt 0) {
    Write-Host "FAILED: $($failures -join ', ')"
    exit 1
}
Write-Host "All integration checks passed"
exit 0
