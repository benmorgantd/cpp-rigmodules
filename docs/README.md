# Documentation

How the RigModules documentation is organized and kept current (D21 in `design/decisions.md`).

## Two homes, two audiences

| Folder | Reader | Answers | Tense |
|---|---|---|---|
| `README.md`, `docs/` | People using the system: template authors, riggers, tool writers | What does it do, and how do I use it? | What is true now |
| `design/` | People changing the system | Why is it built this way, and what was rejected? | History, with dates |

User docs never explain history ("this used to..."), and the decision log is never the only place a user-facing rule is written down. When a decision changes behaviour, the decision entry says why and the owning page says what.

## Pages and what each owns

Each topic has exactly one owning page. Other pages link to it instead of repeating it. The README's summaries (modules table, quick start) are the one allowed exception, and they must stay consistent with the owning page.

| Page | Owns |
|---|---|
| [`../README.md`](../README.md) | What the project is, requirements, build, quick start, repo layout, links |
| [`templates.md`](templates.md) | Template file format: every key, required or optional, defaults, allowed values, `parentStructure`, constraints, mirroring, validation rules |
| [`commands.md`](commands.md) | Every Maya command: syntax, flags, objects, behaviour, undo, errors |
| [`modules.md`](modules.md) | Every module: `moduleType`, `moduleArgs`, joints, controls, child plugs, sockets, behaviour |
| [`controls.md`](controls.md) | What every control carries: markup, shapes and the axis convention, the shape file format, mirroring shapes |
| [`skinning.md`](skinning.md) | Skin weight files: what they match on (joint markup, `meshId`, topology), how loading works, the file format |
| [`python.md`](python.md) | The `rigmodules` Python package: installing it with `RigModules.mod`, and every function |
| [`../tests/README.md`](../tests/README.md) | Running tests, case-file formats, where new tests go |
| [`../design/decisions.md`](../design/decisions.md) | Design decisions and their reasons |

A new topic that fits no page (a Python API, export, pose libraries) gets a new page here and a row in this table.

## When to update

A change updates its owning page **in the same change** when it touches anything a user can see:

- A command, flag, or flag's behaviour → `commands.md`
- A template key, default, allowed value, or validation rule → `templates.md`
- A module, its `moduleArgs`, child plugs, sockets, controls or behaviour → `modules.md` and the README modules table
- Control markup, a shape, or the shape file format → `controls.md`
- The weight file format, `meshId`, or how weights load → `skinning.md`
- A function in the `rigmodules` Python package, or `RigModules.mod` → `python.md`
- Build steps, requirements, or the repo layout → `README.md`
- Running or writing tests → `tests/README.md`

**New things get docs, not just changed things.** A new module, command, template key or workflow is not done until it is documented. Add it to the page that owns its topic. If no page fits, add a page here, a row in the table above, and a link in the README's documentation list.

**Search for stale mentions.** After a rename or behaviour change, search every page for the old name or behaviour, including examples, and fix each one.

A change to a golden snapshot (a public name changed) always has a docs change, because those names appear in `modules.md`.

Internal changes (refactors, compute changes with the same results, test-only changes) need no user doc change.

## Writing rules

- **Describe the code, not the plan.** Document what is built. Planned features go in `design/`, or under a clearly marked "Not built yet" note if users need to know.
- **The code wins.** When a page and the code disagree, fix the page, and say so in the change summary.
- **Use the code's names exactly**: child plug, socket, `moduleArgs`, long flag names (`namespace`, never `ns`).
- **Command examples are Python `maya.cmds`, never MEL.** Flags are keyword arguments named by the long flag name, and a multi-use flag takes a list: `cmds.createRigConstraint("hero:arm_ctrl_l", driver=["layout.end", "spine.end"])`. Flag tables name the keyword without a leading `-`.
- **Show, then list.** A short example first, then the full reference table.
- **Plain Markdown in the repo.** No doc generator, so the docs are reviewed in the same diff as the code.

## Tested examples

The README quick start is a test: `tests/maya/concepts/test_docs_examples.py` reads its template straight from `README.md`, builds it on the `readme_quick_start` skeleton fixture and runs the space switch. The shape file example in `controls.md` and the weight file example in `skinning.md` are loaded by the same file. If you change any of them, keep the test in step. A worked example added elsewhere in the docs gets a test in the same file.
