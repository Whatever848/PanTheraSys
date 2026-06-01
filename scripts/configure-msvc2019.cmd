@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0configure-msvc2019.ps1" %*
exit /b %ERRORLEVEL%
