"""Finds a Maya install. Plain Python: nothing here imports Maya."""
import os

DEFAULT_VERSION = "2026"
LOCATION_VARIABLE = "MAYA_LOCATION"


def find_maya_dir(maya_dir=None, version=DEFAULT_VERSION):
    """The Maya install folder: the argument, else MAYA_LOCATION, else the standard Windows folder.
    Raises FileNotFoundError, naming what was tried, when the folder has no bin."""
    candidates = [maya_dir, os.environ.get(LOCATION_VARIABLE),
                  os.path.join(os.environ.get("ProgramFiles", r"C:\Program Files"), "Autodesk", "Maya" + version)]
    tried = [c for c in candidates if c]
    for folder in tried:
        if os.path.isdir(os.path.join(folder, "bin")):
            return folder
    raise FileNotFoundError("No Maya install found. Tried: %s. Set %s to the Maya install folder."
                            % (", ".join(tried), LOCATION_VARIABLE))


def mayapy_exe(maya_dir=None, version=DEFAULT_VERSION):
    return _exe("mayapy.exe", maya_dir, version)


def maya_exe(maya_dir=None, version=DEFAULT_VERSION):
    return _exe("maya.exe", maya_dir, version)


def _exe(name, maya_dir, version):
    path = os.path.join(find_maya_dir(maya_dir, version), "bin", name)
    if not os.path.isfile(path):
        raise FileNotFoundError("%s not found at %s" % (name, path))
    return path
