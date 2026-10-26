@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "APP_EXE=%SCRIPT_DIR%build\mingw\UimLimitSensorTest.exe"

if not exist "%APP_EXE%" (
    echo [ERROR] UimLimitSensorTest.exe not found:
    echo         %APP_EXE%
    echo.
    echo Build it first from tools\uim_limit_sensor_test:
    echo   cmake --preset mingw-debug
    echo   cmake --build --preset mingw-debug
    pause
    exit /b 1
)

start "UIM Limit Sensor Test" "%APP_EXE%"
endlocal
