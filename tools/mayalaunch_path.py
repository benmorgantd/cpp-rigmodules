"""Puts the mayalaunch package on sys.path. Import this module, then import mayalaunch.

mayalaunch is a separate repo shared with mayadev (D48). It is found through MAYALAUNCH_ROOT (the folder
that holds the `mayalaunch` package), else a sibling folder named mayalaunch beside any parent of this repo.
This is the one piece of launcher code that can't live in mayalaunch, because it runs before it can be imported.
"""
import os
import sys

ROOT_VARIABLE = "MAYALAUNCH_ROOT"


def find_root():
    candidates = [os.environ.get(ROOT_VARIABLE)]
    folder = os.path.dirname(os.path.abspath(__file__))
    while True:
        candidates.append(os.path.join(folder, "mayalaunch"))
        parent = os.path.dirname(folder)
        if parent == folder:
            break
        folder = parent
    for candidate in candidates:
        if candidate and os.path.isfile(os.path.join(candidate, "mayalaunch", "__init__.py")):
            return candidate
    raise ImportError("mayalaunch not found. Set %s to the folder that holds the mayalaunch package." % ROOT_VARIABLE)


_root = find_root()
if _root not in sys.path:
    sys.path.insert(0, _root)
