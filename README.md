# Houdini 3mf importer and exporter
SOP nodes in C++ for importing and exporting 3mf files. These supercede earlier python versions.

## Installation
These folders are designed to be built within the Houdini HDK samples hierarchy.
1. Place folders in `$HFS/toolkit/samples/SOP/`.
2. Use the provided CmakeLists.txt files and use make in the build subdirectory

## Dependencies
We require tinyxml2.h, tinyxml2.cpp, stb_image_write.h, and stb_image.h currently. They are included in the repository currently.

## Status and Limitations
We currently cannot import 3mf files using multi-properties.
