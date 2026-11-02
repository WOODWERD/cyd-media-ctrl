# Installs the Media Ctrl helper for this Windows user and starts it now and at each sign-in.
$ErrorActionPreference = 'Stop'
$dir = Join-Path $env:LOCALAPPDATA 'MediaCtrl'
$exe = Join-Path $dir 'MediaCtrlHelper.exe'
$source = Join-Path $PSScriptRoot 'MediaCtrlHelper.exe'

if (-not (Test-Path $source)) { throw "MediaCtrlHelper.exe needs to be in the same folder as this script." }
Get-Process MediaCtrlHelper -ErrorAction SilentlyContinue | Stop-Process -Force
New-Item -ItemType Directory -Force -Path $dir | Out-Null
Copy-Item $source $exe -Force
Unblock-File $exe
Set-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'MediaCtrlHelper' -Value "`"$exe`""
Start-Process $exe
Write-Host "Media Ctrl helper installed and running. Log: $dir\MediaCtrl.log"
