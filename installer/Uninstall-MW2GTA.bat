@echo off
title MW2-GTA uninstaller
if not exist "%~dp0installer\Uninstall-MW2GTA.ps1" goto notrelease
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0installer\Uninstall-MW2GTA.ps1" %*
echo.
pause
exit /b

:notrelease
echo The uninstaller scripts were not found next to this file.
echo.
echo Run Uninstall-MW2GTA.bat from the main folder of the release zip
echo (MW2-GTA-^<version^>.zip from https://github.com/Ermes-x86/MW2SA-Beta/releases).
echo.
pause
exit /b 1
