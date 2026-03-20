param(
    [string]$BuildDir = "build/msvc2019",
    [string]$QtRoot = "D:/Qt/6.2.0/msvc2019_64"
)

$ErrorActionPreference = "Stop"
[Console]::InputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
cmd /c chcp 65001 > $null

$vswhere = "C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found. Install Visual Studio 2019 with MSVC C++ tools."
}

$installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installationPath) {
    throw "Visual Studio 2019 with MSVC x64 tools was not found."
}

$vsDevCmd = Join-Path $installationPath "Common7/Tools/VsDevCmd.bat"
if (-not (Test-Path $vsDevCmd)) {
    throw "VsDevCmd.bat not found under $installationPath"
}

$qtCMake = Join-Path $QtRoot "bin/qt-cmake.bat"
if (-not (Test-Path $qtCMake)) {
    throw "qt-cmake.bat not found under $QtRoot"
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$quotedVsDevCmd = '"' + $vsDevCmd + '"'
$quotedQtCMake = '"' + $qtCMake + '"'
$command = "$quotedVsDevCmd -arch=x64 && $quotedQtCMake -S . -B $BuildDir -G Ninja"

cmd /c $command
