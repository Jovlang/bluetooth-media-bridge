$ErrorActionPreference = 'Stop'
Start-Process -FilePath (Join-Path $PSScriptRoot 'bin\media-bridge.exe') -WorkingDirectory $PSScriptRoot -WindowStyle Hidden
