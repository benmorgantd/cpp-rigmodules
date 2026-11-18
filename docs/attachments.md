# Attachments

An attachment is a rig with its own skeleton that follows another rig: a face, a cape, a prop. It is built from its own template into its own namespace, and it takes the sockets it needs from the rig it follows (the parent rig). Both rigs stay ordinary rigs. Nothing about the parent knows the attachment exists, apart from a record on the rig root.

## In a template

A socket of the parent rig is written `parent:module.socket`. It is valid in two places.

**`parentStructure` values.** A child plug follows the parent's socket:

```json
{ "moduleType": "FkChain", "name": "face_chain", "shapeType": "Box", "side": "Center",
  "moduleArgs": { "joints": [["Center", "f_a"], ["Center", "f_b"]] },
  "parentStructure": { "start": "parent:fkchain_1.end" } }
```

**Constraint drivers.** A control the parent drives:

```json
{ "name": "neck_follow", "target": "neck_copy.start",
  "drivers": [ { "socket": "parent:fkchain_1.index_1", "label": "Body" } ] }
```

The template therefore lists everything it needs from the parent. There is no separate mapping. A template with a `parent:` id is an attachment and can only be built with a parent rig.

- The part after the prefix is `module.socket`, with the module's `name` in the parent rig. `parent:layout.end` is the parent's layout socket.
- A `parent:` id never takes part in the template's own loop check. The parent rig is not part of the template.
- In a mirrored module or constraint, a `parent:` id swaps the side suffix of the parent module's name: `parent:arm_l.end` becomes `parent:arm_r.end`. A parent module with no `_l` or `_r` suffix, such as `parent:head.end`, stays as it is.
- Labels: a driver's label defaults to its socket id, `parent:` included. Give a driver a `label` when the animator will see it.

## Building

```python
cmds.createRigFromTemplate(*face_joints, template="C:/rigs/face.json", asset="shooter\\characters\\hero",
                           namespace="hero_face", parentRig="hero_body:rigRootModule1")
```

`parentRig` is the parent's rig root. It is checked with the rest of the template, before anything is built:

- A template with `parent:` ids needs `parentRig`, and a template without any refuses one.
- `parentRig` must be a rig root, in another namespace than the one being built.
- Every socket the template takes from the parent must exist there. Every missing one is listed in the same report, so a body built from another template shows all of its mismatches at once.

On a failed check nothing is built. See [`createRigFromTemplate`](commands.md#createrigfromtemplate).

## What the animator sees

- A child plug that follows a `parent:` socket moves with the parent's control, as it would with a socket in its own rig.
- A control whose constraint has only `parent:` drivers is driven by the body. It is hidden, its transform and visibility are locked and out of the channel box, and it sits at rest, so keys left on it can never surprise an animator. A control with another driver as well (a `layout` socket, say) is a normal space switch and stays free.
- The attachment's controls stay in the attachment's own rig root, under its own groups. Only the matrices connect across rigs.
- The attachment has its own poses and animation, saved and loaded like any rig's ([library.md](library.md)).

## Records

`getRigInfo` on any node of the attachment returns `parentRigRoot`, the parent's rig root, and `null` for a rig that is not an attachment. In Python it is `RigInfo.parent_rig_root` ([python.md](python.md)). The parent's rig root lists its attachments on its `attachments` message array.

## Changing the parent

The follow is a set of connections written at build time. To attach to a different body, or to detach, rebuild the attachment. Its animation survives because its control names are unchanged and the controls the body drives carry no keys.

## Not covered

- A look that spans a rig and its attachment: [`setRigLook`](commands.md#setriglook) works on one rig root, so a tool applies the body's look and the face's look with one call each. mayadev's `apply_look` does this: it hands each rig only the weight files whose `skeletonHash` is the rig's (`rigmodules.skinweights.read_file_skeleton_hash`) and the meshes those files are for ([mayadev.md](mayadev.md)).
- The export of an attachment's skeleton. It is its own skeleton file and has its own hash ([skeletons.md](skeletons.md)).
