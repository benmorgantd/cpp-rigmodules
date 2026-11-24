"""Writes the asset system flowchart page: the asset-flowchart skill (SKILL.md beside this file) says when to run it.

The diagram is hand-placed SVG. Content and coordinates live together, in this order: the records band (project,
asset, rig and look records, build_rig, the Registry), the main rows (one row per file type: file, load/save tool,
scene node), the right side (capture, apply, library, exports, sources, engine), the bottom band (publish, local
versions, source control), then the HTML around it (legend, matching keys, formats table, doc conflicts).

Plain Python 3, no dependencies:
    python3 generate.py --out asset-flow.html --rigmodules-sha 9dc46fc --mayadev-sha 6ae0473
"""
import argparse
import html

# Places where the docs disagree with each other or with the code, found while reading them for the diagram.
# Each item is HTML. The section is left out while the list is empty.
DOC_CONFLICTS = []

svg = []


def esc(s):
    return html.escape(s, quote=True)


def box(x, y, w, h, kind, title, lines=(), badge=None, tag=None):
    """kind: rm | md | ma | fbx | tool | scene. lines: (text, cls) pairs."""
    svg.append(f'<g class="node {kind}">')
    svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{8 if kind in ("tool", "scene") else 3}"/>')
    tx = x + 12
    if badge:
        svg.append(f'<circle class="badge" cx="{x + 16}" cy="{y + 17}" r="9"/>'
                   f'<text class="badge-t" x="{x + 16}" y="{y + 21}" text-anchor="middle">{badge}</text>')
        tx = x + 32
    svg.append(f'<text class="t" x="{tx}" y="{y + 21}">{esc(title)}</text>')
    if tag:
        svg.append(f'<text class="tag" x="{x + w - 10}" y="{y + 20}" text-anchor="end">{esc(tag)}</text>')
    ly = y + 38
    for text, cls in lines:
        svg.append(f'<text class="{cls}" x="{x + 12}" y="{ly}">{esc(text)}</text>')
        ly += 15
    svg.append('</g>')


def path(d, both=False, dashed=False, label=None, lx=0, ly=0, anchor="middle"):
    cls = "edge dash" if dashed else "edge"
    start = ' marker-start="url(#ahs)"' if both else ""
    svg.append(f'<path class="{cls}" d="{d}" marker-end="url(#ah)"{start}/>')
    if label:
        svg.append(f'<text class="elabel" x="{lx}" y="{ly}" text-anchor="{anchor}">{esc(label)}</text>')


def lane(x, w, y, h, label, sub):
    svg.append(f'<rect class="lane" x="{x}" y="{y}" width="{w}" height="{h}" rx="6"/>')
    svg.append(f'<text class="lane-t" x="{x + 10}" y="{y + 20}">{esc(label)}</text>')
    svg.append(f'<text class="lane-s" x="{x + 10}" y="{y + 35}">{esc(sub)}</text>')


W, H = 1520, 1062
svg.append(f'<svg viewBox="0 0 {W} {H}" width="{W}" height="{H}" role="img" '
           'aria-labelledby="dtitle"><title id="dtitle">Flowchart of the asset system: files in the asset folder, the '
           'commands that load and save them, the Maya scene, and the library, clips and engine outputs</title>')
svg.append('<defs>'
           '<marker id="ah" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">'
           '<path class="ahp" d="M0,0 L10,5 L0,10 z"/></marker>'
           '<marker id="ahs" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">'
           '<path class="ahp" d="M0,0 L10,5 L0,10 z"/></marker>'
           '</defs>')

# ---- bands ---------------------------------------------------------------
svg.append('<text class="band-t" x="20" y="22">RECORDS · what an asset holds, and how a build finds it</text>')
lane(20, 1480, 30, 128, "", "")
svg.append('<text class="band-t" x="20" y="184">BUILD AND ROUND TRIP · one row per file type</text>')
lane(20, 270, 192, 634, "ASSET FOLDER", "authored, in source control")
lane(310, 280, 192, 634, "LOAD →  ← SAVE", "①–⑥ is the order build_rig runs")
lane(610, 270, 192, 634, "MAYA SCENE", "one rig per namespace: {name}_{rig}")
lane(900, 280, 192, 634, "CAPTURE · APPLY", "control data and skeleton data")
lane(1200, 300, 192, 634, "LIBRARY · CLIPS · ENGINE", "what leaves the rig")

# ---- records band --------------------------------------------------------
box(40, 52, 110, 92, "md", "project.json", [("assetTypes", "s"), ("fileTypes", "s"), ("rig.regions", "s"), ("format 3", "m")])
box(160, 52, 110, 92, "md", "asset.json", [("assetType only", "s"), ("id = folder path", "s"), ("format 2", "m")])
box(330, 52, 240, 92, "tool", "build_rig", [("Build Rig from Asset… / Asset Editor", "s"),
                                           ("checks the look fits the skeleton,", "s"),
                                           ("builds a parent rig first, runs ①–⑥", "s")], tag="bmaya")
svg.append('<rect class="group" x="622" y="44" width="246" height="108" rx="6"/>')
box(630, 52, 110, 92, "md", "rig.json", [("skeleton ref", "s"), ("source · parent", "s"), ("format 1", "m")])
box(750, 52, 110, 92, "md", "look.json", [("skeletons its", "s"), ("meshes bind to", "s"), ("format 1", "m")])
box(920, 52, 560, 92, "tool", "Registry", [("Scans every asset.json, rig.json, look.json and publish folder in the project.", "s"),
                                          ("In memory only, never written, so nothing shared is checked out or merged.", "s"),
                                          ("find(asset_type=, skeleton=) · latest_publish(id, kind, name) · errors", "m")], tag="bmaya")
path("M622,98 L572,98", label="reads", lx=597, ly=92)
path("M868,98 L918,98", label="scans", lx=893, ly=92)
path("M450,144 L450,198", dashed=True)

# ---- main rows (shifted below the lane headers) ---------------------------
svg.append('<g transform="translate(0,22)">')
svg.append('<text class="band-t" x="1500" y="834" text-anchor="end">PUBLISH · VERSIONS · SOURCE CONTROL · mayadev, around every save</text>')
lane(20, 1480, 842, 186, "", "")
rows = [250, 335, 420, 505, 590, 675, 760]
H_ROW = 64
A, B, C = 40, 330, 630
AW, BW, CW = 230, 240, 230


def top(r):
    return rows[r] - H_ROW // 2


files = [
    ("rm", "skeleton.json", "skeletons/{skeleton}/", "marked joints, rests · skeletonHash", "v1"),
    ("rm", "template.json", "rigs/{rig}/", "modules, parentStructure, rigName", "v1"),
    ("rm", "shapes.json", "rigs/{rig}/", "one shape per control, by markup", "v2"),
    ("ma", "{name}_{look}_model.ma", "looks/{look}/model/", "meshes only, each with a meshId", ""),
    ("rm", "weights/<mesh>.json", "looks/{look}/", "meshId · [side, usage] · topology", "v3"),
    ("md", "textures.json", "looks/{look}/textures/", "material and images per meshId", "v1"),
    ("ma", "{name}_{rig}_rig.ma", "rigs/{rig}/", "the built rig scene", ""),
]
for r, (kind, title, folder, desc, ver) in enumerate(files):
    box(A, top(r), AW, H_ROW, kind, title, [(folder, "m"), (desc, "s")], tag=ver or None)

tools = [
    ("1", "→ cmds.loadRigSkeleton", "← cmds.saveRigSkeleton"),
    ("2", "→ cmds.createRigFromTemplate", "← template handler copies in"),
    ("3", "→ cmds.loadRigControlShapes", "← cmds.saveRigControlShapes"),
    ("4", "→ model handler: import", "← Save Model (meshes only)"),
    ("5", "→ cmds.setRigLook (apply_look)", "← cmds.saveRigSkinWeights"),
    ("6", "→ load_textures", "← save / import_textures"),
    (None, "→ import, never reference", "← Save Rig: Maya ASCII"),
]
badges = {"1": "①", "2": "②", "3": "③", "4": "④", "5": "⑤", "6": "⑥"}
for r, (b, load, save) in enumerate(tools):
    svg.append(f'<g class="node tool"><rect x="{B}" y="{top(r)}" width="{BW}" height="{H_ROW}" rx="8"/>')
    tx = B + 12
    if b:
        svg.append(f'<circle class="badge" cx="{B + 18}" cy="{rows[r]}" r="10"/>'
                   f'<text class="badge-t" x="{B + 18}" y="{rows[r] + 4}" text-anchor="middle">{b}</text>')
        tx = B + 36
    svg.append(f'<text class="m strong" x="{tx}" y="{rows[r] - 6}">{esc(load)}</text>')
    svg.append(f'<text class="m" x="{tx}" y="{rows[r] + 13}">{esc(save)}</text></g>')

scene = [
    ("Joints", "joint markup: side, usage,", "export, skin · namespace"),
    ("Rig root", "assetId · rigName · skeletonHash", "rigVersion · pluginVersion · publishVersion"),
    ("Controls", "controlMarkup (side, usage)", "keyable channels · shape attrs"),
    ("Meshes", "meshId: a GUID on the transform,", "survives rename and import"),
    ("lookRoot + skinClusters", "the look worn; each mesh is", "loaded, folded or rigid"),
    ("Shaders", "standardSurface per material,", "tagged so the next swap clears it"),
    ("Animation scenes", "hold imported rig copies,", "so they carry what they need"),
]
for r, (title, l1, l2) in enumerate(scene):
    box(C, top(r), CW, H_ROW, "scene", title, [(l1, "s"), (l2, "s")])

for r in range(7):
    y = rows[r]
    both = r != 1
    path(f"M{A + AW},{y} L{B - 2},{y}", both=both)
    path(f"M{B + BW},{y} L{C - 2},{y}", both=both)

# ---- right side ----------------------------------------------------------
D, DW, E, EW = 920, 240, 1220, 270
box(D, 212, DW, 76, "tool", "Capture Joint Clip",
    [("capture_joint_clip → JointClip", "m"), ("→ write_fbx  (save_anim_clip)", "m")], tag="bmaya")
box(D, 310, DW, 70, "tool", "Save Skeleton Export",
    [("write_skeleton_fbx at rest", "m"), ("check_skeleton_match for clips", "m")], tag="bmaya")
box(D, 405, DW, 70, "tool", "Animate From Joint Clip",
    [("read_fbx (+ bone map) → JointClip", "m"), ("→ cmds.animateRigFromJointClip", "m")])
box(D, 507, DW, 76, "tool", "Library Browser",
    [("cmds.saveRigPose / loadRigPose", "m"), ("cmds.saveRigAnimation / load…", "m")], tag="bmaya")

# exports folder
svg.append(f'<g class="node fbx"><rect x="{E}" y="200" width="{EW}" height="226" rx="3"/>')
svg.append(f'<text class="t" x="{E + 12}" y="221">exports/</text>')
svg.append(f'<text class="m" x="{E + 12}" y="238">skeletons/{{skeleton}}/exports/</text>')
svg.append(f'<text class="s em" x="{E + 12}" y="258">a clip bundle: &lt;name&gt;/</text>')
bundle = [("fbx", "<name>.fbx", "what the joints do"),
          ("rm", "<name>.controlanim.json", "what the rig did (sidecar)"),
          ("md", "<name>.animproperties.json", "loop, root motion, events"),
          ("rm", "<name>.bonemap.json", "optional: markup for FBX")]
yy = 276
for kind, f, d in bundle:
    svg.append(f'<rect class="dot {kind}" x="{E + 14}" y="{yy - 8}" width="8" height="8" rx="1"/>')
    svg.append(f'<text class="m" x="{E + 28}" y="{yy}">{esc(f)}</text>')
    svg.append(f'<text class="s" x="{E + 28}" y="{yy + 13}">{esc(d)}</text>')
    yy += 30
svg.append(f'<line class="rule" x1="{E + 12}" y1="{yy - 6}" x2="{E + EW - 12}" y2="{yy - 6}"/>')
svg.append(f'<text class="s em" x="{E + 12}" y="{yy + 10}">the skeleton export, beside the bundles:</text>')
svg.append(f'<rect class="dot fbx" x="{E + 14}" y="{yy + 17}" width="8" height="8" rx="1"/>')
svg.append(f'<text class="m" x="{E + 28}" y="{yy + 25}">{{name}}_{{skel}}_skeleton.fbx + .bonemap</text>')
svg.append('</g>')

box(E, 440, EW, 56, "fbx", "sources/", [("mocap, vendor or engine FBX + bone map", "s")], tag="copied in")

svg.append(f'<g class="node rm"><rect x="{E}" y="510" width="{EW}" height="146" rx="3"/>')
svg.append(f'<text class="t" x="{E + 12}" y="531">Pose and animation library</text>')
svg.append(f'<text class="m" x="{E + 12}" y="548">controlpose v3 · controlanim v3</text>')
src = [("approved", "rigs/{rig}/controlposes/"),
       ("shared", "the source rig's folders"),
       ("user", "<root>\\user\\<login>\\…")]
yy = 570
for n, p in src:
    svg.append(f'<text class="s strong" x="{E + 12}" y="{yy}">{n}</text>')
    svg.append(f'<text class="m" x="{E + 72}" y="{yy}">{esc(p)}</text>')
    yy += 18
svg.append(f'<text class="s em" x="{E + 12}" y="{yy + 6}">every save goes to user · masks.json beside each</text>')
svg.append('</g>')

box(E, 676, EW, 70, "eng", "Game engine import", [("clip FBX + animproperties, paired with", "s"),
                                                  ("the skeleton export by joint names", "s")], tag="untested")

# edges C <-> D, D <-> E
path(f"M{C + CW},{rows[0]} L{D - 2},{rows[0]}")                       # joints -> capture
path(f"M{C + CW},272 L890,272 L890,345 L{D - 2},345")                 # joints -> skeleton export
path(f"M{D},425 L{C + CW + 2},425")                                   # animate -> controls
path(f"M{C + CW},446 L890,446 L890,545 L{D - 2},545", both=True)       # controls <-> library browser
path(f"M{D + DW},250 L{E - 2},250", label="writes", lx=1190, ly=244)
path(f"M{D + DW},345 L{E - 2},345", label="writes", lx=1190, ly=339)
path(f"M{E},415 L{D + DW + 2},415", label="reads", lx=1190, ly=409)
path(f"M{E},462 L{D + DW + 2},462", label="reads", lx=1190, ly=456)
path(f"M{D + DW},545 L{E - 2},545", both=True)
path(f"M{E + EW},300 L1508,300 L1508,711 L{E + EW + 2},711")

# ---- bottom band ---------------------------------------------------------
box(40, 870, 330, 140, "md", "publish_rig → rigs/{rig}/publish/<id>/",
    [("<id> = UTC time + user: 20261108T142012Z_bmorgan", "m"),
     ("copies skeleton.json, template.json, rig.json,", "s"),
     ("shapes.json, postops.py · writes publish.json", "s"),
     ("gate: a skeleton change that breaks the last publish", "s"),
     ("asks, then is recorded as breakingChanges", "s")])
box(390, 870, 330, 140, "md", "publish_look → looks/{look}/publish/<id>/",
    [("a folder per publish, never edited", "s"),
     ("copies look.json, the model, weights,", "s"),
     ("textures and shaders · writes publish.json", "s"),
     ("gate: refuses while a mesh is folded, rigid", "s"),
     ("or stale, or the look wasn't applied this session", "s")])
box(740, 870, 360, 140, "tool", "Local versions", [
    ("every handlers.save copies the old file first:", "s"),
    ("%LOCALAPPDATA%\\bmaya\\versions\\…\\shapes.v007.json", "m"),
    ("a folder type copies whole: weights.v003\\", "m"),
    ("never submitted · BMAYA_VERSIONS_DIR overrides", "s"),
    ("Restore Version… in the Asset Editor's Files tab", "s")], tag="bmaya")
box(1120, 870, 360, 140, "tool", "Source control adapter", [
    ("checkout(path) before an overwrite,", "s"),
    ("add(path) after a new file", "s"),
    ("list_folders · sync: looks not in the workspace", "s"),
    ("default does nothing; no Perforce adapter yet", "s"),
    ("ask for +l (exclusive lock) on .ma files", "s")], tag="bmaya")
path(f"M120,{rows[6] + 32} L120,866", label="rig inputs", lx=126, ly=850, anchor="start")
path(f"M230,{rows[6] + 32} L230,818 L555,818 L555,866", label="look inputs", lx=390, ly=812)

svg.append('</g>')
svg.append('</svg>')
SVG = "\n".join(svg)

page = r"""<title>RigModules Asset Flow</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;500&family=IBM+Plex+Sans+Condensed:wght@500;600&family=IBM+Plex+Sans:wght@400;500;600&display=swap">
<style>
/* Layout: a drafting-sheet page. Header, one wide flowchart in its own scroller, then key and reference tables. */
:root {
  --bg: #f3f5f6; --paper: #ffffff; --fg: #1b2328; --muted: #5d6a72; --line: #c9d1d6; --lane: #e8ecee;
  --edge: #48555d; --accent: #1f6f8b;
  --rm: #1f6f8b; --rm-bg: #e3f0f5;
  --md: #a3661a; --md-bg: #f7ecdc;
  --ma: #6b4fa0; --ma-bg: #eee8f7;
  --fbx: #3f7d3a; --fbx-bg: #e5f1e3;
  --eng: #5d6a72; --eng-bg: #eceff1;
  --tool-bg: #ffffff; --scene-bg: #f9fafb;
  --f-display: "IBM Plex Sans Condensed", "Arial Narrow", system-ui, sans-serif;
  --f-body: "IBM Plex Sans", system-ui, -apple-system, "Segoe UI", sans-serif;
  --f-mono: "IBM Plex Mono", ui-monospace, "Cascadia Mono", Consolas, monospace;
}
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) {
  --bg: #12171a; --paper: #182024; --fg: #e3e9ec; --muted: #94a3ab; --line: #34424a; --lane: #1d272c;
  --edge: #9aaab3; --accent: #6cc0dd;
  --rm: #6cc0dd; --rm-bg: #15313b; --md: #e0a656; --md-bg: #3a2b16; --ma: #b39be0; --ma-bg: #2c2440;
  --fbx: #8cc785; --fbx-bg: #1e3420; --eng: #94a3ab; --eng-bg: #222b30; --tool-bg: #182024; --scene-bg: #1a2327;
  color-scheme: dark } }
:root[data-theme="dark"] {
  --bg: #12171a; --paper: #182024; --fg: #e3e9ec; --muted: #94a3ab; --line: #34424a; --lane: #1d272c;
  --edge: #9aaab3; --accent: #6cc0dd;
  --rm: #6cc0dd; --rm-bg: #15313b; --md: #e0a656; --md-bg: #3a2b16; --ma: #b39be0; --ma-bg: #2c2440;
  --fbx: #8cc785; --fbx-bg: #1e3420; --eng: #94a3ab; --eng-bg: #222b30; --tool-bg: #182024; --scene-bg: #1a2327;
  color-scheme: dark }
body { background: var(--bg); color: var(--fg); font-family: var(--f-body); font-size: 15px; line-height: 1.55; }
.wrap { max-width: 1560px; margin: 0 auto; padding-block: 32px 64px;
  padding-left: max(16px, env(safe-area-inset-left, 0px)); padding-right: max(16px, env(safe-area-inset-right, 0px)); }
header { display: grid; gap: 6px; margin-bottom: 20px; }
.eyebrow { font-family: var(--f-mono); font-size: 12px; letter-spacing: .06em; text-transform: uppercase; color: var(--muted); }
h1 { font-family: var(--f-display); font-weight: 600; font-size: clamp(28px, 4vw, 40px); line-height: 1.1; margin: 0; text-wrap: balance; }
h2 { font-family: var(--f-display); font-weight: 600; font-size: 22px; margin: 0 0 10px; text-wrap: balance; }
p { margin: 0; max-width: 72ch; }
.lede { color: var(--muted); }
code { font-family: var(--f-mono); font-size: .88em; }
.legend { display: flex; flex-wrap: wrap; gap: 8px 18px; margin: 16px 0 12px; font-size: 13px; }
.legend span { display: inline-flex; align-items: center; gap: 7px; }
.sw { width: 14px; height: 14px; border-radius: 2px; border: 1.5px solid; display: inline-block; }
.sw.rm { background: var(--rm-bg); border-color: var(--rm); } .sw.md { background: var(--md-bg); border-color: var(--md); }
.sw.ma { background: var(--ma-bg); border-color: var(--ma); } .sw.fbx { background: var(--fbx-bg); border-color: var(--fbx); }
.sw.tool { background: var(--tool-bg); border-color: var(--edge); border-radius: 5px; }
.sw.scene { background: var(--scene-bg); border-color: var(--edge); border-style: dashed; border-radius: 5px; }
.scroller { overflow-x: auto; background: var(--paper); border: 1px solid var(--line); border-radius: 8px; padding: 12px; }
.scroller svg { display: block; max-width: none; }
.hint { font-size: 12px; color: var(--muted); margin-top: 6px; }
section { margin-top: 40px; }
.keys { display: grid; grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); gap: 12px; margin-top: 12px; }
.key { background: var(--paper); border: 1px solid var(--line); border-radius: 6px; padding: 14px 16px; min-width: 0; display: grid; gap: 4px; align-content: start; }
.key b { font-family: var(--f-mono); font-weight: 500; font-size: 14px; color: var(--accent); }
.key p { font-size: 14px; }
.key .who { font-size: 12.5px; color: var(--muted); }
.tbl { overflow-x: auto; border: 1px solid var(--line); border-radius: 6px; background: var(--paper); }
table { border-collapse: collapse; width: 100%; min-width: 900px; font-size: 13.5px; }
th, td { text-align: left; padding: 8px 12px; border-bottom: 1px solid var(--line); vertical-align: top; }
th { font-family: var(--f-mono); font-weight: 500; font-size: 11.5px; letter-spacing: .05em; text-transform: uppercase; color: var(--muted); background: var(--lane); }
tr:last-child td { border-bottom: 0; }
td:first-child { white-space: nowrap; font-family: var(--f-mono); font-size: 12.5px; }
td.v { font-variant-numeric: tabular-nums; font-family: var(--f-mono); font-size: 12.5px; }
.own { display: inline-block; font-family: var(--f-mono); font-size: 11px; padding: 1px 7px; border-radius: 3px; border: 1px solid; white-space: nowrap; }
.own.rm { color: var(--rm); background: var(--rm-bg); } .own.md { color: var(--md); background: var(--md-bg); }
.own.ma { color: var(--ma); background: var(--ma-bg); } .own.fbx { color: var(--fbx); background: var(--fbx-bg); }
ol.flags { margin: 0; padding-left: 22px; display: grid; gap: 10px; max-width: 90ch; }
ol.flags li { padding-left: 4px; }
.src { font-size: 13px; color: var(--muted); margin-top: 28px; }

/* SVG */
svg text { font-family: var(--f-body); fill: var(--fg); }
.band-t { font-family: var(--f-mono); font-size: 11px; letter-spacing: .08em; fill: var(--muted); }
.lane { fill: var(--lane); stroke: none; }
.lane-t { font-family: var(--f-display); font-weight: 600; font-size: 13px; letter-spacing: .06em; fill: var(--fg); }
.lane-s { font-size: 11px; fill: var(--muted); }
.group { fill: none; stroke: var(--md); stroke-dasharray: 3 3; stroke-width: 1; }
.node rect { stroke-width: 1.5; }
.node.rm > rect { fill: var(--rm-bg); stroke: var(--rm); }
.node.md > rect { fill: var(--md-bg); stroke: var(--md); }
.node.ma > rect { fill: var(--ma-bg); stroke: var(--ma); }
.node.fbx > rect { fill: var(--fbx-bg); stroke: var(--fbx); }
.node.eng > rect { fill: var(--eng-bg); stroke: var(--eng); stroke-dasharray: 5 3; }
.node.tool > rect { fill: var(--tool-bg); stroke: var(--edge); }
.node.scene > rect { fill: var(--scene-bg); stroke: var(--edge); stroke-dasharray: 4 3; }
.t { font-weight: 600; font-size: 13px; }
.s { font-size: 11px; fill: var(--muted); }
.m { font-family: var(--f-mono) !important; font-size: 10.5px; fill: var(--fg); }
.strong { font-weight: 600; }
.em { font-style: italic; }
.tag { font-family: var(--f-mono) !important; font-size: 10px; fill: var(--muted); }
.badge { fill: var(--accent); }
.badge-t { font-family: var(--f-mono) !important; font-size: 11px; font-weight: 500; fill: var(--paper); }
.dot.rm { fill: var(--rm); } .dot.md { fill: var(--md); } .dot.fbx { fill: var(--fbx); }
.rule { stroke: var(--fbx); stroke-opacity: .4; }
.edge { fill: none; stroke: var(--edge); stroke-width: 1.4; }
.edge.dash { stroke-dasharray: 5 4; }
.ahp { fill: var(--edge); }
.elabel { font-family: var(--f-mono) !important; font-size: 10px; fill: var(--muted); }
</style>

<div class="wrap">
<header>
  <div class="eyebrow">RigModules + mayadev · D29, D42, D54, D70</div>
  <h1>RigModules Asset Flow</h1>
  <p class="lede">The files an asset holds, the commands and tools that move each one between disk and the Maya scene, and where poses, animation and clips go once they leave the rig. Drawn from the user docs on <code>main</code> of both repos (RigModules <code>__RM_SHA__</code>, mayadev <code>__MD_SHA__</code>).</p>
</header>

<div class="legend" aria-label="Legend">
  <span><i class="sw rm"></i>RigModules format: C++ or <code>rigmodules</code> reads it</span>
  <span><i class="sw md"></i>mayadev format: <code>bmaya.core.asset</code></span>
  <span><i class="sw ma"></i>Maya ASCII scene</span>
  <span><i class="sw fbx"></i>FBX and its sidecars</span>
  <span><i class="sw tool"></i>command or tool</span>
  <span><i class="sw scene"></i>scene node</span>
</div>

<div class="scroller">
__SVG__
</div>
<p class="hint">Scroll sideways on a narrow screen. Arrows point the way data moves; a two-headed arrow is a load and a save of the same file.</p>

<section>
  <h2>What each file binds by</h2>
  <p class="lede">No file finds its target by node name. Four keys do the matching, so files survive renames, rebuilds and re-imports.</p>
  <div class="keys">
    <div class="key"><b>joint markup [side, usage]</b><p>Weight file influences, JointClips, bone maps and the skeleton file itself.</p><span class="who">Set with <code>cmds.rigJointMarkup</code>. A bone map supplies it for an FBX that has none.</span></div>
    <div class="key"><b>control markup</b><p>Shape files, ControlPoses and ControlAnims. Unique per rig, which is why poses and animation belong to a rig.</p><span class="who">Set by the build (D25). Not editable.</span></div>
    <div class="key"><b>meshId</b><p>Weight files and <code>textures.json</code> find their mesh by this GUID on the transform.</p><span class="who"><code>cmds.setRigMeshId</code> (Mark Meshes for Skinning). Load by Name is the repair when ids changed.</span></div>
    <div class="key"><b>skeletonHash</b><p>Stamped on the rig root, weight files and library meta. A mismatch marks a file stale and never fails a build (D70).</p><span class="who">Returned by <code>cmds.saveRigSkeleton</code>. Covers markup, names, parents, rotate order, rests to 1e-4.</span></div>
  </div>
</section>

<section>
  <h2>Formats</h2>
  <div class="tbl"><table>
    <thead><tr><th>File</th><th>Format</th><th>Ver</th><th>Lives in</th><th>Written by</th><th>Read by</th></tr></thead>
    <tbody>
      <tr><td>project.json</td><td><span class="own md">mayadev</span></td><td class="v">3</td><td>project folder</td><td><code>Project.create</code>, New Project…</td><td><code>Project</code>, Registry. Older versions update themselves on load</td></tr>
      <tr><td>asset.json</td><td><span class="own md">mayadev</span></td><td class="v">2</td><td>asset folder</td><td><code>Asset.create</code>, New Asset…</td><td><code>Asset</code>, Registry</td></tr>
      <tr><td>rig.json / look.json</td><td><span class="own md">mayadev</span></td><td class="v">1</td><td><code>rigs/{rig}/</code>, <code>looks/{look}/</code></td><td><code>add_rig</code>, <code>add_look</code></td><td><code>build_rig</code>, <code>apply_look</code>, library sources, Registry</td></tr>
      <tr><td>skeleton.json</td><td><span class="own rm">RigModules</span></td><td class="v">1</td><td><code>skeletons/{skeleton}/</code></td><td><code>cmds.saveRigSkeleton</code></td><td><code>cmds.loadRigSkeleton</code>; publish breaking-change check</td></tr>
      <tr><td>template.json</td><td><span class="own rm">RigModules</span></td><td class="v">1</td><td><code>rigs/{rig}/</code></td><td>template handler (copies a shared template, sets <code>rigName</code>), Asset Editor hand edit</td><td><code>cmds.createRigFromTemplate</code></td></tr>
      <tr><td>shapes.json</td><td><span class="own rm">RigModules</span></td><td class="v">2</td><td><code>rigs/{rig}/</code></td><td><code>cmds.saveRigControlShapes</code></td><td><code>cmds.loadRigControlShapes</code></td></tr>
      <tr><td>weights/&lt;mesh&gt;.json</td><td><span class="own rm">RigModules</span></td><td class="v">3</td><td><code>looks/{look}/weights/</code></td><td><code>cmds.saveRigSkinWeights</code> (one per skinned mesh)</td><td><code>cmds.setRigLook</code> (with fallbacks), <code>cmds.loadRigSkinWeights</code> (exact)</td></tr>
      <tr><td>textures.json</td><td><span class="own md">mayadev</span></td><td class="v">1</td><td><code>looks/{look}/textures/</code></td><td><code>save_textures</code>, <code>import_textures</code></td><td><code>load_textures</code>, <code>apply_look</code></td></tr>
      <tr><td>*_model.ma, *_rig.ma</td><td><span class="own ma">Maya ASCII</span></td><td class="v">–</td><td><code>looks/{look}/model/</code>, <code>rigs/{rig}/</code></td><td>Save Model (meshes only), Save Rig</td><td>Import into a namespace, never referenced</td></tr>
      <tr><td>*.controlpose.json</td><td><span class="own rm">RigModules</span></td><td class="v">3</td><td>library sources</td><td><code>cmds.saveRigPose</code> (with <code>meta</code>)</td><td><code>cmds.loadRigPose</code>, <code>getRigDataFileInfo</code> for the browser</td></tr>
      <tr><td>*.controlanim.json</td><td><span class="own rm">RigModules</span></td><td class="v">3</td><td>library sources, beside a clip</td><td><code>cmds.saveRigAnimation</code></td><td><code>cmds.loadRigAnimation</code>; the <code>sidecar</code> of <code>animateRigFromJointClip</code></td></tr>
      <tr><td>JointClip .json</td><td><span class="own rm">RigModules</span></td><td class="v">1</td><td><code>%TEMP%\bmaya_joint_clips</code> (transient)</td><td><code>capture_joint_clip</code>, <code>read_fbx</code></td><td><code>write_fbx</code>, <code>cmds.animateRigFromJointClip</code>, <code>cmds.poseRigFromJointPose</code></td></tr>
      <tr><td>&lt;name&gt;.fbx</td><td><span class="own fbx">FBX</span></td><td class="v">–</td><td><code>skeletons/{s}/exports/&lt;name&gt;/</code>, <code>sources/</code></td><td><code>rigmodules.jointclip.write_fbx</code>; sources are copied in by hand</td><td><code>read_fbx</code>, the engine importer</td></tr>
      <tr><td>*.bonemap.json</td><td><span class="own rm">rigmodules</span></td><td class="v">1</td><td>beside an FBX</td><td><code>write_bone_map</code>, the skeleton export</td><td><code>read_fbx</code></td></tr>
      <tr><td>*.animproperties.json</td><td><span class="own md">mayadev</span></td><td class="v">1</td><td>beside a clip</td><td><code>animproperties.write</code>, Edit Clip Properties…</td><td>The engine importer only</td></tr>
      <tr><td>publish.json</td><td><span class="own md">mayadev</span></td><td class="v">–</td><td><code>…/publish/&lt;id&gt;/</code></td><td><code>publishes.create</code> via <code>publish_rig</code> / <code>publish_look</code></td><td>Registry <code>latest_publish</code>, the breaking-change check</td></tr>
      <tr><td>masks.json</td><td><span class="own md">mayadev</span></td><td class="v">–</td><td>beside the library folders</td><td><code>library.save_mask</code></td><td>Library browser scope</td></tr>
    </tbody>
  </table></div>
</section>

__CONFLICTS__

<p class="src">Sources: RigModules <code>design/asset-model.md</code>, <code>docs/library.md</code>, <code>docs/skinning.md</code>, <code>docs/skeletons.md</code>, <code>docs/looks.md</code>, <code>docs/python.md</code>, <code>docs/commands.md</code>; mayadev <code>docs/assets.md</code>, <code>docs/clips.md</code>, <code>docs/tools.md</code>. The upstream art stages in <code>design/asset-pipeline.md</code> are a proposal and are not drawn.</p>
</div>
"""



def conflicts_section():
    if not DOC_CONFLICTS:
        return ""
    items = "\n".join(f"    <li>{item}</li>" for item in DOC_CONFLICTS)
    return ('<section>\n  <h2>Where the docs disagree</h2>\n'
            '  <p class="lede" style="margin-bottom:12px">The diagram follows the code where these conflict.</p>\n'
            f'  <ol class="flags">\n{items}\n  </ol>\n</section>')


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", default="asset-flow.html", help="the HTML file to write")
    parser.add_argument("--rigmodules-sha", required=True, help="the RigModules main commit the docs were read at")
    parser.add_argument("--mayadev-sha", required=True, help="the mayadev main commit the docs were read at")
    args = parser.parse_args()
    text = (page.replace("__SVG__", SVG).replace("__RM_SHA__", args.rigmodules_sha)
            .replace("__MD_SHA__", args.mayadev_sha).replace("__CONFLICTS__", conflicts_section()))
    with open(args.out, "w", encoding="utf-8") as handle:
        handle.write(text)
    print(args.out)


if __name__ == "__main__":
    main()
