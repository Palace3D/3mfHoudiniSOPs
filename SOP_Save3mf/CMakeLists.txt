cmake_minimum_required( VERSION 3.16 )

# On Windows, use vcpkg's static minizip and zlib, built against the same dynamic C runtime Houdini uses
# (/MD). They are then linked into the plugin itself: no minizip.dll or z.dll next to it, and no PATH
# entry needed. This has to be set before project(), where the vcpkg toolchain reads it. Pass
# -DVCPKG_TARGET_TRIPLET=... when configuring to use a different triplet.
if( CMAKE_HOST_WIN32 AND NOT DEFINED VCPKG_TARGET_TRIPLET )
    set( VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "vcpkg triplet for minizip and zlib" )
endif()

project( SOP_Save3mf )

# CMAKE_PREFIX_PATH must contain the path to the toolkit/cmake subdirectory of
# the Houdini installation. HFS is set by Houdini's Command Line Tools shell on
# Windows, or by sourcing houdini_setup on Linux. See the "Compiling with CMake"
# section of the HDK documentation for other ways to specify this path.
# Nothing here depends on where this folder is, so the source can live anywhere.
if( NOT DEFINED ENV{HFS} OR "$ENV{HFS}" STREQUAL "" )
    message( FATAL_ERROR "HFS is not set, so CMake can't find Houdini. On Windows, configure from Houdini's "
                         "Command Line Tools shell. On Linux, first run 'source houdini_setup' in your Houdini "
                         "install folder, in the same terminal." )
endif()
message( STATUS "Building against Houdini at $ENV{HFS}" )
list( APPEND CMAKE_PREFIX_PATH "$ENV{HFS}/toolkit/cmake" )

# Locate Houdini's libraries and header files.
# Registers an imported library target named 'Houdini'.
find_package( Houdini REQUIRED )

set( library_name SOP_Save3mf )

# zlib (minizip's dependency), found with CMake's built-in module.
find_package( ZLIB REQUIRED )

# minizip, for reading and writing the 3mf zip archive. On Windows it comes from
# vcpkg (through the toolchain file given when configuring); on Linux, from the
# system's minizip package.
if( WIN32 )
    # vcpkg's minizip has named its CMake package and target differently over time
    # (minizip / unofficial-minizip; MINIZIP::minizip, minizip::minizip, ...), so take
    # whichever this vcpkg provides.
    find_package( minizip CONFIG QUIET )
    if( NOT minizip_FOUND )
        find_package( unofficial-minizip CONFIG QUIET )
    endif()
    set( MINIZIP_LIBRARY "" )
    get_property( _imported DIRECTORY PROPERTY IMPORTED_TARGETS )
    foreach( _t ${_imported} )
        string( TOLOWER "${_t}" _tl )
        if( _tl MATCHES "minizip" AND NOT MINIZIP_LIBRARY )
            set( MINIZIP_LIBRARY ${_t} )
        endif()
    endforeach()
    if( NOT MINIZIP_LIBRARY )
        message( FATAL_ERROR "minizip not found through vcpkg (triplet ${VCPKG_TARGET_TRIPLET}). Check that "
                             "'vcpkg install minizip:${VCPKG_TARGET_TRIPLET}' succeeded, and that the configure "
                             "command passes -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake. "
                             "Imported targets found: ${_imported}" )
    endif()
    message( STATUS "Using minizip target ${MINIZIP_LIBRARY}" )
else()
    find_library( MINIZIP_LIBRARY NAMES minizip minizip-ng-compat minizip-ng PATHS /usr/lib64 )
    if( NOT MINIZIP_LIBRARY )
        message( FATAL_ERROR "minizip not found. Install your distro's minizip development package "
                             "(e.g. minizip-ng-compat-devel or minizip-devel)." )
    endif()
endif()

# Add a library and its source files.
add_library( ${library_name} SHARED
    SOP_Save3mf.C
    SOP_Save3mf.h
)

# Link against the Houdini libraries (which also adds Houdini's include directories and
# compile definitions), minizip, and zlib.
target_link_libraries( ${library_name} Houdini ${MINIZIP_LIBRARY} ZLIB::ZLIB )

target_include_directories( ${library_name} PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
)
if( NOT WIN32 )
    # The system minizip headers live in their own subfolder.
    target_include_directories( ${library_name} PRIVATE /usr/include/minizip )
endif()

# Sets several common target properties, such as the library's output directory, so the
# built plugin goes straight into the user's houdiniX.Y/dso folder.
houdini_configure_target( ${library_name} )

# Optional: build the plugin into another folder instead of the user's houdiniX.Y/dso.
# packaging/build_release.bat sets this to the package layout's houdiniX.Y/dso folder, so
# release builds never touch your own Houdini preferences. Leave it empty for normal builds.
set( PALACE3D_DSO_DIR "" CACHE PATH "Folder to build the plugin into (empty: your houdiniX.Y/dso folder)" )
if( PALACE3D_DSO_DIR )
    file( TO_CMAKE_PATH "${PALACE3D_DSO_DIR}" _dso_dir )
    message( STATUS "Building the plugin into ${_dso_dir}" )
    # The plugin itself (a .dll is a "runtime" output, a .so a "library" output). The import
    # library (.lib/.exp) stays in the build folder, out of the package.
    foreach( _cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL )
        set_target_properties( ${library_name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY${_cfg} "${_dso_dir}"
            LIBRARY_OUTPUT_DIRECTORY${_cfg} "${_dso_dir}"
            ARCHIVE_OUTPUT_DIRECTORY${_cfg} "${CMAKE_CURRENT_BINARY_DIR}/lib"
        )
    endforeach()
endif()
