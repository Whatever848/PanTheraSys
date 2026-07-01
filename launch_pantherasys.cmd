@echo off
setlocal

set "PROJECT_ROOT=D:\PanSoftware\PanTheraSys"
set "APP_EXE=%PROJECT_ROOT%\build\mingw_vcpkg\apps\console\PanTheraConsole.exe"

if not exist "%APP_EXE%" (
    set "APP_EXE=%PROJECT_ROOT%\build\mingw-env\apps\console\PanTheraConsole.exe"
)

if not exist "%APP_EXE%" (
    echo Cannot find PanTheraConsole.exe.
    echo Please build the project first, then run this launcher again.
    echo.
    pause
    exit /b 1
)

cd /d "%PROJECT_ROOT%"
start "PanTheraSys" "%APP_EXE%"
