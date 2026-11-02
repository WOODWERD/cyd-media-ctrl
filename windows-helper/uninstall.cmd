@echo off
taskkill /IM MediaCtrlHelper.exe /F
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v MediaCtrlHelper /f
rmdir /S /Q "%LOCALAPPDATA%\MediaCtrl"
echo Media Ctrl helper removed.
pause
