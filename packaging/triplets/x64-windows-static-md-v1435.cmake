# vcpkg triplet for the Houdini 20.5 build: like x64-windows-static-md (static
# libraries, dynamic C runtime /MD), but built with MSVC build tools 14.35, the
# compiler Houdini 20.5 itself was built with. The plugins for 20.5 are built
# with 14.35 too, and its linker can't read libraries made by a newer compiler.
#
# Used by build_release.bat through --overlay-triplets / VCPKG_OVERLAY_TRIPLETS.
# Needs "MSVC v143 - VS 2022 C++ x64/x86 build tools (v14.35-17.5)" installed
# from the Visual Studio Installer.

set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_PLATFORM_TOOLSET v143)
set(VCPKG_PLATFORM_TOOLSET_VERSION 14.35)
