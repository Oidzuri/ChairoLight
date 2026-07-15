[CmdletBinding()]
param(
    [string]$RepositoryRoot,
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [string]$WindeployQt
)

$ErrorActionPreference = "Stop"
if (-not $RepositoryRoot) {
    $RepositoryRoot = Join-Path $PSScriptRoot "..\.."
}
$RepositoryRoot = (Resolve-Path $RepositoryRoot).Path
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $RepositoryRoot "Software\bin"
}
$BuildDirectory = (Resolve-Path $BuildDirectory).Path

$version = (Get-Content (Join-Path $RepositoryRoot "Software\RELEASE_VERSION") -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$') {
    throw "Invalid release version: $version"
}

$artifactRoot = Join-Path $RepositoryRoot "artifacts"
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $artifactRoot "ChairoLight-$version-portable"
}
$artifactRoot = [IO.Path]::GetFullPath($artifactRoot)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $OutputDirectory.StartsWith($artifactRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputDirectory must be inside $artifactRoot"
}
$zipPath = "$OutputDirectory.zip"

$sourceExe = Join-Path $BuildDirectory "Prismatik.exe"
if (-not (Test-Path $sourceExe)) {
    throw "Release executable was not found: $sourceExe"
}

if (Test-Path $OutputDirectory) {
    Remove-Item -LiteralPath $OutputDirectory -Recurse -Force
}
if (Test-Path $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

Copy-Item -LiteralPath $sourceExe -Destination (Join-Path $OutputDirectory "Prismatik.exe")
foreach ($companion in @("libraryinjector.dll", "prismatik-unhook.dll")) {
    $path = Join-Path $BuildDirectory $companion
    if (Test-Path $path) {
        Copy-Item -LiteralPath $path -Destination $OutputDirectory
    }
}

if (-not $WindeployQt) {
    $command = Get-Command windeployqt -ErrorAction SilentlyContinue
    if (-not $command) {
        $command = Get-Command windeployqt-qt5 -ErrorAction SilentlyContinue
    }
    if ($command) {
        $WindeployQt = $command.Source
    }
}
if (-not $WindeployQt -or -not (Test-Path $WindeployQt)) {
    throw "windeployqt was not found. Pass -WindeployQt with the Qt deployment tool path."
}

& $WindeployQt --release --no-translations --no-angle --no-opengl-sw --no-system-d3d-compiler --compiler-runtime --dir $OutputDirectory (Join-Path $OutputDirectory "Prismatik.exe")
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

foreach ($document in @("LICENSE", "NOTICE.md", "THIRD_PARTY_LICENSES.md", "README.md", "CHANGELOG.md", "KNOWN_ISSUES.md")) {
    Copy-Item -LiteralPath (Join-Path $RepositoryRoot $document) -Destination $OutputDirectory
}

# Prismatik's compatibility layer treats the application directory as portable
# only when this marker exists. Keep it empty so no developer setting, device or
# COM port is shipped; the application populates defaults on the user's first run.
New-Item -ItemType File -Path (Join-Path $OutputDirectory "main.conf") -Force | Out-Null

$qtBin = Split-Path -Parent $WindeployQt
$licenseRoot = Join-Path (Split-Path -Parent $qtBin) "share\licenses"
$licenseCopies = @(
    @{ Source = "qt5-base\LICENSE.LGPL3"; Destination = "Qt\LICENSE.LGPL3" },
    @{ Source = "qt5-base\LICENSE.GPL3-EXCEPT"; Destination = "Qt\LICENSE.GPL3-EXCEPT" },
    @{ Source = "gcc-libs\COPYING.LIB"; Destination = "GCC\COPYING.LIB" },
    @{ Source = "gcc-libs\COPYING.RUNTIME"; Destination = "GCC\COPYING.RUNTIME" },
    @{ Source = "winpthreads\COPYING"; Destination = "WinPthreads\COPYING" }
)
foreach ($license in $licenseCopies) {
    $source = Join-Path $licenseRoot $license.Source
    if (-not (Test-Path $source)) {
        throw "Required dependency license was not found: $source"
    }
    $destination = Join-Path (Join-Path $OutputDirectory "licenses") $license.Destination
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
}

# A release package must never inherit the developer's portable profile.
$forbiddenNames = @("Profiles", "profile.ini", "settings.ini")
$forbidden = Get-ChildItem -LiteralPath $OutputDirectory -Recurse -Force | Where-Object {
    $forbiddenNames -contains $_.Name
}
if ($forbidden) {
    throw "Personal configuration found in portable output: $($forbidden.FullName -join ', ')"
}

$required = @("Prismatik.exe", "Qt5Core.dll", "Qt5Gui.dll", "Qt5Widgets.dll", "Qt5Network.dll", "Qt5SerialPort.dll", "main.conf", "LICENSE", "NOTICE.md", "THIRD_PARTY_LICENSES.md", "licenses\Qt\LICENSE.LGPL3")
foreach ($file in $required) {
    if (-not (Test-Path (Join-Path $OutputDirectory $file))) {
        throw "Required portable file is missing: $file"
    }
}
if ((Get-Item (Join-Path $OutputDirectory "main.conf")).Length -ne 0) {
    throw "Portable main.conf marker must be empty"
}

$manifest = Get-ChildItem -LiteralPath $OutputDirectory -File -Recurse |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($OutputDirectory.Length + 1).Replace('\', '/')
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $relative"
    }
$manifest | Set-Content -LiteralPath (Join-Path $OutputDirectory "SHA256SUMS.txt") -Encoding ascii

New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null
Compress-Archive -Path (Join-Path $OutputDirectory "*") -DestinationPath $zipPath -CompressionLevel Optimal

Write-Host "Portable directory: $OutputDirectory"
Write-Host "Portable archive:   $zipPath"
