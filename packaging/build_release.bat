@echo off
setlocal EnableExtensions

rem ===========================================================================
rem  build_release.bat
rem
rem  Builds SOP_Read3mf and SOP_Save3mf in Release for each Houdini version
rem  set below, stages the Houdini package layout in dist\stage, and zips it
rem  into dist\palace3d_3mf-<version>-windows.zip.
rem
rem  Usage, from a normal Command Prompt (not Houdini's Command Line Tools
rem  shell; this script sets HFS itself for each version):
rem      packaging\build_release.bat 1.0.0
rem  The version only names the zip; it defaults to "dev".
rem
rem  Versions whose install path is left empty are skipped. The layout is
rem  still staged for test_package.bat, but no zip is made, so a release zip
rem  never goes out missing one of its Houdini versions.
rem ===========================================================================

rem ---- Settings for this machine ---------------------------------------------
set "HOUDINI_22_0=C:\Program Files\Side Effects Software\Houdini 22.0.429"
set "TOOLSET_22_0="

rem Houdini 20.5: fill these in once it's installed. TOOLSET picks an MSVC
rem toolset inside 20.5's range (hcustom --output_compiler_range), in the form
rem v143,version=14.NN
set "HOUDINI_20_5="
set "TOOLSET_20_5="

rem vcpkg folder (the one containing vcpkg.exe). If left empty, the
rem VCPKG_ROOT environment variable is used.
set "VCPKG_DIR=C:\dev\vcpkg"
rem ----------------------------------------------------------------------------

set "TRIPLET=x64-windows-static-md"
set "RELVER=%~1"
if "%RELVER%"=="" set "RELVER=dev"

if "%VCPKG_DIR%"=="" set "VCPKG_DIR=%VCPKG_ROOT%"
if "%VCPKG_DIR%"=="" (
    echo ERROR: Set VCPKG_DIR at the top of this script to your vcpkg folder.
    exit /b 1
)
if not exist "%VCPKG_DIR%\vcpkg.exe" (
    echo ERROR: vcpkg.exe not found in "%VCPKG_DIR%".
    exit /b 1
)
where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: cmake is not on PATH.
    exit /b 1
)

pushd "%~dp0.."
set "ROOT=%CD%"
set "STAGE=%ROOT%\dist\stage"
set "PKG=%STAGE%\palace3d_3mf"
set "MISSING="

echo.
echo === Making sure vcpkg has static minizip and zlib [%TRIPLET%] ===
"%VCPKG_DIR%\vcpkg.exe" install minizip:%TRIPLET%
if errorlevel 1 goto :fail

if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%PKG%\help\nodes\sop"
if errorlevel 1 goto :fail

call :build_version 22.0 "%HOUDINI_22_0%" "%TOOLSET_22_0%"
if errorlevel 1 goto :fail
call :build_version 20.5 "%HOUDINI_20_5%" "%TOOLSET_20_5%"
if errorlevel 1 goto :fail

echo.
echo === Staging the package ===
copy /y "%ROOT%\packaging\palace3d_3mf.json" "%STAGE%\" >nul
if errorlevel 1 goto :fail
copy /y "%ROOT%\SOP_Read3mf\hdk_read3mf.txt" "%PKG%\help\nodes\sop\" >nul
if errorlevel 1 goto :fail
copy /y "%ROOT%\SOP_Save3mf\hdk_save3mf.txt" "%PKG%\help\nodes\sop\" >nul
if errorlevel 1 goto :fail
echo Staged in %STAGE%

if defined MISSING (
    echo.
    echo No zip made: not built for Houdini%MISSING%.
    echo The staged package can still be tried with packaging\test_package.bat.
    goto :done
)

set "ZIP=%ROOT%\dist\palace3d_3mf-%RELVER%-windows.zip"
if exist "%ZIP%" del "%ZIP%"
rem tar is built into Windows 10 and 11; -a picks the zip format from the name.
tar -a -c -f "%ZIP%" -C "%STAGE%" palace3d_3mf.json palace3d_3mf
if errorlevel 1 goto :fail
echo.
echo === Done: %ZIP% ===

:done
popd
exit /b 0

:fail
echo.
echo *** build_release.bat FAILED, see the messages above. ***
popd
exit /b 1


rem ---------------------------------------------------------------------------
rem  :build_version <X.Y> "<Houdini install folder>" "<toolset or empty>"
rem ---------------------------------------------------------------------------
:build_version
set "HVER=%~1"
set "HPATH=%~2"
set "HTOOLSET=%~3"
if "%HPATH%"=="" (
    echo.
    echo === Skipping Houdini %HVER%: no install path set ===
    set "MISSING=%MISSING% %HVER%"
    exit /b 0
)
if not exist "%HPATH%\toolkit\cmake\HoudiniConfig.cmake" (
    echo ERROR: "%HPATH%" doesn't look like a Houdini install - no toolkit\cmake\HoudiniConfig.cmake.
    exit /b 1
)
set "HFS=%HPATH%"
set "DSO=%PKG%\houdini%HVER%\dso"
set "BUILDDIR=%ROOT%\build-%HVER%"
set "TOOLSET_ARG="
if not "%HTOOLSET%"=="" set "TOOLSET_ARG=-T %HTOOLSET%"

rem A fresh build folder each time: CMake remembers the compiler it chose first.
if exist "%BUILDDIR%" rmdir /s /q "%BUILDDIR%"

call :build_sop SOP_Read3mf
if errorlevel 1 exit /b 1
call :build_sop SOP_Save3mf
if errorlevel 1 exit /b 1

rem Static linking means the dso folder should hold only the two plugins.
for %%F in ("%DSO%\*") do (
    if /i not "%%~nxF"=="SOP_Read3mf.dll" if /i not "%%~nxF"=="SOP_Save3mf.dll" (
        echo ERROR: unexpected file %%~nxF in %DSO%. Was minizip or zlib linked as a DLL?
        exit /b 1
    )
)
exit /b 0


rem ---------------------------------------------------------------------------
rem  :build_sop <SOP folder name>   (uses HVER, HFS, DSO, BUILDDIR, TOOLSET_ARG)
rem ---------------------------------------------------------------------------
:build_sop
echo.
echo === Houdini %HVER%: %1 ===
cmake -S "%ROOT%\%1" -B "%BUILDDIR%\%1" -A x64 %TOOLSET_ARG% "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_DIR%\scripts\buildsystems\vcpkg.cmake" "-DVCPKG_TARGET_TRIPLET=%TRIPLET%" "-DPALACE3D_DSO_DIR=%DSO%"
if errorlevel 1 exit /b 1
cmake --build "%BUILDDIR%\%1" --config Release
if errorlevel 1 exit /b 1
if not exist "%DSO%\%1.dll" (
    echo ERROR: %1.dll was not built into %DSO%.
    exit /b 1
)
exit /b 0
