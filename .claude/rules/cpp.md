---
paths:
  - "cpp/**"
  - "tools/generate_control_shapes.py"
---

# C++ rules: reuse, formatting, naming, Maya API

Loaded when working under `cpp/`. Moved out of CLAUDE.md so it is read only when C++ is touched. The review checklist in CLAUDE.md still applies.

## Reuse and complexity control

Complexity grows through duplication and one-off special cases. Resist both.

### Look before you write

1. Before writing any helper, search `maya/shared/Utilities.h`, `maya/shared/RigModule.h` and `core/` (and their implementations) for existing functionality.
2. If something close exists, **reuse it**. If it nearly fits, **make a small, backward-compatible edit** (new overload, optional parameter, clearer name) instead of writing a parallel version.
3. Only write a new shared function when nothing close exists. Put it in the right shared location, not inside a module.
4. When you reuse or edit a shared function, check its other callers so you do not change their behavior.
5. In your change summary, state what existing code you reused or extended and why a new function was needed, if one was added.
6. **Shareable helpers are shared when written, not after their third copy** (author, 2026-11-09). A helper that isn't about one feature's own concepts goes straight into the shared layer, even with one caller: finding nodes, joints or meshes, namespaces, DAG and history walks, components, file IO, flag parsing, report display. It goes in `maya/shared/Utilities.h` under the class for its topic (`DagUtils`, `MeshUtils`, `CommandUtils`, ...), or in the shared file that owns the topic (`JointMarkupScene` for joints by markup, `RigTraversal` for walking a rig), or `core/` if it needs no Maya. Only logic about the feature itself (a skinCluster's undo state, a module's math) stays file-local. Before finishing, list every anonymous-namespace helper the change added and say why each one isn't shareable.

### Structural practices

- **Rule of three, for feature logic only.** Logic about one feature may be duplicated twice before it is extracted. Scene and utility helpers never are: see step 6 above. Do not invent shared APIs nobody calls.
- **One home per concept.** Each piece of logic, constant, and string lives in exactly one place. Shared code goes in the shared layer. Module-specific code stays in the module.
- **Modules stay thin.** A module declares its attributes, child plugs, and sockets, then delegates to shared code for common behavior. If two modules share behavior, move it up to `maya/shared/RigModule.h` or `maya/shared/Utilities.h` (or `core/` when it needs no Maya) instead of copying.
- **Prefer composition and data over inheritance and branching.** Avoid deep class hierarchies and long `if (moduleType == ...)` chains. Extend through shared helpers and the `ModuleRegistry`.
- **Consistent patterns.** A new module should look like the existing ones (FkChain, Layout). If you need to break the pattern, say why. If the pattern itself is wrong, propose changing it everywhere rather than diverging once.
- **Small, single-purpose functions** with clear inputs and outputs. Avoid functions that need many flags to behave differently.
- **Minimize surface area.** Keep things private or file-local by default. Every new public function, attribute, or JSON key is a long-term commitment.
- **No dead or speculative code.** No unused parameters, "just in case" options, or commented-out blocks. Delete what is not used.
- **Avoid new dependencies** unless they clearly pay for themselves. Ask first.
- **Document why, not what.** Comment non-obvious decisions, invariants, and Maya quirks. Do not narrate obvious code.
- **Refactor with the change, not after.** If adding a feature exposes duplication or awkward structure, fix the structure in the same effort or flag it explicitly. Do not leave it for "later."

### Complexity checks

When a change adds notable complexity, call it out and justify it: a new shared function, a new attribute on an existing module, a new JSON key, or a new special case. If a simpler path exists, take it or present it as an alternative.

## Code conventions

### Formatting
Every curly bracket goes on its own new line (Allman), with no exceptions. This includes functions, control flow, namespaces, classes, lambdas, empty bodies (`X::X()` then `{` and `}` on their own lines), and multi-line initializer blocks. A braced init list that fits on one line is a value, not a block, and stays inline: `int v{ 1 }`, `foo(a, {}, b)`, `{ attr, 0.0 }`. `test_source_layout.py` checks this.

Indentation is tabs. `cpp/.clang-format` encodes these rules (Allman, tabs, a parameter list that doesn't fit on one line goes one parameter per line, sorted includes).

**Includes** are grouped in blank-line-separated blocks: the file's own header first, then project headers, the standard library and Maya, in whatever groups the file already uses. Each block is sorted alphabetically, ignoring case. `test_source_layout.py` checks the order. Select the include lines and run Format Selection to sort them. Do not run it over whole files yet: it would also re-align hand-formatted code. Use it on new code or a selection.

```cpp
if (status != MS::kSuccess)
{
    return status;
}
```

### Naming

| Kind | Convention | Example |
|---|---|---|
| `MObject` | `o` prefix | `oMyName` |
| `MPlug` | `p` prefix | `pMyPlug` |
| `MFn*` function sets | `fn` prefix | `fnMyMfn` |
| Module attributes | `a` prefix | `aMyAttributeName` |
| Module files and classes | `<Name>Module.h/.cpp`, class `<Name>ModuleNode` (D53). The template `moduleType` stays `<Name>`. | `AimModule.cpp`, `AimModuleNode`, `"Aim"` |

### Maya API rules

- **Pass lvalue MObjects to non-const `MObject&` parameters.** `MPlug::child(MObject& attr, MStatus*)` does not accept a temporary, so `pElement.child(fnModule.attribute("name"))` fails with E0304. Look the attribute up on its own line into a named `oAttrName` variable, then pass that variable. This applies to any Maya API function taking a non-const `MObject&`, including the result of `fnNode.attribute(...)` and `MFnAttribute::create(...)`. Check the header signature before nesting a call as an argument.
- Always capture and check `MStatus`. Use the project's existing error-handling macros or patterns rather than inventing new ones.
- In `initialize()`, every attribute needs create, add, and `attributeAffects` handled together. Missing `attributeAffects` is a common silent bug, so check it on every new attribute.
- In `compute()`, group plug reads and writes by purpose and avoid re-querying the same plug.
- Keep `compute()` free of allocations and string lookups. Resolve plugs and attributes outside the hot path where possible.

## Structure: Maya-free core plus thin plugin layer

Studio-style split, matching how the author structures their Python code. The folders exist (D13); the rest of the split is still planned.

- **Core (`core/`, no Maya headers):** JSON parsing and validation, joint markup, shared constants. Still to move here: child plug and socket resolution, `parentStructure` ordering, matrix math. Meant to be testable with GoogleTest and no Maya install (no harness yet).
- **Plugin layer (`maya/`, Maya API):** nodes, attribute definitions, plug I/O, and the commands that bridge to the core.
- **Python (started, D30):** a thin package in `python/rigmodules`. Built: the rig query (`get_rig_info`, `get_rigs`), `template.get_rig_name` and `skinweights`. Planned: a build wrapper, the rest of the rig query API (modules, controls, sockets, rest values), template validation passed through to the core, shape IO and pose data read and write, and user-facing error messages. Placement rule: Python goes here only if it would have to change when a RigModules node, attribute, flag or template key changes. It imports no Qt or `bmaya` and reads no environment variables or asset paths (D24).

