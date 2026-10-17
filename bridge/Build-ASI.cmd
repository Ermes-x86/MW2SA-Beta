@echo off
rem Builds bridge\build\MW2GTA.asi (x86, GTA SA 1.0 US).
rem Requirements:
rem   - Visual Studio Build Tools with the C++ x86 toolset
rem   - plugin-sdk (https://github.com/DK22Pac/plugin-sdk) built for GTA SA,
rem     path given by PLUGIN_SDK_DIR (Plugin.lib in %PLUGIN_SDK_DIR%\output\lib)
setlocal
if "%PLUGIN_SDK_DIR%"=="" (
  echo Set PLUGIN_SDK_DIR to your plugin-sdk folder, e.g.
  echo   set PLUGIN_SDK_DIR=C:\dev\plugin-sdk
  exit /b 1
)
set SDK=%PLUGIN_SDK_DIR%
if "%VCVARS32%"=="" (
  for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VCVARS32=%%i\VC\Auxiliary\Build\vcvars32.bat
)
if not exist "%VCVARS32%" (
  echo Could not find vcvars32.bat. Install Visual Studio Build Tools with C++ or set VCVARS32.
  exit /b 1
)
call "%VCVARS32%" >nul
if not exist "%~dp0build" mkdir "%~dp0build"
cl /nologo /std:c++latest /O2 /MT /EHsc /LD /DGTASA /DRW /DPLUGIN_SGV_10US /D_CRT_SECURE_NO_WARNINGS /d1trimfile:"%SDK%\\" /d1trimfile:"%~dp0\" /I"%SDK%\shared" /I"%SDK%\shared\game" /I"%SDK%\shared\injector" /I"%SDK%\plugin_sa" /I"%SDK%\plugin_sa\game_sa" /I"%SDK%\plugin_sa\game_sa\enums" /I"%SDK%\plugin_sa\game_sa\rw" /I"%SDK%\shared\dxsdk" "%~dp0src\MW2GTA.cpp" /Fo"%~dp0build\MW2GTA.obj" /link /OUT:"%~dp0build\MW2GTA.asi" /PDB:"%~dp0build\MW2GTA.pdb" "%SDK%\output\lib\Plugin.lib" user32.lib gdi32.lib advapi32.lib
exit /b %errorlevel%
