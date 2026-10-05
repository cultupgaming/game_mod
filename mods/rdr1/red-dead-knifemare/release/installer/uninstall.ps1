param([string]$GameDir)

$ErrorActionPreference = "Stop"

function Require-File([string]$Path, [string]$Name) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "$Name not found: $Path" }
}
function Hash-File([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function Step([string]$Text) {
    Write-Host ""
    Write-Host "============================================================"
    Write-Host $Text
    Write-Host "============================================================"
}

$InstallerDir = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($GameDir)) { $GameDir = Split-Path -Parent $InstallerDir }
if (-not (Test-Path -LiteralPath $GameDir -PathType Container)) { throw "Game folder not found: $GameDir" }
$GameDir = (Resolve-Path -LiteralPath $GameDir).Path
Require-File (Join-Path $GameDir "RDR.exe") "RDR.exe"

if (Get-Process -Name "RDR" -ErrorAction SilentlyContinue) { throw "Close Red Dead Redemption before uninstalling." }

$AssetDir = Join-Path $GameDir "RedDeadKnifemareAssets"
$StateFile = Join-Path $AssetDir "release-install-state.json"
Require-File $StateFile "Red Dead Knifemare Nexus install state"
$State = Get-Content -LiteralPath $StateFile -Raw | ConvertFrom-Json

$TargetAsi = Join-Path $GameDir "RedDeadKnifemare.asi"
$UpdateMapres = Join-Path $GameDir "update\game\mapres.rpf"
$TargetLoaderDll = Join-Path $GameDir "wininet.dll"
$TargetLoaderIni = Join-Path $GameDir "wininet.ini"
$MapresBackup = Join-Path $AssetDir "release-mapres.before-rdk.rpf"
$LoaderDllBackup = Join-Path $AssetDir "release-wininet.before-rdk.dll"
$LoaderIniBackup = Join-Path $AssetDir "release-wininet.before-rdk.ini"
$PreviousAsiBackup = Join-Path $AssetDir "release-RedDeadKnifemare.before-install.asi"

Step "Validating Knifemare-owned files before uninstall"
Require-File $UpdateMapres "Knifemare update\game\mapres.rpf"
Require-File $TargetLoaderDll "Knifemare wininet.dll"
Require-File $TargetLoaderIni "Knifemare wininet.ini"
Require-File $TargetAsi "RedDeadKnifemare.asi"

if ((Hash-File $UpdateMapres) -ne [string]$State.mapres_deployed_sha256) {
    throw "update\game\mapres.rpf changed after Knifemare installation. Refusing to overwrite another mod's changes."
}
if ((Hash-File $TargetLoaderDll) -ne [string]$State.wininet_dll_deployed_sha256) {
    throw "wininet.dll changed after Knifemare installation. Refusing to overwrite another mod's changes."
}
if ((Hash-File $TargetLoaderIni) -ne [string]$State.wininet_ini_deployed_sha256) {
    throw "wininet.ini changed after Knifemare installation. Refusing to overwrite another mod's changes."
}
if ((Hash-File $TargetAsi) -ne [string]$State.asi_deployed_sha256) {
    throw "RedDeadKnifemare.asi changed after installation. Refusing to remove an untracked replacement."
}

if ([bool]$State.had_update_mapres) {
    Require-File $MapresBackup "pre-Knifemare mapres backup"
    if ((Hash-File $MapresBackup) -ne [string]$State.mapres_backup_sha256) { throw "The saved pre-Knifemare mapres backup changed. Refusing an unsafe restore." }
}
if ([bool]$State.had_wininet_dll) {
    Require-File $LoaderDllBackup "pre-Knifemare wininet.dll backup"
    if ((Hash-File $LoaderDllBackup) -ne [string]$State.wininet_dll_backup_sha256) { throw "The saved pre-Knifemare wininet.dll backup changed. Refusing an unsafe restore." }
}
if ([bool]$State.had_wininet_ini) {
    Require-File $LoaderIniBackup "pre-Knifemare wininet.ini backup"
    if ((Hash-File $LoaderIniBackup) -ne [string]$State.wininet_ini_backup_sha256) { throw "The saved pre-Knifemare wininet.ini backup changed. Refusing an unsafe restore." }
}
if ([bool]$State.had_asi) {
    Require-File $PreviousAsiBackup "pre-install RedDeadKnifemare.asi backup"
    if ((Hash-File $PreviousAsiBackup) -ne [string]$State.asi_backup_sha256) { throw "The saved pre-install ASI backup changed. Refusing an unsafe restore." }
}

Step "Restoring pre-install files"
if ([bool]$State.had_update_mapres) { Copy-Item -LiteralPath $MapresBackup -Destination $UpdateMapres -Force }
else { Remove-Item -LiteralPath $UpdateMapres -Force }

if ([bool]$State.had_wininet_dll) { Copy-Item -LiteralPath $LoaderDllBackup -Destination $TargetLoaderDll -Force }
else { Remove-Item -LiteralPath $TargetLoaderDll -Force }

if ([bool]$State.had_wininet_ini) { Copy-Item -LiteralPath $LoaderIniBackup -Destination $TargetLoaderIni -Force }
else { Remove-Item -LiteralPath $TargetLoaderIni -Force }

if ([bool]$State.had_asi) { Copy-Item -LiteralPath $PreviousAsiBackup -Destination $TargetAsi -Force }
else { Remove-Item -LiteralPath $TargetAsi -Force }

foreach ($Path in @($StateFile, $MapresBackup, $LoaderDllBackup, $LoaderIniBackup, $PreviousAsiBackup)) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) { Remove-Item -LiteralPath $Path -Force }
}

$RuntimeTrace = Join-Path $GameDir "RedDeadKnifemare.trace.log"
$TempRuntimeTrace = Join-Path ([System.IO.Path]::GetTempPath()) "RedDeadKnifemare.trace.log"
foreach ($Path in @($RuntimeTrace, $TempRuntimeTrace)) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) { Remove-Item -LiteralPath $Path -Force }
}

foreach ($Directory in @((Join-Path $GameDir "update\game"), (Join-Path $GameDir "update"), $AssetDir)) {
    if (Test-Path -LiteralPath $Directory -PathType Container) {
        $Remaining = @(Get-ChildItem -LiteralPath $Directory -Force)
        if ($Remaining.Count -eq 0) { Remove-Item -LiteralPath $Directory -Force }
    }
}

Step "Uninstall complete"
Write-Host "Red Dead Knifemare files were removed and tracked pre-install files were restored."
Write-Host "You can now delete Install.bat, Uninstall.bat, README.md and RedDeadKnifemareInstaller if you no longer need the package."
