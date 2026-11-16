"""Reading rig template files without building them. Template validation stays in the C++ read phase."""

import json


def get_rig_name(path: str) -> str:
    """The template's rigName: the name of the rig every build of it makes, and its folder in the asset (D54).
    The default namespace is "{asset name}_{rigName}". Raises ValueError when the
    file isn't JSON or has no rigName."""
    try:
        with open(path, encoding="utf-8") as handle:
            data = json.load(handle)
    except ValueError as error:
        raise ValueError(f"{path} is not a JSON template: {error}") from None
    rig_name = data.get("rigName") if isinstance(data, dict) else None
    if not isinstance(rig_name, str) or not rig_name:
        raise ValueError(f"{path} has no rigName.")
    return rig_name
