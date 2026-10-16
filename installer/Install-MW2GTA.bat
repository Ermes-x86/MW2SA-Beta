@echo off
title MW2-GTA installer
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0installer\Install-MW2GTA.ps1" %*
if errorlevel 1 echo. & echo Installation did not complete.
echo.
pause
