@echo off
title MW2-GTA uninstaller
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0installer\Uninstall-MW2GTA.ps1" %*
echo.
pause
