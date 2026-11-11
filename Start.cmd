@echo off
if not exist "%~dp0bin\media-bridge.exe" (
    echo Build the bridge with build.cmd first.
    pause
    exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0Start.ps1"
