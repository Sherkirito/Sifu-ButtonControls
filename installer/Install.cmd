@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Manage-Bundled.ps1" -Mode Install %*
echo.
pause
