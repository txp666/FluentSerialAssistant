[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDir,
    [string]$OutputDir = (Join-Path $PSScriptRoot "..\dist"),
    [ValidateSet("Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [string]$Version = "",
    [string]$QtBinDir = "",
    [string]$InnoSetupCompilerPath = "",
    [string]$SourceDir = "",
    [switch]$StageOnly
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Resolve-Tool([string]$Name, [string]$ExplicitPath) {
    if ($ExplicitPath) {
        return (Resolve-Path -LiteralPath $ExplicitPath -ErrorAction Stop).Path
    }
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        throw "Required tool '$Name' was not found."
    }
    return $command.Source
}

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw "package_windows.ps1 must run on Windows."
}

$BuildDir = (Resolve-Path -LiteralPath $BuildDir -ErrorAction Stop).Path
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

$versionFile = Join-Path $BuildDir "FluentSerialAssistant-version.txt"
if (-not (Test-Path -LiteralPath $versionFile -PathType Leaf)) {
    throw "Version file is missing. Configure the CMake build first: $versionFile"
}
$configuredVersion = (Get-Content -LiteralPath $versionFile -Raw).Trim()
if (-not $Version) {
    $Version = $configuredVersion
}
if ($Version -ne $configuredVersion) {
    throw "Requested version '$Version' does not match CMake project version '$configuredVersion'."
}
if ($Version -notmatch '^\d+\.\d+\.\d+([-.][0-9A-Za-z.-]+)?$') {
    throw "Invalid release version: $Version"
}

$packageName = "FluentSerialAssistant-$Version-windows-x64"
$stageRoot = Join-Path $OutputDir "stage"
$stageDir = Join-Path $stageRoot $packageName

if ($SourceDir) {
    if ($StageOnly) {
        throw "SourceDir and StageOnly cannot be used together."
    }
    $packageSource = (Resolve-Path -LiteralPath $SourceDir -ErrorAction Stop).Path
} else {
    $expectedStagePrefix = [IO.Path]::GetFullPath($stageRoot).TrimEnd('\') + '\'
    $resolvedStageDir = [IO.Path]::GetFullPath($stageDir)
    if (-not $resolvedStageDir.StartsWith($expectedStagePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing unsafe stage directory: $resolvedStageDir"
    }
    if (Test-Path -LiteralPath $stageDir) {
        Remove-Item -LiteralPath $stageDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $stageDir -Force | Out-Null

    Write-Host "Installing $packageName into the staging directory..."
    & cmake --install $BuildDir --config $Configuration --component Runtime --prefix $stageDir
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --install failed with exit code $LASTEXITCODE."
    }

    $executable = Join-Path $stageDir "FluentSerialAssistant.exe"
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "Installed executable is missing: $executable"
    }

    $windeployqt = if ($QtBinDir) {
        Resolve-Tool "windeployqt.exe" (Join-Path $QtBinDir "windeployqt.exe")
    } else {
        Resolve-Tool "windeployqt.exe" ""
    }
    Write-Host "Deploying Qt runtime dependencies..."
    & $windeployqt `
        --release `
        --no-translations `
        --no-system-d3d-compiler `
        --no-system-dxc-compiler `
        --no-opengl-sw `
        --no-ffmpeg `
        --dir $stageDir `
        $executable
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed with exit code $LASTEXITCODE."
    }

    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "..\README.md") -Destination $stageDir
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "..\CHANGELOG.md") -Destination $stageDir
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "..\LICENSE") -Destination $stageDir
    $packageSource = $stageDir
}

$packageExecutable = Join-Path $packageSource "FluentSerialAssistant.exe"
if (-not (Test-Path -LiteralPath $packageExecutable -PathType Leaf)) {
    throw "Package source does not contain FluentSerialAssistant.exe: $packageSource"
}

if ($StageOnly) {
    Write-Host "Staged: $packageSource"
    exit 0
}

$iscc = Resolve-Tool "ISCC.exe" $InnoSetupCompilerPath
$installerScript = (Resolve-Path -LiteralPath (
    Join-Path $PSScriptRoot "..\packaging\windows\FluentSerialAssistant.iss"
) -ErrorAction Stop).Path
$installerBaseName = "$packageName-setup"
$installerPath = Join-Path $OutputDir "$installerBaseName.exe"

Write-Host "Creating Inno Setup installer..."
& $iscc `
    "/Qp" `
    "/O$OutputDir" `
    "/F$installerBaseName" `
    "/DAppVersion=$Version" `
    "/DSourceDir=$packageSource" `
    $installerScript
if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup compilation failed with exit code $LASTEXITCODE."
}
if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf)) {
    throw "Inno Setup did not create the expected installer: $installerPath"
}

Write-Host "Created: $installerPath"
