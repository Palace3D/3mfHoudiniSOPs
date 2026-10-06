# Houdini 3mf importer and exporter

SOP nodes in C++ for importing and exporting 3mf files. These
supersede earlier Python versions.

Builds and runs on both Linux and Windows.

## Installation

These SOPs are built from source against your own Houdini install,
using the HDK (Houdini Development Kit) that comes with every copy of
Houdini, in `$HFS/toolkit/`. They have been built and tested with
**Houdini 22.0**. A plugin built for one Houdini version (e.g. 22.0)
will not load in another (e.g. 20.5): you have to build it against
the version you'll run it in, and older versions may need small code
changes.

1. Clone this repository, or download it as a ZIP from GitHub's
   **Code** button and unzip it, anywhere you like in a folder you
   own (e.g. `C:\Users\<you>\Documents\src\` on Windows, or
   `~/src/` on Linux):
   ```
   git clone https://github.com/Palace3D/3mfHoudiniSOPs.git
   ```
   This gives you a `3mfHoudiniSOPs` folder containing the
   `SOP_Read3mf` and `SOP_Save3mf` folders you'll build in. The
   build finds Houdini through the `HFS` environment variable, so
   the source doesn't need to be inside Houdini's install or its
   `toolkit` folder. (Avoid putting it inside Houdini's own install
   under `C:\Program Files`: Windows only lets an administrator write
   there.)
2. Follow the platform-specific build steps below.

Throughout, `houdiniX.Y` means the folder for **your** Houdini
version, e.g. `houdini22.0` or `houdini20.5`. Copying an example path
with the wrong version in it is an easy mistake to make.

### Building on Fedora / Linux

Prerequisites: a C++ compiler (gcc), CMake, and the `minizip` and
`zlib` development packages (e.g. `minizip-ng-compat-devel` or
`minizip-devel`, and `zlib-devel`, depending on your distro's
package names).

First set up Houdini's environment in your shell, so CMake can find
the HDK. This sets `HFS`, and without it the configure step fails
because it can't find Houdini:
```
cd /opt/hfsX.Y.ZZZ
source houdini_setup
```
Then, from inside each SOP's folder:
```
mkdir build && cd build
cmake ..
make
```
This installs the built `.so` into `$HOME/houdiniX.Y/dso/`
automatically via `houdini_configure_target()`.

### Building on Windows

#### Prerequisites

- **Visual Studio**, with the **Desktop development with C++**
  workload. The free Community edition is fine. Note that Visual
  Studio is a different product from Visual Studio Code; VS Code
  does not include the C++ compiler you need.
- **CMake**, the standalone install from cmake.org, added to `PATH`
  (the installer offers this).
- **Git**, which vcpkg needs.
- **[vcpkg](https://github.com/microsoft/vcpkg)**, with minizip
  installed. Install Visual Studio first; vcpkg uses its compiler.
  ```
  git clone https://github.com/microsoft/vcpkg
  cd vcpkg
  .\bootstrap-vcpkg.bat
  .\vcpkg install minizip
  ```

#### Check your compiler version

Each Houdini version only loads plugins built with a certain range
of Microsoft C++ compiler versions. A brand-new Visual Studio can
easily be *newer* than an older Houdini accepts, and the plugin then
fails to load with "Incompatible compiler versions". Check before
you build:

1. Open Houdini's **Command Line Tools** shell (Start menu, under
   your Houdini install; it runs `hcmd.exe`) and run:
   ```
   hcustom --output_compiler_range
   ```
   It prints the oldest and newest compiler versions this Houdini
   accepts, as numbers like `19.38`.
2. When you configure (below), CMake prints the compiler it found,
   e.g. `The CXX compiler identification is MSVC 19.44.xxxxx`.
3. If your compiler is outside the range, install a matching one
   alongside your current Visual Studio: open the **Visual Studio
   Installer**, choose **Modify**, go to **Individual components**,
   search for "MSVC", and tick a "MSVC v143 - VS 2022 C++ x64/x86
   build tools" entry whose version fits. Compiler 19.**NN** comes
   from build tools 14.**NN**. Then add `-T v143,version=14.NN` to
   the configure command below, and check that CMake now reports the
   older compiler.

#### Build

Use Houdini's **Command Line Tools** shell, which sets the `HFS`
variable CMake needs. (It doesn't need to run as administrator, as
long as the source is in a folder you own.) From inside each SOP's
folder:
```
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```
**Don't leave out `--config Release`.** Without it, Visual Studio
makes a Debug build, which Houdini won't load. (A sign of this is
`zd.dll` and `minizipd.dll`, with a "d" at the end, appearing in your
`dso` folder.)

This installs the built `.dll` (plus its `minizip.dll`/`z.dll`
runtime dependencies) into your `houdiniX.Y\dso\` folder
automatically.

If you change anything about the compiler or the Houdini version,
delete the `build` folder and configure again: CMake remembers the
compiler it chose the first time.

#### Add the `dso` folder to `PATH`

**This step is required.** Without it, the plugin builds fine but
fails to load in Houdini, with no visible error message in the
normal GUI. Windows doesn't reliably find a plugin DLL's own
dependencies (here, `minizip.dll` and `z.dll`) in the folder the
plugin is loaded from.

First find your Houdini user preferences folder. It is usually
`C:\Users\<you>\Documents\houdiniX.Y`, but if your Documents folder
is synced by OneDrive it may be somewhere like
`C:\Users\<you>\OneDrive - <Company>\Documents\houdiniX.Y`. To be
sure, open Houdini's **Python Shell** (Windows menu) and run:
```
hou.homeHoudiniDirectory()
```
In that folder, open (or create) `houdini.env` in a text editor and
add this line, using that folder's path with forward slashes and
keeping the quotes:
```
PATH = "<your-houdini-user-pref-folder>/dso;$PATH"
```
e.g. `PATH = "C:/Users/<you>/Documents/houdini22.0/dso;$PATH"`.
Houdini reads `houdini.env` at every startup. To check that it
took, run `import os; print(os.environ["PATH"])` in the Python
Shell and look for your `dso` folder.

#### Troubleshooting

If a node doesn't appear in the Tab menu after building, the plugin
failed to load. To see why, set the environment variable
`HOUDINI_DSO_ERROR=2` and launch `hbatch.exe` (not `houdinifx.exe`,
which has no console to print to) from a terminal. Then:

- **"Couldn't load ... z.dll" or "... minizip.dll", "Missing version
  information."** Harmless. Houdini tries every DLL in `dso` as a
  plugin, and these two are libraries, not plugins. You'll only see
  these messages with `HOUDINI_DSO_ERROR` set.
- **"Couldn't load ... SOP_Read3mf.dll", "Incompatible compiler
  versions."** Your compiler is outside this Houdini's range. See
  "Check your compiler version" above.
- **"Couldn't load ... SOP_Read3mf.dll", "The specified module could
  not be found."** The plugin was found, but a DLL it needs wasn't.
  Check the `PATH` step above, and that you built with `--config
  Release`. To list exactly what the plugin needs, run this from a
  Visual Studio "Developer Command Prompt":
  ```
  dumpbin /dependents <path-to-your-dso-folder>\SOP_Read3mf.dll
  ```
  Any name ending in `D.dll` (like `MSVCP140D.dll`) means a Debug
  build.

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
