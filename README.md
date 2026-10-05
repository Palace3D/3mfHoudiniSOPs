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

## Color Handling

3mf colors are sRGB, but Houdini's `Cd` is linear. Both SOPs have a
**Convert sRGB** toggle, on by default, that converts between the
two: `Save3mf` encodes `Cd` as sRGB when it writes, and `Read3mf`
decodes the file's colors back to linear when it reads. Without the
conversion, colors made in Houdini look darker once they leave it
(in a slicer, in other viewers, and on the printed part) than they
did in the viewport. Texture images are not changed either way.

**Files from Save3mf 3.0 and earlier.** Before version 3.1,
`Save3mf` wrote linear `Cd` values into the file without converting
them. Those files hold the right numbers for Houdini but the wrong
ones for everything else, which is why parts exported that way print
darker than expected. The writing version is recorded in the file's
`Application` metadata (`Houdini 3MF Export 3.0`), and `Read3mf`
puts it in the `in_application` detail attribute.

When `Read3mf` sees such a file with Convert sRGB on, and colors the
conversion would change, it puts a warning on the node. To get the
original colors back, turn Convert sRGB **off** on `Read3mf` and
press Read. To repair the file itself, read it with Convert sRGB off
and save it with Convert sRGB on. Re-exporting from the original
scene is better if you still have it, since the old files kept the
linear values in 8 bits and the dark tones are coarse.

Files from other applications are sRGB by the spec, so leave
Convert sRGB on for them.

**Multiproperties blending.** When `Read3mf` stacks multiproperties
layers, each layer goes over the ones beneath it by its own alpha (a
colorgroup color's alpha, or a texture image's alpha channel). The
blend is done in linear light, as the 3mf Materials Specification
recommends. Some other applications, including the renderer behind the
thumbnails in the 3mf Consortium's test suites, blend the sRGB values
directly, which makes blends darker and lets a dark, partly opaque
layer show more strongly over a bright one. So blended areas can look
lighter and smoother in Houdini than in those thumbnails; that
difference is expected.

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

- Multiproperties are supported, with colorgroup, texture, and base
  material layers and their alphas, except for the `multiply` blend
  method. A multiproperties group whose `blendmethods` asks for
  `multiply` is currently blended with the default `mix` instead, and
  Read3mf puts a warning on the node naming the group.
- Texture `filter` settings are not currently supported. A texture
  that sets one is read with the default filter, and Read3mf puts a
  warning on the node.
- Save3mf currently writes the whole input as a single 3mf object.
  A part read from a file with several objects is therefore saved
  back as one object; the `object_id` and `mesh_name` attributes
  Read3mf adds are not used when saving.
- We do not currently support the Composite Materials extension
  (`<m:compositematerials>`). A composite materials group that an
  object or triangle refers to directly is skipped, with a warning
  in the console, and whatever refers to it gets the object's
  default color (white if the object has none). A composite
  materials group used as a layer of a multiproperties group makes
  the import fail with an error whenever that multiproperties group
  is used.
- We currently read only the root model file in a 3mf package. Models
  whose objects live in other `.model` files, which the Production
  Extension allows through a `p:path` attribute on components and
  build items, are not supported. The other files are not parsed,
  and the import stops with an error naming the file as soon as it
  meets a component or build item that points into one.
- Read3mf currently understands only the Materials and Production
  extensions. A file can list the extensions it can't be read
  correctly without (its `requiredextensions`); if that list names any
  others, such as Slice or Beam Lattice, Read3mf still reads the file
  but puts a warning on the node naming them, since whatever depends on
  them is ignored.
