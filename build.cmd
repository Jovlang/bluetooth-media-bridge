@echo off
setlocal
cd /d "%~dp0"
where cl.exe >nul 2>nul
if errorlevel 1 (
    echo Open an x64 Native Tools Command Prompt for Visual Studio 2022 first.
    exit /b 1
)
if not defined WindowsSdkDir (
    echo Windows SDK environment is missing. Use a Visual Studio developer prompt.
    exit /b 1
)
if not exist bin mkdir bin
if not exist build mkdir build
rc /nologo /fo build\media-bridge.res media-bridge.rc
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /I "%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt" media-bridge.cpp build\media-bridge.res /Fo:build\media-bridge.obj /Fe:bin\media-bridge.exe /link windowsapp.lib user32.lib shell32.lib
exit /b %errorlevel%
