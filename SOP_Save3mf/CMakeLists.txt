cmake_minimum_required( VERSION 3.6 )

project( HDK_Project )

# CMAKE_PREFIX_PATH must contain the path to the toolkit/cmake subdirectory of
# the Houdini installation. See the "Compiling with CMake" section of the HDK
# documentation for more details, which describes several options for
# specifying this path.
list( APPEND CMAKE_PREFIX_PATH "$ENV{HFS}/toolkit/cmake" )

# Locate Houdini's libraries and header files.
# Registers an imported library target named 'Houdini'.
find_package( Houdini REQUIRED )

set( library_name SOP_Save3mf )

# Code generation for the embedded DS file in SOP_Save3mf.C.
#houdini_generate_proto_headers( FILES SOP_Save3mf.C )

# MY ADDITIONS!
# Use CMake's built-in module to find the ZLIB library (Minizip's dependency)
find_package( ZLIB REQUIRED )

if( WIN32 )
    find_package( minizip CONFIG REQUIRED )
    set( MINIZIP_LIBRARY MINIZIP::minizip )
else()
    find_library( MINIZIP_LIBRARY NAMES minizip minizip-ng-compat minizip-ng PATHS /usr/lib64 )
endif()

set( library_name SOP_Save3mf )

# Add a library and its source files.
add_library( ${library_name} SHARED
    SOP_Save3mf.C
    SOP_Save3mf.h
)

# Link against the Houdini libraries, and add required include directories and
# compile definitions.
# MY ADDITIONS are the ZLIB and MINIZIP pieces
target_link_libraries( ${library_name} Houdini ${MINIZIP_LIBRARY} )

# Include ${CMAKE_CURRENT_BINARY_DIR} for the generated header.
# MY ADDITIONS are the minizip include
target_include_directories( ${library_name} PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
)
if( NOT WIN32 )
    target_include_directories( ${library_name} PRIVATE /usr/include/minizip )
endif()


# Sets several common target properties, such as the library's output directory.
houdini_configure_target( ${library_name} )
