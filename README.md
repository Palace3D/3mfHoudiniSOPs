# Houdini 3mf importer and exporter

SOP nodes in C++ for importing and exporting 3mf files. These
supersede earlier Python versions.

Builds and runs on both Linux and Windows. A prebuilt package is
available for Windows, for Houdini 20.5 and 22.0.

## Installation

There are two ways to install these SOPs:

- **Prebuilt package (Windows, Houdini 20.5 and 22.0).** Download,
  unzip, restart Houdini. Nothing to build and no settings to edit.
- **Build from source**, for Linux, other Houdini versions, or
  development. See [Building from source](#building-from-source).

**Use one or the other, not both.** If the package is installed and
you also have source-built `SOP_Read3mf` / `SOP_Save3mf` plugins in
your own `houdiniX.Y/dso` folder, Houdini finds two copies of each
node and which one you get isn't reliable. Before installing the
package, delete any source-built copies (see
[Switching from a source build](#switching-from-a-source-build)).

Throughout, `houdiniX.Y` means the folder for **your** Houdini
version, e.g. `houdini22.0` or `houdini20.5`. Copying an example path
with the wrong version in it is an easy mistake to make.

### Installing the package (Windows)

1. From this repository's **Releases** page on GitHub, download
   `palace3d_3mf-<version>-windows.zip`. The one zip works with both
   Houdini 20.5 and 22.0.
2. Find your Houdini user preferences folder. It is usually
   `C:\Users\<you>\Documents\houdiniX.Y`, but if your Documents
   folder is synced by OneDrive it may be somewhere like
   `C:\Users\<you>\OneDrive - <Company>\Documents\houdiniX.Y`. To be
   sure, open Houdini's **Python Shell** (Windows menu) and run:
   ```
   hou.homeHoudiniDirectory()
   ```
3. In that folder, create a folder named `packages` if there isn't
   one already.
4. Unzip the download into `packages`, so that it looks like this:
   ```
   houdiniX.Y\packages\palace3d_3mf.json
   houdiniX.Y\packages\palace3d_3mf\...
   ```
   The `.json` file must be directly inside `packages`, not in a
   subfolder: Houdini doesn't look in subfolders for it. (Windows'
   "Extract All" offers a folder named after the zip; remove that
   part of the path before extracting.)
5. Restart Houdini. Both nodes are in the SOP Tab menu (type
   "3mf"), and right-click > **Help** on either node shows its help
   page.

If you use both Houdini 20.5 and 22.0, unzip into each version's
`packages` folder.

**Updating:** delete `palace3d_3mf.json` and the `palace3d_3mf`
folder from `packages`, unzip the new version there, and restart
Houdini. **Uninstalling:** delete the same two.

**If the nodes don't appear,** set the environment variable
`HOUDINI_PACKAGE_VERBOSE=1` and start `hbatch.exe` from a Command
Prompt (it's in Houdini's `bin` folder): Houdini then prints which
package files it found and what it did with them. A package for the
wrong Houdini version is skipped silently: it only loads in 20.5.x
and 22.0.x.

### Switching from a source build

If you built these SOPs yourself before, remove that build before
installing the package. In your `houdiniX.Y\dso` folder, delete
`SOP_Read3mf.dll` and `SOP_Save3mf.dll`, and `minizip.dll` and
`z.dll` if they're there. Help pages you copied into
`houdiniX.Y\help\nodes\sop\` (`hdk_read3mf.txt`, `hdk_save3mf.txt`)
can go too, since the package has its own. You can also remove the
`PATH = ".../dso;$PATH"` line older instructions had you add to
`houdini.env`.

### Building from source

The SOPs are built against your own Houdini install, using the HDK
(Houdini Development Kit) that comes with every copy of Houdini, in
`$HFS/toolkit/`. They have been built and tested with **Houdini
22.0** (and, for the prebuilt package, 20.5). A plugin built for one
Houdini version (e.g. 22.0) will not load in another (e.g. 20.5):
you have to build it against the version you'll run it in, and older
versions may need small code changes.

A source build puts the plugins in your own `houdiniX.Y/dso` folder.
Don't have the package installed at the same time (see above).

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

#### Building on Fedora / Linux

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
This puts the built `.so` into `$HOME/houdiniX.Y/dso/`
automatically via `houdini_configure_target()`.

#### Building on Windows

##### Prerequisites

- **Visual Studio**, with the **Desktop development with C++**
  workload. The free Community edition is fine. Note that Visual
  Studio is a different product from Visual Studio Code; VS Code
  does not include the C++ compiler you need.
- **CMake**, the standalone install from cmake.org, added to `PATH`
  (the installer offers this).
- **Git**, which vcpkg needs.
- **[vcpkg](https://github.com/microsoft/vcpkg)**, with the static
  build of minizip installed. Install Visual Studio first; vcpkg
  uses its compiler.
  ```
  git clone https://github.com/microsoft/vcpkg
  cd vcpkg
  .\bootstrap-vcpkg.bat
  .\vcpkg install minizip:x64-windows-static-md
  ```
  The `x64-windows-static-md` part matters: it builds minizip and
  zlib as static libraries, which are linked into the plugins
  themselves, so there are no extra DLLs to find at load time. The
  CMake files ask vcpkg for this version automatically.

##### Check your compiler version

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

##### Build

Use Houdini's **Command Line Tools** shell, which sets the `HFS`
variable CMake needs. (It doesn't need to run as administrator, as
long as the source is in a folder you own.) From inside each SOP's
folder:
```
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```
**Don't leave out `--config Release`.** Without it, Visual Studio
makes a Debug build, which Houdini won't load.

This puts the built `.dll` into your `houdiniX.Y\dso\` folder
automatically. Nothing else needs to go with it, and no `PATH`
change is needed.

If you change anything about the compiler, the Houdini version, or
the vcpkg triplet, delete the `build` folder and configure again:
CMake remembers what it chose the first time.

##### Troubleshooting

If a node doesn't appear in the Tab menu after building, the plugin
failed to load. To see why, set the environment variable
`HOUDINI_DSO_ERROR=2` and launch `hbatch.exe` (not `houdinifx.exe`,
which has no console to print to) from a terminal. Then:

- **"Couldn't load ... SOP_Read3mf.dll", "Incompatible compiler
  versions."** Your compiler is outside this Houdini's range. See
  "Check your compiler version" above.
- **"Couldn't load ... SOP_Read3mf.dll", "The specified module could
  not be found."** The plugin was found, but a DLL it needs wasn't.
  Usually this is a Debug build: check that you built with `--config
  Release`. To list exactly what the plugin needs, run this from a
  Visual Studio "Developer Command Prompt":
  ```
  dumpbin /dependents <path-to-your-dso-folder>\SOP_Read3mf.dll
  ```
  Any name ending in `D.dll` (like `MSVCP140D.dll`) means a Debug
  build. `minizip.dll` or `z.dll` in the list means minizip was
  linked as a DLL: make sure vcpkg has `minizip:x64-windows-static-md`
  installed, delete the `build` folder, and configure again.
- **"Couldn't load ... z.dll" or "... minizip.dll", "Missing version
  information."** Left over from an older build, which put these
  next to the plugins. Delete both from your `dso` folder.

### Building the release package (maintainers)

`packaging/` holds what's needed to make the prebuilt Windows
package:

- `palace3d_3mf.json`: the Houdini package file. It enables the
  package only on Windows in Houdini 20.5.x and 22.0.x, and points
  Houdini at the matching build's `dso` folder and at the shared help
  pages, using `$HOUDINI_PACKAGE_PATH` so users edit nothing.
- `build_release.bat`: builds both SOPs in Release for each Houdini
  version listed at its top (into `build-20.5` and `build-22.0`),
  stages the package in `dist\stage`, and zips it into
  `dist\palace3d_3mf-<version>-windows.zip`. Set the Houdini install
  paths, any MSVC toolset needed for 20.5, and the vcpkg folder at
  the top of the script, then run it from a normal Command Prompt
  with the release version: `packaging\build_release.bat 1.0.0`.
  It makes no zip unless every version was built.
- `test_package.bat` (with `check_package.py`): installs the staged
  package into a throwaway preferences folder
  (`C:\houdini-package-test\houdiniX.Y`), checks with hython that
  both SOPs load from the package, then starts Houdini with that
  folder: `packaging\test_package.bat 22.0`. Your own preferences
  folder, its `dso` folder, and its `houdini.env` aren't read, so
  this shows what a new user gets.

Upload the zip to a GitHub Release; `build-*` and `dist` are not
committed.

## Node Help

Each SOP has help documentation, written in SideFX's wiki markup
format, alongside its source: `SOP_Save3mf/hdk_save3mf.txt` and
`SOP_Read3mf/hdk_read3mf.txt`. The prebuilt package includes both,
so this section is only for source builds.

To make Houdini show this as real, working node help (right-click
a node > Help), copy both files into Houdini's shared node-help
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

**Exporter version.** `Save3mf` records the version of what it
writes in each file's `Application` metadata. The current `Save3mf`
writes `Houdini 3MF Export 3.1`. This number changes only when the
contents of the files `Save3mf` writes change, so it is separate from
the release number of the package: a release that only fixes other
things still writes `Houdini 3MF Export 3.1`.

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
first layer is always treated as fully opaque, as the spec says. The
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
(Fedora) or vcpkg (Windows), as described above. On Windows they are
linked statically into the plugins, so the prebuilt package has no
other DLLs:
- `minizip`
- `zlib`

## Status and Limitations

- Multiproperties are supported, with colorgroup, texture, and base
  material layers and their alphas, except for the `multiply` blend
  method. If a multiproperties group's `blendmethods` asks for
  `multiply`, the import currently stops with an error naming the
  group; turn on Only Geometry to import the shape without colors.
- When a multiproperties group's first layer is a base material, the
  3mf spec recommends blending the layers above it on their own and
  then laying the result over the material. Read3mf currently blends
  all the layers in order instead. That gives the same colors wherever
  the second layer is fully opaque, but can differ where it is partly
  transparent. Read3mf checks the second layer's alphas and puts a
  warning on the node, naming the group, only when that layer can be
  partly transparent.
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
