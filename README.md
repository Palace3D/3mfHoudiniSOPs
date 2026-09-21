# Houdini 3mf importer and exporter

SOP nodes in C++ for importing and exporting 3mf files. These
supersede earlier Python versions.

Builds and runs on both Linux and Windows.

## Installation

These folders are designed to be built within the Houdini HDK
samples hierarchy.

1. Place the `SOP_Save3mf` and `SOP_Read3mf` folders in
   `$HFS/toolkit/samples/SOP/`.
2. Follow the platform-specific build steps below.

### Building on Fedora / Linux

Prerequisites: a C++ compiler (gcc), CMake, and the `minizip` and
`zlib` development packages (e.g. `minizip-ng-compat-devel` or
`minizip-devel`, and `zlib-devel`, depending on your distro's
package names).

From inside each SOP's folder:
```
mkdir build && cd build
cmake ..
make
```
This installs the built `.so` into `$HOME/houdiniX.Y/dso/`
automatically via `houdini_configure_target()`.

### Building on Windows

Prerequisites:
- **Visual Studio** (Community edition is fine) with the
  **Desktop development with C++** workload. Check
  `hcustom --output_compiler_range` (from Houdini's Command Line
  Tools shell) for the exact accepted compiler version range for
  your Houdini install.
- **CMake** (standalone install from cmake.org, added to `PATH`).
- **[vcpkg](https://github.com/microsoft/vcpkg)**, with minizip
  installed:
  ```
  git clone https://github.com/microsoft/vcpkg
  cd vcpkg
  .\bootstrap-vcpkg.bat
  .\vcpkg install minizip
  ```

From inside each SOP's folder, using hcmd.exe, Houdini's **Command Line
Tools** shell (Start menu, under Houdini's install; this loads the
`HFS` environment variable CMake needs) for the configure step:
```
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```
The second command (the actual build) can be re-run from a regular
terminal afterward — only the first (configure) step needs
Houdini's environment. This installs the built `.dll` (plus its
`minizip.dll`/`z.dll` runtime dependencies) into
`%HOMEPATH%\Documents\houdiniX.Y\dso\` automatically.

**Required extra step — add the `dso` folder to `PATH` via
`houdini.env`.** Without this, the plugin builds fine but fails to
load in Houdini with no visible error message in the normal GUI.
Windows doesn't reliably resolve a plugin DLL's own sibling
dependencies (here, `minizip.dll`/`z.dll`) from the same folder
it's loaded from. Add this line to
`%HOMEPATH%\Documents\houdiniX.Y\houdini.env`:
```
PATH = "<your-houdini-user-pref-dir>/dso;$PATH"
```
e.g. `C:/Users/<you>/Documents/houdini22.0/dso;$PATH`. Houdini
reads `houdini.env` automatically at every startup.

If a node doesn't appear in the Tab menu after building, the
plugin likely failed to load silently. To see the actual reason,
set `HOUDINI_DSO_ERROR=2` in your environment and launch
`hbatch.exe` (not `houdinifx.exe` — the GUI executable doesn't
print to a console) from a terminal.

## Node Help

Each SOP has help documentation, written in SideFX's wiki markup
format, alongside its source: `SOP_Save3mf/hdk_save3mf.txt` and
`SOP_Read3mf/hdk_read3mf.txt`.

To make Houdini show this as real, working node help (right-click
a node > Help), move both files into Houdini's shared node-help
folder:
```
$HOUDINI_USER_PREF_DIR/help/nodes/sop/
```
e.g. `%HOMEPATH%\Documents\houdiniX.Y\help\nodes\sop\` on Windows,
or `$HOME/houdiniX.Y/help/nodes/sop/` on Fedora/Linux. The `help`,
`nodes`, and `sop` folders don't exist by default — create them if
needed. No restart required; Houdini's help browser picks these up
on demand. The filenames must match the SOPs' internal operator
names (`hdk_save3mf`, `hdk_read3mf`), not their Tab-menu labels.

## Debug Logging

Both SOPs have a `Debug` toggle that prints extra diagnostic
information to the console. It's hidden from the parameter
interface by default, since it's a development/troubleshooting
aid rather than something end users need day-to-day.

To turn it on for a specific node: right-click the node and choose
**Parameters and Channels > Edit Parameter Interface**. Under
**Existing Parameters**, turn on **Show Invisible Parameters** —
`Debug` will then appear in that list. Click it, then turn off the
**Invisible** toggle on its Parameter Description. This reveals the
toggle on that one node instance only — every other instance,
including newly-dropped ones, still starts with it hidden.

## Dependencies

Vendored in this repository (no separate install needed):
- `tinyxml2.h` / `tinyxml2.cpp`
- `stb_image.h`
- `stb_image_write.h`

Not vendored — install via your platform's package manager
(Fedora) or vcpkg (Windows), as described above:
- `minizip`
- `zlib`

## Status and Limitations

- We currently cannot import 3mf files using multi-properties.
  Many examples will work, but not all — this is a known,
  ongoing limitation (currently caused by how layers are being
  mapped onto Houdini shaders).
- We do not correctly handle 3mf files that use the Composite
  Materials extension (`<m:compositematerials>`). A multiproperties
  layer referencing a composite-materials resource is not parsed as
  color, base material, or texture, so it's cleanly rejected with an
  error rather than silently misinterpreted.
