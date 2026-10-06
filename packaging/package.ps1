# Creates the release artifacts:
#   dist/BetterGFNNeural.exe
#   dist/BetterGFNNeural-<ver>-portable.zip   (portable.dat => settings/logs next to the exe)
#   dist/BetterGFNNeuralSetup.exe             (NSIS, per-user install)
#   dist/SHA256SUMS.txt
param(
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [string]$Version = "1.1.0",
    [string]$Makensis = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root "dist"
$stage = Join-Path $dist "stage"
Remove-Item -Recurse -Force $dist -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage | Out-Null

$exe = Join-Path $BuildDir "bin\BetterGFNNeural.exe"
if (-not (Test-Path $exe)) { throw "missing $exe" }
Copy-Item $exe $stage
Copy-Item $exe $dist
foreach ($f in @("README.md", "THIRD_PARTY_NOTICES.md", "CHANGELOG.md", "BUILDING.md")) { Copy-Item (Join-Path $root $f) $stage }
Copy-Item (Join-Path $root "LICENSE") (Join-Path $stage "LICENSE.txt")
Copy-Item (Join-Path $root "resources\app.ico") $stage
Copy-Item -Recurse (Join-Path $root "models") (Join-Path $stage "models")
New-Item -ItemType Directory -Force -Path (Join-Path $stage "shaders") | Out-Null
Get-ChildItem (Join-Path $root "shaders") -File | Copy-Item -Destination (Join-Path $stage "shaders")
Copy-Item -Recurse (Join-Path $root "shaders\generated") (Join-Path $stage "shaders\generated")
Copy-Item -Recurse (Join-Path $root "docs") (Join-Path $stage "docs")

# ---- Portable ZIP
$portable = Join-Path $dist "portable"
Copy-Item -Recurse $stage $portable
Set-Content -Path (Join-Path $portable "portable.dat") -Value "Better GFN Neural portable mode: settings and logs are stored in this folder."
$zip = Join-Path $dist "BetterGFNNeural-$Version-portable.zip"
Compress-Archive -Path (Join-Path $portable "*") -DestinationPath $zip -Force
Remove-Item -Recurse -Force $portable

# ---- Installer
if (-not $Makensis) {
    foreach ($c in @("C:\Program Files (x86)\NSIS\makensis.exe", "C:\Program Files\NSIS\makensis.exe")) { if (Test-Path $c) { $Makensis = $c } }
}
if ($Makensis -and (Test-Path $Makensis)) {
    & $Makensis /V2 "/DVERSION=$Version" "/DSRCDIR=$stage" "/DOUTFILE=$(Join-Path $dist 'BetterGFNNeuralSetup.exe')" (Join-Path $root "installer\BetterGFNNeural.nsi")
    if ($LASTEXITCODE -ne 0) { throw "makensis failed" }
} else {
    Write-Warning "NSIS not found - installer not built"
}
Remove-Item -Recurse -Force $stage
$files = Get-ChildItem $dist -File | Sort-Object Name
$files | ForEach-Object { "{0,-45} {1,12:N0} bytes  SHA256 {2}" -f $_.Name, $_.Length, (Get-FileHash $_.FullName -Algorithm SHA256).Hash }
# sha256sum-compatible checksum list (verify with: Get-FileHash, or sha256sum -c SHA256SUMS.txt)
$files | ForEach-Object { "{0} *{1}" -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $_.Name } |
    Set-Content -Path (Join-Path $dist "SHA256SUMS.txt") -Encoding ascii
