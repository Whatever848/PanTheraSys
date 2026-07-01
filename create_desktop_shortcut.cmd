@echo off
setlocal

set "PROJECT_ROOT=D:\PanSoftware\PanTheraSys"
set "LAUNCHER=%PROJECT_ROOT%\launch_pantherasys.cmd"
set "ICON=%PROJECT_ROOT%\build\mingw_vcpkg\apps\console\PanTheraConsole.exe"

if not exist "%LAUNCHER%" (
    echo Cannot find launcher: %LAUNCHER%
    echo.
    pause
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -Command "$desktop=[Environment]::GetFolderPath('Desktop'); $path=Join-Path $desktop 'PanTheraSys.lnk'; $shell=New-Object -ComObject WScript.Shell; $shortcut=$shell.CreateShortcut($path); $shortcut.TargetPath='%LAUNCHER%'; $shortcut.WorkingDirectory='%PROJECT_ROOT%'; $shortcut.IconLocation='%ICON%,0'; $shortcut.Description='Launch PanTheraSys'; $shortcut.Save(); Write-Host ('Created: ' + $path)"

if errorlevel 1 (
    echo.
    pause
    exit /b 1
)

echo.
echo Done.
pause
