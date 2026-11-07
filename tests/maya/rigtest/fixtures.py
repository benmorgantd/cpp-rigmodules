"""Reading fixture files: skeletons, templates and data-driven case files.

Every fixture file is checked for unknown keys, the same rule rig templates follow (D17),
so a typo in a case file fails loudly instead of silently testing nothing.
"""
import copy
import json
import os

from rigtest import config


class FixtureError(Exception):
    """A fixture or case file is malformed."""


def read_json(path):
    try:
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)
    except ValueError as error:
        raise FixtureError("%s is not valid JSON: %s" % (path, error))


def check_keys(data, where, required, optional=()):
    if not isinstance(data, dict):
        raise FixtureError("%s must be a JSON object" % where)
    missing = [k for k in required if k not in data]
    unknown = [k for k in data if k not in required and k not in optional]
    problems = []
    if missing:
        problems.append("missing key(s) %s" % ", ".join(missing))
    if unknown:
        problems.append("unknown key(s) %s" % ", ".join(unknown))
    if problems:
        raise FixtureError("%s: %s" % (where, "; ".join(problems)))


# ---- skeletons -------------------------------------------------------------------------------

def load_skeleton(name):
    """fixtures/skeletons/<name>.json -> list of joint dicts with defaults filled in."""
    path = os.path.join(config.FIXTURES_DIR, "skeletons", name + ".json")
    data = read_json(path)
    check_keys(data, path, ["joints"])
    joints = []
    for index, joint in enumerate(data["joints"]):
        where = "%s joints[%d]" % (path, index)
        check_keys(joint, where, ["name", "side", "usage", "translate"], ["rotate", "parent", "export", "skin"])
        filled = {"rotate": [0, 0, 0], "parent": None, "export": True, "skin": True}
        filled.update(joint)
        joints.append(filled)
    return joints


# ---- templates -------------------------------------------------------------------------------

def template_path(name):
    """'RigTemplates/X.json' resolves against the repo. Anything else is fixtures/templates/<name>.json."""
    if name.startswith("RigTemplates/"):
        return os.path.join(config.REPO_DIR, name)
    return os.path.join(config.FIXTURES_DIR, "templates", name + ".json")


def load_template(name):
    return read_json(template_path(name))


def write_template(data, label="template"):
    """Writes a template dict to the work folder and returns a forward-slash path."""
    path = os.path.join(config.WORK_DIR, label + ".json")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2)
    return path.replace("\\", "/")


# ---- JSON pointer edits (validation cases) ---------------------------------------------------

def _pointer_parts(pointer):
    if not pointer.startswith("/"):
        raise FixtureError("JSON pointer must start with '/': %r" % pointer)
    return [p.replace("~1", "/").replace("~0", "~") for p in pointer[1:].split("/")]


def _walk(data, parts, pointer):
    for part in parts:
        if isinstance(data, list):
            data = data[int(part)]
        elif isinstance(data, dict) and part in data:
            data = data[part]
        else:
            raise FixtureError("JSON pointer %s does not exist in the base template" % pointer)
    return data


def apply_edits(template, edits):
    """Returns a copy of template with each edit applied in order.

    {"set": "/a/0/b", "value": v} sets or adds a key, or replaces a list item.
    {"remove": "/a/b"} deletes a key or list item.
    """
    result = copy.deepcopy(template)
    for index, edit in enumerate(edits):
        if "set" in edit:
            check_keys(edit, "edits[%d]" % index, ["set", "value"])
            parts = _pointer_parts(edit["set"])
            parent = _walk(result, parts[:-1], edit["set"])
            if isinstance(parent, list):
                parent[int(parts[-1])] = copy.deepcopy(edit["value"])
            else:
                parent[parts[-1]] = copy.deepcopy(edit["value"])
        elif "remove" in edit:
            check_keys(edit, "edits[%d]" % index, ["remove"])
            parts = _pointer_parts(edit["remove"])
            parent = _walk(result, parts[:-1], edit["remove"])
            _walk(parent, parts[-1:], edit["remove"])
            del parent[int(parts[-1]) if isinstance(parent, list) else parts[-1]]
        else:
            raise FixtureError("edits[%d] needs 'set' or 'remove'" % index)
    return result
