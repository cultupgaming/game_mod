param(
    [string]$SdkDir = "C:\modding\RDR1-SDK",
    [switch]$AllowNonMain,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

function Step([string]$Message) {
    Write-Host ""
    Write-Host "============================================================"
    Write-Host $Message
    Write-Host "============================================================"
}
function Require-File([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "$Description not found: $Path" }
}
function Require-Command([string]$Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) { throw "$Name was not found in PATH." }
}
function Select-CMakeGenerator {
    $HelpText = (& cmake --help 2>&1 | Out-String)
    foreach ($Candidate in @("Visual Studio 18 2026", "Visual Studio 17 2022")) {
        if ($HelpText.Contains($Candidate)) { return $Candidate }
    }
    throw "No supported Visual Studio CMake generator found. Install Visual Studio 2022/2026 with Desktop development with C++."
}

Step "Validating Red Dead Knifemare release inputs"
Require-Command "git"
Require-Command "cmake"
Require-Command "Compress-Archive"

$ModDir = $PSScriptRoot
$RepoDir = (& git -C $ModDir rev-parse --show-toplevel).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($RepoDir)) { throw "Could not resolve repository root." }
$RepoDir = (Resolve-Path -LiteralPath $RepoDir).Path

$VersionFile = Join-Path $ModDir "VERSION"
$TraceSource = Join-Path $ModDir "src\trace.cpp"
$MainSource = Join-Path $ModDir "src\main.cpp"
$CMakeLists = Join-Path $ModDir "CMakeLists.txt"
$ReadmeSource = Join-Path $ModDir "release\README.md"
$InstallBatSource = Join-Path $ModDir "release\Install.bat"
$UninstallBatSource = Join-Path $ModDir "release\Uninstall.bat"
$InstallPsSource = Join-Path $ModDir "release\installer\install.ps1"
$UninstallPsSource = Join-Path $ModDir "release\installer\uninstall.ps1"
$UiAssetDir = Join-Path $ModDir "assets\streaming\assassin_knife_ui_wtds"
$Radial = Join-Path $UiAssetDir "radial_thrn_knife.wtd"
$Manual = Join-Path $UiAssetDir "weapons_thrn_knife.wtd"
$UalDll = Join-Path $ModDir "tools\ultimate-asi-loader\wininet.dll"
$UalIni = Join-Path $ModDir "tools\ultimate-asi-loader\wininet.ini"
$UalLicense = Join-Path $ModDir "tools\ultimate-asi-loader\LICENSE.txt"
$MagicRdrUrl = "https://github.com/Foxxyyy/Magic-RDR/releases"
$PublicRepoUrl = "https://github.com/cultupgaming/game_mod.git"
$PublicRepoBrowseUrl = "https://github.com/cultupgaming/game_mod"

Require-File $VersionFile "VERSION file"
Require-File $TraceSource "trace.cpp"
Require-File $MainSource "main.cpp"
Require-File $CMakeLists "CMakeLists.txt"
Require-File $ReadmeSource "release README"
Require-File $InstallBatSource "Install.bat"
Require-File $UninstallBatSource "Uninstall.bat"
Require-File $InstallPsSource "release install.ps1"
Require-File $UninstallPsSource "release uninstall.ps1"
if (-not (Test-Path -LiteralPath $UiAssetDir -PathType Container)) { throw "Assassin Knife UI asset directory not found: $UiAssetDir" }
Require-File $Radial "radial_thrn_knife.wtd"
Require-File $Manual "weapons_thrn_knife.wtd"
Require-File $UalDll "Ultimate ASI Loader wininet.dll"
Require-File $UalIni "Ultimate ASI Loader wininet.ini"
Require-File $UalLicense "Ultimate ASI Loader LICENSE.txt"

$Version = (Get-Content -LiteralPath $VersionFile -Raw).Trim()
if ($Version -notmatch '^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$') { throw "Invalid semantic VERSION: '$Version'" }

$Branch = (& git -C $RepoDir branch --show-current).Trim()
if ([string]::IsNullOrWhiteSpace($Branch)) { throw "Repository is in detached HEAD state." }
if ($Branch -ne "main" -and -not $AllowNonMain) { throw "Release packaging is restricted to main. Use -AllowNonMain only to test this script before merge." }

$Dirty = @(git -C $RepoDir status --short)
if ($Dirty.Count -gt 0) {
    $Dirty | ForEach-Object { Write-Host "  $_" }
    throw "Working tree is not clean. Exact-source release packaging requires committed source only."
}

$Commit = (& git -C $RepoDir rev-parse HEAD).Trim()
$ShortCommit = (& git -C $RepoDir rev-parse --short HEAD).Trim()
if ($Commit -notmatch '^[0-9a-fA-F]{40}$') { throw "Could not resolve a full 40-character source commit SHA." }

Step "Verifying exact public source commit"
$PublicBranchRef = "refs/heads/$Branch"
$PublicRefText = (& git ls-remote $PublicRepoUrl $PublicBranchRef | Out-String).Trim()
if ($LASTEXITCODE -ne 0) { throw "Could not query the public source repository: $PublicRepoBrowseUrl" }
if ([string]::IsNullOrWhiteSpace($PublicRefText)) {
    throw "Public branch '$Branch' was not found in $PublicRepoBrowseUrl. Push this exact branch before packaging."
}
$PublicCommit = (($PublicRefText -split '\s+')[0]).Trim()
if ($PublicCommit -notmatch '^[0-9a-fA-F]{40}$') { throw "Could not resolve the public commit for $PublicBranchRef." }
if ($PublicCommit -ne $Commit) {
    throw "Release blocked: local commit $Commit does not match published public $PublicBranchRef ($PublicCommit). Push the exact commit and retry."
}
$ExactSourceUrl = "$PublicRepoBrowseUrl/tree/$Commit/mods/rdr1/red-dead-knifemare"
Write-Host "Verified public source: $PublicBranchRef -> $Commit"
Write-Host "Exact source URL: $ExactSourceUrl"

$TraceText = Get-Content -LiteralPath $TraceSource -Raw
if (-not $TraceText.Contains("constexpr bool TRACE_ENABLED = false;") -or $TraceText.Contains("constexpr bool TRACE_ENABLED = true;")) {
    throw "Release blocked: runtime tracing is not explicitly disabled."
}
$MainText = Get-Content -LiteralPath $MainSource -Raw
if (-not $MainText.Contains("constexpr bool KEYBOARD_HOTKEYS_ENABLED = false;") -or $MainText.Contains("constexpr bool KEYBOARD_HOTKEYS_ENABLED = true;")) {
    throw "Release blocked: function-key hotkeys are not explicitly disabled."
}

$ReadmeText = Get-Content -LiteralPath $ReadmeSource -Raw
foreach ($Text in @("v$Version", "Install.bat", "Uninstall.bat", "RedDeadKnifemare.asi", "Assassin Knife", "MagicRDR")) {
    if (-not $ReadmeText.Contains($Text)) { throw "Release README is missing expected text: $Text" }
}

Require-File (Join-Path $SdkDir "inc\natives.h") "ScriptHookRDR natives.h"
Require-File (Join-Path $SdkDir "lib\ScriptHookRDR.lib") "ScriptHookRDR.lib"

$BuildRoot = Join-Path $ModDir "build"
$BuildDir = Join-Path $BuildRoot "release-cmake"
$StageDir = Join-Path $BuildRoot "release-stage"
$ArchivePath = Join-Path (Join-Path $ModDir "release") "Red-Dead-Knifemare-v$Version.zip"

if (Test-Path -LiteralPath $ArchivePath -PathType Leaf) {
    if (-not $Force) { throw "Release archive already exists: $ArchivePath. Use -Force to replace it deliberately." }
    Remove-Item -LiteralPath $ArchivePath -Force
}

Step "Verifying mandatory Assassin Knife UI payload"
$UiFiles = @(Get-ChildItem -LiteralPath $UiAssetDir -File -Filter "*.wtd")
if ($UiFiles.Count -ne 2) { throw "Assassin Knife UI asset directory must contain exactly two WTD files." }
if ((Get-FileHash -LiteralPath $Radial -Algorithm SHA256).Hash -ne "F171C489A236AC9DCD358FAFEB7DFB4268AA96AA048D2CE9FE221119FD405BAA") { throw "Unexpected radial WTD hash." }
if ((Get-FileHash -LiteralPath $Manual -Algorithm SHA256).Hash -ne "946CB5DD359398A65EE5CE14C7FC40F1EF63647FD728E306EF57A5E1FC4F29D5") { throw "Unexpected weapons WTD hash." }

Step "Building clean Release ASI"
foreach ($Path in @($BuildDir, $StageDir)) { if (Test-Path -LiteralPath $Path) { Remove-Item -LiteralPath $Path -Recurse -Force } }
$Generator = Select-CMakeGenerator
& cmake -S $ModDir -B $BuildDir -G $Generator -A x64 "-DRDR_SDK_DIR=$SdkDir"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE." }
& cmake --build $BuildDir --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Release build failed with exit code $LASTEXITCODE." }
$BuiltAsi = Join-Path $BuildDir "bin\RedDeadKnifemare.asi"
Require-File $BuiltAsi "Built RedDeadKnifemare.asi"

Step "Creating release package"
$InstallerStage = Join-Path $StageDir "RedDeadKnifemareInstaller"
$PayloadStage = Join-Path $InstallerStage "payload"
$AssetsStage = Join-Path $InstallerStage "assets"
$ToolsStage = Join-Path $InstallerStage "tools"
$MagicStage = Join-Path $ToolsStage "MagicRDR"
$UalStage = Join-Path $ToolsStage "ultimate-asi-loader"
foreach ($Path in @($StageDir,$InstallerStage,$PayloadStage,$AssetsStage,$ToolsStage,$MagicStage,$UalStage)) {
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
}

Copy-Item -LiteralPath $ReadmeSource -Destination (Join-Path $StageDir "README.md") -Force
Copy-Item -LiteralPath $InstallBatSource -Destination (Join-Path $StageDir "Install.bat") -Force
Copy-Item -LiteralPath $UninstallBatSource -Destination (Join-Path $StageDir "Uninstall.bat") -Force
Copy-Item -LiteralPath $InstallPsSource -Destination (Join-Path $InstallerStage "install.ps1") -Force
Copy-Item -LiteralPath $UninstallPsSource -Destination (Join-Path $InstallerStage "uninstall.ps1") -Force
Copy-Item -LiteralPath $BuiltAsi -Destination (Join-Path $PayloadStage "RedDeadKnifemare.asi") -Force
Copy-Item -LiteralPath $Radial -Destination (Join-Path $AssetsStage "radial_thrn_knife.wtd") -Force
Copy-Item -LiteralPath $Manual -Destination (Join-Path $AssetsStage "weapons_thrn_knife.wtd") -Force
Copy-Item -LiteralPath $UalDll -Destination (Join-Path $UalStage "wininet.dll") -Force
Copy-Item -LiteralPath $UalIni -Destination (Join-Path $UalStage "wininet.ini") -Force
Copy-Item -LiteralPath $UalLicense -Destination (Join-Path $UalStage "LICENSE.txt") -Force

$MagicInstructions = @"
MagicRDR is required to install the mandatory Assassin Knife UI, but it is not redistributed in this package.

1. Download MagicRDR from the official releases page:
   $MagicRdrUrl
2. Extract the downloaded MagicRDR release.
3. Copy the extracted MagicRDR files into this folder.
4. Make sure this folder contains MagicRDR.exe and its Assemblies folder.
5. Run Install.bat again.

The installer will refuse to install Knifemare without the Assassin Knife UI.
"@
Set-Content -LiteralPath (Join-Path $MagicStage "DOWNLOAD-MAGICRDR.txt") -Value $MagicInstructions -Encoding UTF8

$SourceText = @"
Red Dead Knifemare v$Version
Source commit: $Commit
Exact source: $ExactSourceUrl
Public repository: $PublicRepoBrowseUrl
Verified public ref: $PublicBranchRef

The release script verified that the local build commit exactly matched the published public ref above before building the package.

The Assassin Knife UI is mandatory for this release.
MagicRDR is intentionally not redistributed. Users must download it from:
$MagicRdrUrl
"@
Set-Content -LiteralPath (Join-Path $StageDir "SOURCE.txt") -Value $SourceText -Encoding UTF8

foreach ($Required in @(
    (Join-Path $StageDir "README.md"),
    (Join-Path $StageDir "SOURCE.txt"),
    (Join-Path $StageDir "Install.bat"),
    (Join-Path $StageDir "Uninstall.bat"),
    (Join-Path $InstallerStage "install.ps1"),
    (Join-Path $InstallerStage "uninstall.ps1"),
    (Join-Path $PayloadStage "RedDeadKnifemare.asi"),
    (Join-Path $AssetsStage "radial_thrn_knife.wtd"),
    (Join-Path $AssetsStage "weapons_thrn_knife.wtd"),
    (Join-Path $MagicStage "DOWNLOAD-MAGICRDR.txt"),
    (Join-Path $UalStage "wininet.dll"),
    (Join-Path $UalStage "wininet.ini"),
    (Join-Path $UalStage "LICENSE.txt")
)) { Require-File $Required "Required staged file" }

$NestedArchives = @(Get-ChildItem -LiteralPath $StageDir -Recurse -File | Where-Object { $_.Extension -in @(".zip", ".7z", ".rar", ".tar", ".gz") })
if ($NestedArchives.Count -gt 0) { throw "Release contains nested archives: $($NestedArchives.FullName -join ', ')" }

$Forbidden = @(Get-ChildItem -LiteralPath $StageDir -Recurse -File | Where-Object {
    ($_.Extension -in @(".log", ".pdb", ".wav", ".cpp", ".h", ".lib", ".obj", ".exp", ".ilk")) -or
    ($_.Name -like "*.trace.*") -or ($_.Name -like "runtime-*.md")
})
if ($Forbidden.Count -gt 0) { throw "Release contains forbidden development/debug files: $($Forbidden.FullName -join ', ')" }

$MagicRdrFiles = @(Get-ChildItem -LiteralPath $StageDir -Recurse -File | Where-Object { $_.Name -eq "MagicRDR.exe" -or $_.FullName -match '[\\/]MagicRDR[\\/].*\.dll$' })
if ($MagicRdrFiles.Count -gt 0) { throw "Release must not redistribute MagicRDR binaries: $($MagicRdrFiles.FullName -join ', ')" }

$ExeFiles = @(Get-ChildItem -LiteralPath $StageDir -Recurse -File -Filter "*.exe")
if ($ExeFiles.Count -gt 0) { throw "Release must not contain executable files: $($ExeFiles.FullName -join ', ')" }

$AsiFiles = @(Get-ChildItem -LiteralPath $StageDir -Recurse -File -Filter "*.asi")
if ($AsiFiles.Count -ne 1 -or $AsiFiles[0].Name -ne "RedDeadKnifemare.asi") {
    throw "Release must contain exactly one ASI: RedDeadKnifemare.asi"
}

$DllFiles = @(Get-ChildItem -LiteralPath $StageDir -Recurse -File -Filter "*.dll")
$ExpectedLoaderDll = Join-Path $UalStage "wininet.dll"
$UnexpectedDlls = @($DllFiles | Where-Object { $_.FullName -ne $ExpectedLoaderDll })
if ($UnexpectedDlls.Count -gt 0 -or $DllFiles.Count -ne 1) {
    throw "Release may contain only the expected Ultimate ASI Loader wininet.dll. Found: $($DllFiles.FullName -join ', ')"
}

Compress-Archive -Path (Join-Path $StageDir "*") -DestinationPath $ArchivePath -CompressionLevel Optimal
Require-File $ArchivePath "Release ZIP"
$AsiHash = (Get-FileHash -LiteralPath (Join-Path $PayloadStage "RedDeadKnifemare.asi") -Algorithm SHA256).Hash
$ZipHash = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash
$ZipSize = (Get-Item -LiteralPath $ArchivePath).Length

Step "Release package ready"
Write-Host "Red Dead Knifemare v$Version"
Write-Host "Source commit: $ShortCommit ($Commit)"
Write-Host "Exact source: $ExactSourceUrl"
Write-Host "Verified public ref: $PublicBranchRef"
Write-Host "Package: $ArchivePath"
Write-Host "Size: $ZipSize bytes"
Write-Host "ASI SHA256: $AsiHash"
Write-Host "ZIP SHA256: $ZipHash"
Write-Host ""
Write-Host "MagicRDR is not included. Users must supply it before Install.bat can install the mandatory Assassin Knife UI."
Write-Host "Test flow: extract the ZIP into the RDR game folder and double-click Install.bat."
