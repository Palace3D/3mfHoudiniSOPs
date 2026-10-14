"""Checks that the Palace3D 3MF package loaded. Run by test_package.bat under hython.

Exits with 0 if both SOPs are available and come from the package, 1 otherwise.
"""
import os
import sys

import hou

PLUGINS = ("hdk_read3mf", "hdk_save3mf")


def norm(path):
    return os.path.normcase(os.path.normpath(path))


ok = True
print()
print("Houdini " + hou.applicationVersionString())
print("Preferences folder: " + hou.homeHoudiniDirectory())

pkg = hou.getenv("PALACE3D_3MF") or ""
build = hou.getenv("PALACE3D_3MF_BUILD") or ""
print("PALACE3D_3MF       = " + (pkg or "(not set)"))
print("PALACE3D_3MF_BUILD = " + (build or "(not set)"))
if not pkg or not build:
    print("FAIL: the package didn't set its variables, so Houdini didn't load it.")
    print("      Check the HOUDINI_PACKAGE_VERBOSE messages above.")
    ok = False

dso = os.path.join(build, "dso") if build else ""
category = hou.sopNodeTypeCategory()
for name in PLUGINS:
    dll = os.path.join(dso, "SOP_" + name[4:].capitalize() + ".dll") if dso else ""
    if dll and not os.path.isfile(dll):
        print("FAIL: {} isn't in the package.".format(dll))
        ok = False
    node_type = hou.nodeType(category, name)
    if node_type is None:
        print("FAIL: SOP {} isn't available; its plugin didn't load.".format(name))
        ok = False
        continue
    source = node_type.sourcePath() or ""
    print("OK:   {} [{}], from {}".format(name, node_type.description(), source or "?"))
    # For a compiled operator, sourcePath() is normally its DLL. Flag one that
    # loaded from somewhere other than the package, e.g. a source build.
    if dso and os.path.isfile(source) and norm(os.path.dirname(source)) != norm(dso):
        print("FAIL: {} loaded from outside the package.".format(name))
        ok = False

    help_file = os.path.join(pkg, "help", "nodes", "sop", name + ".txt") if pkg else ""
    if help_file and not os.path.isfile(help_file):
        print("FAIL: help page {} is missing.".format(help_file))
        ok = False

print()
print("Package check " + ("PASSED" if ok else "FAILED"))
sys.exit(0 if ok else 1)
