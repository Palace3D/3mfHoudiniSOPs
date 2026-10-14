@echo off
setlocal EnableExtensions

rem ===========================================================================
rem  test_package.bat
rem
rem  Installs the package staged by build_release.bat (dist\stage) into a
rem  separate, throwaway preferences folder, checks with hython that both SOPs
rem  load from it, then starts Houdini with that preferences folder so you can
rem  try the nodes by hand.
rem
rem  Your own preferences folder (and any source-built DLLs in its dso folder,
rem  and its houdini.env) is never read, so this tests what a new user gets.
rem
rem  Usage:  packaging\test_package.bat [22.0 or 20.5]     default 22.0
rem ===========================================================================

rem ---- Settings for this machine (same paths as build_release.bat) ----------
set "HOUDINI_22_0=C:\Program Files\Side Effects Software\Houdini 22.0.429"
set "HOUDINI_20_5="
set "TESTROOT=C:\houdini-package-test"
rem ----------------------------------------------------------------------------

set "HVER=%~1"
if "%HVER%"=="" set "HVER=22.0"
set "HPATH="
if "%HVER%"=="22.0" set "HPATH=%HOUDINI_22_0%"
if "%HVER%"=="20.5" set "HPATH=%HOUDINI_20_5%"
if "%HPATH%"=="" (
    echo ERROR: no install path set for Houdini %HVER%. Use 22.0 or 20.5, and fill in the path above.
    exit /b 1
)
if not exist "%HPATH%\bin\hython.exe" (
    echo ERROR: hython.exe not found in "%HPATH%\bin".
    exit /b 1
)

set "STAGE=%~dp0..\dist\stage"
if not exist "%STAGE%\palace3d_3mf.json" (
    echo ERROR: no staged package found. Run packaging\build_release.bat first.
    exit /b 1
)
if not exist "%STAGE%\palace3d_3mf\houdini%HVER%\dso\SOP_Read3mf.dll" (
    echo ERROR: the staged package has no Houdini %HVER% build.
    exit /b 1
)

rem Variables set for the whole system or account would leak into the test.
for %%V in (HOUDINI_PATH HOUDINI_DSO_PATH HOUDINI_PACKAGE_DIR HOUDINI_USER_PREF_DIR) do (
    if defined %%V echo WARNING: %%V is set in your environment; the test may not be isolated.
)

rem A fresh packages folder in the test preferences folder. Everything else
rem there (window layouts and so on) is kept between runs.
set "PREFS=%TESTROOT%\houdini%HVER%"
if exist "%PREFS%\packages" rmdir /s /q "%PREFS%\packages"
mkdir "%PREFS%\packages"
if errorlevel 1 exit /b 1
xcopy /e /i /q /y "%STAGE%" "%PREFS%\packages" >nul
if errorlevel 1 (
    echo ERROR: couldn't copy the package into %PREFS%\packages.
    exit /b 1
)
echo Installed the package into %PREFS%\packages

set "HOUDINI_USER_PREF_DIR=%TESTROOT:\=/%/houdini__HVER__"
set "HOUDINI_PACKAGE_VERBOSE=1"
set "HOUDINI_DSO_ERROR=2"

echo.
echo === Checking with hython ===
"%HPATH%\bin\hython.exe" "%~dp0check_package.py"
if errorlevel 1 (
    echo.
    echo The package check failed; not starting Houdini.
    exit /b 1
)

echo.
echo === Starting Houdini %HVER% with the test preferences folder ===
echo Try both nodes from the Tab menu, and right-click ^> Help on each.
start "" "%HPATH%\bin\houdini.exe"
exit /b 0
