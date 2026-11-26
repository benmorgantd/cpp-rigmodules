# Command results: structured JSON instead of strings and exceptions

**Status:** Proposal, not built, not signed off. No decision id yet: it gets one (and an entry in `decisions.md`) when the author signs off.

## Problem

Commands report results in three inconsistent ways: a list of node names, a human string, or an exception whose message is `displayError` text. A caller that is a script, a test or an AI tool has to scrape text to learn what happened, what was created, and why something failed. Some commands already return JSON (`getRigInfo`, `getRigControlInfo`, `getRigControlChannels`, `getRigKeys`), and the template read phase already collects a `ValidationReport` (D17). This proposal gives all of them one shape.

## The envelope

```json
{
  "version": 1,
  "ok": false,
  "data": {},
  "changed": [{"role": "rigRoot", "node": "|char:rig_root", "action": "created"}],
  "warnings": [{"code": "MIRROR_POSITION", "message": "...", "path": "modules[3].joints[1]"}],
  "errors": [{
    "code": "UNKNOWN_SOCKET",
    "message": "Module 'arm' has no socket 'wrist'",
    "path": "parentStructure.hand.start",
    "got": "wrist",
    "expected": ["start", "end"],
    "hint": "Did you mean 'end'?"
  }]
}
```

- `ok: true` carries the payload in `data`. Create and edit commands list what they touched in `changed`, by role, with control markup (side, usage) as well as the node name. Names are unstable, markup is the public key (D25).
- `ok: false` carries one or more `errors`: a stable `code`, a machine-readable `path` into the template or flags, the offending value (`got`), the valid alternatives (`expected`) and an optional `hint`. All errors from one pass are returned together, so a caller fixes everything in one edit.
- `version` is the envelope's own format version. Keys and error codes are public API, with the same discipline as control names. A change goes through an update table (D33) and a decision entry.

## Two kinds of failure

`MPxCommand::doIt` returning `kFailure` discards `setResult`, and `cmds` raises a `RuntimeError` whose text is the `displayError` message. JSON in that text is fragile (it can be prefixed, truncated or reformatted), so the design does not depend on it.

| Kind | C++ returns | Caller sees |
|---|---|---|
| Domain failure: bad template, unknown socket, missing joint, validation errors | `kSuccess` plus `{"ok": false, "errors": [...]}` through `setResult` | `cmds` returns normally with the envelope |
| Misuse: bad flags, wrong object count, internal bug | `kFailure` plus `displayError` | Maya's own `RuntimeError` or `TypeError` |

Consequences:

- Structured errors never travel through an exception. The Python layer raises `RigError` itself, after parsing, so the exception is under our control.
- **Nothing is written when `ok` is false.** `createRigFromTemplate` already has a read phase before its write phase. `isUndoable()` returns false then, so a failed call leaves no empty undo item.
- `displayError` still prints the human summary, so Script Editor users see the failure.
- **Cost:** a bare `cmds.createRigFromTemplate(...)` that returns `ok: false` does not raise, and a caller can ignore it. The Python API is the documented entry point for that reason.
- The Python wrapper catches the `RuntimeError` from the misuse case and re-raises it as `RigError(code="COMMAND_FAILED", message=...)`, so callers deal with one exception type.

## Where it is built

- **C++, in `core/` (no Maya headers):** a plain `CommandResult` struct (data, changed, warnings, errors) and `toJson()` with nlohmann. One home for the schema, testable without Maya. `ValidationReport` maps onto `errors`. Commands call it and pass the string to `setResult`.
- **Python, in `python/rigmodules`:** parse only. A small `RigResult` and `RigError` built from `json.loads`, with no schema logic. A suite test feeds a sample payload from every command through the Python parser, so the two sides cannot drift apart. This fits the D24 placement rule (`.claude/rules/mayadev-boundary.md`): Python goes in `rigmodules` only if it must change when the plugin does.
- **MCP server:** a thin wrapper over the Python layer. `ok: false` becomes `isError: true` with `errors` passed through unchanged, and each tool declares an `outputSchema`.

Rejected: a `json=True` flag on every command (a flag on every syntax and two return shapes per command), changing every command's return type at once (breaks callers, and D14 keeps commands thin), and building the JSON in Python (the C++ command would have no typed result, and the schema would live only in Python).

## What it gives an AI client

1. Self-correction: `expected` and `hint` let the model retry with a valid socket, side or usage instead of parsing prose.
2. Typed outputs: an `outputSchema` per tool, so results validate and need no guessing.
3. Complete validation in one pass, with paths into the template.
4. Stable identity: markup beside node names for follow-up calls (`loadRigPose`, `setRigKeys`).
5. Partial success: batch commands report which channels or controls were skipped and why (locked, missing).
6. Verification from `changed`, without re-querying the scene.
7. Dry run: a `validateRigTemplate` command over the existing read phase.

## Costs and risks

- Public API surface: envelope keys, error codes, `version`.
- Payload size: large results (keys, weights) need a cap and pagination, or they fill a model's context.
- Docs and tests: `docs/commands.md` and `docs/python.md` change, and a test checks every command's result against the schema.

## Suggested order

1. Envelope and error-code schema in `core/`, with a decision entry.
2. Serialize `ValidationReport` into it and add `validateRigTemplate`.
3. The Python `RigResult` and `RigError` layer.
4. Migrate the mutating commands one at a time.
5. The MCP server last, with no logic of its own.

## Open questions

- Which commands migrate first, and whether the existing JSON-returning ones keep their bare payload for one version or move straight to the envelope.
- The initial error-code list, and whether codes are namespaced by area (`TEMPLATE_*`, `POSE_*`).
- Page size and cap for large `data` payloads.
