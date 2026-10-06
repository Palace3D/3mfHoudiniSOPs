cmake_minimum_required( VERSION 3.16 )

project( SOP_Read3mf )

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

set( library_name SOP_Read3mf )

# zlib (minizip's dependency), found with CMake's built-in module.
find_package( ZLIB REQUIRED )

# minizip, for reading and writing the 3mf zip archive. On Windows it comes from
# vcpkg (through the toolchain file given when configuring); on Linux, from the
# system's minizip package.
if( WIN32 )
    find_package( minizip CONFIG REQUIRED )
    set( MINIZIP_LIBRARY MINIZIP::minizip )
else()
    find_library( MINIZIP_LIBRARY NAMES minizip minizip-ng-compat minizip-ng PATHS /usr/lib64 )
    if( NOT MINIZIP_LIBRARY )
        message( FATAL_ERROR "minizip not found. Install your distro's minizip development package "
                             "(e.g. minizip-ng-compat-devel or minizip-devel)." )
    endif()
endif()

# Add a library and its source files.
add_library( ${library_name} SHARED
    SOP_Read3mf.C
    SOP_Read3mf.h
    tinyxml2.cpp
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

# Sets several common target properties, such as the library's output directory, and
# installs the built plugin into the user's houdiniX.Y/dso folder.
houdini_configure_target( ${library_name} )
