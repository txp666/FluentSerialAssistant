$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildPreset = "mingw-release"
$buildDir = Join-Path $repoRoot "build\mingw-release"
$outputDir = Join-Path $repoRoot "dist\FluentSerialAssistant-release"
$exeName = "FluentSerialAssistant.exe"
$builtExe = Join-Path $buildDir $exeName
$packageExe = Join-Path $outputDir $exeName

$outputFullPath = [System.IO.Path]::GetFullPath($outputDir)
$repoPrefix = $repoRoot.TrimEnd('\') + '\'
if (-not $outputFullPath.StartsWith($repoPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Output directory must stay inside the repository: $outputFullPath"
}

$windeployqt = (Get-Command windeployqt.exe -ErrorAction SilentlyContinue).Source
if (-not $windeployqt) {
    $candidate = "C:\Qt\6.11.1\mingw_64\bin\windeployqt.exe"
    if (Test-Path $candidate) {
        $windeployqt = $candidate
    }
}
if (-not $windeployqt) {
    throw "windeployqt.exe was not found. Add the Qt bin directory to PATH or install the Qt MinGW kit."
}

$strip = (Get-Command strip.exe -ErrorAction SilentlyContinue).Source
if (-not $strip) {
    $candidate = "C:\Qt\Tools\mingw1310_64\bin\strip.exe"
    if (Test-Path $candidate) {
        $strip = $candidate
    }
}

cmake --preset $buildPreset
cmake --build --preset $buildPreset --parallel

if (-not (Test-Path $builtExe)) {
    throw "Release executable was not produced: $builtExe"
}

if (Test-Path $outputFullPath) {
    Remove-Item -LiteralPath $outputFullPath -Recurse -Force
}

New-Item -ItemType Directory -Force $outputFullPath | Out-Null
Copy-Item -Force $builtExe $packageExe

& $windeployqt `
    --release `
    --no-translations `
    --no-system-d3d-compiler `
    --no-system-dxc-compiler `
    --no-opengl-sw `
    --no-ffmpeg `
    --skip-plugin-types generic,tls,networkinformation,styles `
    --exclude-plugins qgif,qjpeg,qico `
    $packageExe

if ($strip) {
    Get-ChildItem $outputFullPath -Recurse -File |
        Where-Object { $_.Extension -in ".exe", ".dll" } |
        ForEach-Object {
            & $strip --strip-unneeded $_.FullName
        }

    Get-ChildItem $outputFullPath -Recurse -File |
        Where-Object { [string]::IsNullOrEmpty($_.Extension) -and $_.Name -like "st*" } |
        Remove-Item -Force
}

$packageSize = (Get-ChildItem $outputFullPath -Recurse -File | Measure-Object Length -Sum).Sum
Write-Host ("Package: {0}" -f $outputFullPath)
Write-Host ("Size: {0:N2} MB" -f ($packageSize / 1MB))
