param(
    [string]$BuildDir = "build/msvc2019",
    [string]$QtRoot = "D:/Qt/6.2.0/msvc2019_64"
)

$ErrorActionPreference = "Stop"
[Console]::InputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
cmd /c chcp 65001 > $null

$qtBin = Join-Path $QtRoot "bin"
$appPath = Join-Path $BuildDir "apps/console/PanTheraConsole.exe"

if (-not (Test-Path $appPath)) {
    throw "Application not found: $appPath. Build the project first."
}

$env:PATH = "$qtBin;$env:PATH"
& $appPath
