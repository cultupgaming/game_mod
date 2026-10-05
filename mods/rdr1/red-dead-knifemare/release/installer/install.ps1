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

$RdrExe = Join-Path $GameDir "RDR.exe"
$ScriptHook = Join-Path $GameDir "ScriptHookRDR.dll"
$DInput = Join-Path $GameDir "dinput8.dll"
$StockMapres = Join-Path $GameDir "game\mapres.rpf"
$TargetAsi = Join-Path $GameDir "RedDeadKnifemare.asi"
$PayloadAsi = Join-Path $InstallerDir "payload\RedDeadKnifemare.asi"
$UiDir = Join-Path $InstallerDir "assets"
$MagicDir = Join-Path $InstallerDir "tools\MagicRDR"
$LoaderDll = Join-Path $InstallerDir "tools\ultimate-asi-loader\wininet.dll"
$LoaderIni = Join-Path $InstallerDir "tools\ultimate-asi-loader\wininet.ini"
$Radial = Join-Path $UiDir "radial_thrn_knife.wtd"
$Manual = Join-Path $UiDir "weapons_thrn_knife.wtd"
$MagicRdrUrl = "https://github.com/Foxxyyy/Magic-RDR/releases"

Require-File $RdrExe "RDR.exe"
Require-File $ScriptHook "ScriptHookRDR.dll"
Require-File $DInput "dinput8.dll"
Require-File $StockMapres "game\mapres.rpf"
Require-File $PayloadAsi "RedDeadKnifemare.asi payload"
Require-File $Radial "radial_thrn_knife.wtd"
Require-File $Manual "weapons_thrn_knife.wtd"
Require-File $LoaderDll "packaged wininet.dll"
Require-File $LoaderIni "packaged wininet.ini"

if ((Hash-File $Radial) -ne "F171C489A236AC9DCD358FAFEB7DFB4268AA96AA048D2CE9FE221119FD405BAA") { throw "Unexpected radial WTD hash." }
if ((Hash-File $Manual) -ne "946CB5DD359398A65EE5CE14C7FC40F1EF63647FD728E306EF57A5E1FC4F29D5") { throw "Unexpected weapons WTD hash." }

$MagicExeMatches = @()
if (Test-Path -LiteralPath $MagicDir -PathType Container) {
    $MagicExeMatches = @(Get-ChildItem -LiteralPath $MagicDir -Filter "MagicRDR.exe" -File -Recurse -ErrorAction SilentlyContinue)
}
if ($MagicExeMatches.Count -ne 1) {
    Write-Host ""
    Write-Host "MagicRDR is required to install the mandatory Assassin Knife UI."
    Write-Host "MagicRDR is not included in this package."
    Write-Host ""
    Write-Host "Download it from:"
    Write-Host "  $MagicRdrUrl"
    Write-Host ""
    Write-Host "Extract the MagicRDR release into:"
    Write-Host "  $MagicDir"
    Write-Host ""
    Write-Host "That folder must contain MagicRDR.exe and its Assemblies folder."
    Write-Host "Then run Install.bat again."
    Write-Host ""
    $OpenPage = Read-Host "Open the official MagicRDR releases page now? [Y/N]"
    if ($OpenPage -match '^[Yy]') { Start-Process $MagicRdrUrl }
    throw "MagicRDR is missing. No game files were changed."
}

$MagicExePath = $MagicExeMatches[0].FullName
$MagicWorkingDir = $MagicExeMatches[0].DirectoryName
Require-File (Join-Path $MagicWorkingDir "Assemblies\xcompress32.dll") "MagicRDR xcompress32.dll"
Require-File (Join-Path $MagicWorkingDir "Assemblies\PikIO.dll") "MagicRDR PikIO.dll"

if (Get-Process -Name "RDR" -ErrorAction SilentlyContinue) { throw "Close Red Dead Redemption before installing." }

$AssetDir = Join-Path $GameDir "RedDeadKnifemareAssets"
$StateFile = Join-Path $AssetDir "release-install-state.json"
if (Test-Path -LiteralPath $StateFile -PathType Leaf) {
    throw "A Knifemare install is already tracked. Run Uninstall.bat first, then install this package."
}

$UpdateGameDir = Join-Path $GameDir "update\game"
$UpdateMapres = Join-Path $UpdateGameDir "mapres.rpf"
$TargetLoaderDll = Join-Path $GameDir "wininet.dll"
$TargetLoaderIni = Join-Path $GameDir "wininet.ini"
$MapresBackup = Join-Path $AssetDir "release-mapres.before-rdk.rpf"
$LoaderDllBackup = Join-Path $AssetDir "release-wininet.before-rdk.dll"
$LoaderIniBackup = Join-Path $AssetDir "release-wininet.before-rdk.ini"
$PreviousAsiBackup = Join-Path $AssetDir "release-RedDeadKnifemare.before-install.asi"

foreach ($Backup in @($MapresBackup, $LoaderDllBackup, $LoaderIniBackup, $PreviousAsiBackup)) {
    if (Test-Path -LiteralPath $Backup -PathType Leaf) {
        throw "A stale installer backup exists without release-install-state.json: $Backup. Do not overwrite it until the previous failed/manual state is understood."
    }
}

$HadMapres = Test-Path -LiteralPath $UpdateMapres -PathType Leaf
$HadLoaderDll = Test-Path -LiteralPath $TargetLoaderDll -PathType Leaf
$HadLoaderIni = Test-Path -LiteralPath $TargetLoaderIni -PathType Leaf
$HadAsi = Test-Path -LiteralPath $TargetAsi -PathType Leaf

New-Item -ItemType Directory -Path $AssetDir -Force | Out-Null
New-Item -ItemType Directory -Path $UpdateGameDir -Force | Out-Null

try {
    Step "Backing up pre-existing files"
    if ($HadMapres) { Copy-Item -LiteralPath $UpdateMapres -Destination $MapresBackup -Force }
    if ($HadLoaderDll) { Copy-Item -LiteralPath $TargetLoaderDll -Destination $LoaderDllBackup -Force }
    if ($HadLoaderIni) { Copy-Item -LiteralPath $TargetLoaderIni -Destination $LoaderIniBackup -Force }
    if ($HadAsi) { Copy-Item -LiteralPath $TargetAsi -Destination $PreviousAsiBackup -Force }

    Step "Installing mandatory Assassin Knife UI"
    if (-not $HadMapres) { Copy-Item -LiteralPath $StockMapres -Destination $UpdateMapres -Force }
    $BaselineMapresHash = Hash-File $UpdateMapres

    Push-Location $MagicWorkingDir
    try {
        & $MagicExePath -replace $UpdateMapres "root\mapres" $UiDir -current
        $MagicExit = $LASTEXITCODE
    } finally { Pop-Location }
    if ($MagicExit -ne 0) { throw "MagicRDR failed with exit code $MagicExit." }

    $DeployedMapresHash = Hash-File $UpdateMapres
    if ($DeployedMapresHash -eq $BaselineMapresHash) { throw "mapres.rpf did not change after MagicRDR." }

    Step "Installing Knifemare runtime files"
    Copy-Item -LiteralPath $LoaderDll -Destination $TargetLoaderDll -Force
    Copy-Item -LiteralPath $LoaderIni -Destination $TargetLoaderIni -Force
    Copy-Item -LiteralPath $PayloadAsi -Destination $TargetAsi -Force

    if ((Hash-File $TargetLoaderDll) -ne (Hash-File $LoaderDll)) { throw "wininet.dll copy verification failed." }
    if ((Hash-File $TargetLoaderIni) -ne (Hash-File $LoaderIni)) { throw "wininet.ini copy verification failed." }
    if ((Hash-File $TargetAsi) -ne (Hash-File $PayloadAsi)) { throw "RedDeadKnifemare.asi copy verification failed." }

    [ordered]@{
        version = 1
        had_update_mapres = $HadMapres
        had_wininet_dll = $HadLoaderDll
        had_wininet_ini = $HadLoaderIni
        had_asi = $HadAsi
        mapres_baseline_sha256 = $BaselineMapresHash
        mapres_deployed_sha256 = $DeployedMapresHash
        mapres_backup_sha256 = $(if ($HadMapres) { Hash-File $MapresBackup } else { $null })
        wininet_dll_deployed_sha256 = (Hash-File $TargetLoaderDll)
        wininet_dll_backup_sha256 = $(if ($HadLoaderDll) { Hash-File $LoaderDllBackup } else { $null })
        wininet_ini_deployed_sha256 = (Hash-File $TargetLoaderIni)
        wininet_ini_backup_sha256 = $(if ($HadLoaderIni) { Hash-File $LoaderIniBackup } else { $null })
        asi_deployed_sha256 = (Hash-File $TargetAsi)
        asi_backup_sha256 = $(if ($HadAsi) { Hash-File $PreviousAsiBackup } else { $null })
    } | ConvertTo-Json | Set-Content -LiteralPath $StateFile -Encoding UTF8

    Step "Installation complete"
    Write-Host "Red Dead Knifemare and the mandatory Assassin Knife UI are installed. Start the game normally."
}
catch {
    Write-Warning "Install failed; restoring backed-up files."
    if ($HadMapres -and (Test-Path -LiteralPath $MapresBackup)) { Copy-Item -LiteralPath $MapresBackup -Destination $UpdateMapres -Force }
    elseif (-not $HadMapres -and (Test-Path -LiteralPath $UpdateMapres)) { Remove-Item -LiteralPath $UpdateMapres -Force }

    if ($HadLoaderDll -and (Test-Path -LiteralPath $LoaderDllBackup)) { Copy-Item -LiteralPath $LoaderDllBackup -Destination $TargetLoaderDll -Force }
    elseif (-not $HadLoaderDll -and (Test-Path -LiteralPath $TargetLoaderDll)) { Remove-Item -LiteralPath $TargetLoaderDll -Force }

    if ($HadLoaderIni -and (Test-Path -LiteralPath $LoaderIniBackup)) { Copy-Item -LiteralPath $LoaderIniBackup -Destination $TargetLoaderIni -Force }
    elseif (-not $HadLoaderIni -and (Test-Path -LiteralPath $TargetLoaderIni)) { Remove-Item -LiteralPath $TargetLoaderIni -Force }

    if ($HadAsi -and (Test-Path -LiteralPath $PreviousAsiBackup)) { Copy-Item -LiteralPath $PreviousAsiBackup -Destination $TargetAsi -Force }
    elseif (-not $HadAsi -and (Test-Path -LiteralPath $TargetAsi)) { Remove-Item -LiteralPath $TargetAsi -Force }

    if (Test-Path -LiteralPath $StateFile) { Remove-Item -LiteralPath $StateFile -Force }
    foreach ($Backup in @($MapresBackup, $LoaderDllBackup, $LoaderIniBackup, $PreviousAsiBackup)) {
        if (Test-Path -LiteralPath $Backup -PathType Leaf) { Remove-Item -LiteralPath $Backup -Force }
    }
    throw
}
