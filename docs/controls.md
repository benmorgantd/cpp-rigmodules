# Controls

Every control is a `rigControlNode`: a transform that draws its own shape in Viewport 2.0. Its module creates it and computes its world matrix. This page covers what a control carries besides its transform: its markup, its channels, its shape, and the shape file that saves shapes between builds.

## Control markup

The build writes markup on every control, so tools find controls by what they are instead of parsing names.

| Control | Markup |
|---|---|
| A control made for a joint (FkChain, Aim target and up, all six of a TwoBoneIk) | The joint's `side` and `usage` |
| Layout control *n* | `Center`, `layout_<n>` |

The attribute is `controlMarkup`, a JSON string: `{"markupVersion": 1, "side": "Left", "usage": "upperArm"}`. It is not keyable. The key names are public, so don't rename them.

- `(side, usage)` is unique across a rig's controls. The build fails if a joint's markup would give a control the same pair as a layout control (a `Center` joint with usage `layout_0`, for example).
- The control's color and the side letter in its name come from its module's `side`, not from its markup.
- A **marker joint** is a joint that exists only to place a control: Aim's `target` and `up`. Mark it `export: false, skin: false`, so it never reaches the engine skeleton or the skin. Its control takes its markup like any other.
- Find controls with [`getRigControls`](commands.md#getrigcontrols).

## Channels

A control's channels are its keyable attributes that aren't locked or driven: the transforms, plus anything a module or a [space switch](commands.md#createrigconstraint) adds (the `space` enum). They are what an animator keys, and what the pose commands read and write.

- A channel's **rest** is its attribute default, so a build pose is every channel at rest. [`zeroRigControls`](commands.md#zerorigcontrols) goes there.
- Its **mirror mode** is `copy` or `negate`. Transforms copy. A custom channel states its own as an attribute category (`mirrorCopy` or `mirrorNegate`), and the pose commands refuse a control that has a custom channel with neither.
- The **TwoBoneIk** module adds `ikFkBlend`, `stretch` and `soft` (its settings control), `pin` and a `space` (its pole) and `roll` (its IK handle). The first four are numbers from 0 to 1 that copy in a mirror. `roll` is an angle and **negates**: a reflection reverses the sense of a rotation about the limb's axis. The values and what they do are in [modules.md](modules.md#twoboneik).
- A control's **rotate order** is set by its module and locked, so it isn't a channel and can't be changed in the channel box. FkChain controls use XYZ: Y, the side swing, is the middle axis that gimbal locks at 90 degrees, Z is the bend and X the twist.
- A control can carry custom channels and no transform channels: the [ReverseFoot](modules.md#reversefoot)'s control has its translate, rotate and scale locked and hidden and holds the foot's 11 angle channels, each with its rest and mirror mode (`copy` or `negate`). Its toe FK control (`toeFK_00_L_CTRL`) is rotate only, and the blend hides it at blend 1 (and the foot control at blend 0).
- A channel a module drives is not a channel. The TwoBoneIk module drives the `shown` attribute of its FK lower and end controls, IK handle and pole, and the ReverseFoot its foot and toe FK controls, from the ik/fk blend. `shown` is not keyable and not a channel. Those controls' `visibility` is locked and out of the channel box, so they have no `visibility` channel and the pose commands skip it. A control that is not `shown` draws nothing and cannot be picked, while its DAG children are unaffected (D78). The Ribbon's and the Limb's tweakers are not `shown` either.
- A **module-owned space** is a `space` enum that the module itself reads (the TwoBoneIk pole's `Parent` and `Ik`), not a [constraint](commands.md#createrigconstraint). The labels are fixed by the module, so a template can't add one. [`getRigControlSpace`](commands.md#getrigcontrolspace) reports it with the `module` key set, and [`setRigControlSpace`](commands.md#setrigcontrolspace) switches it and keeps the control in place, as it does for a constraint space. Setting the enum directly moves the control without matching, like setting a constraint's `space` directly.
- Channel names are public API, like markup: saved poses and animation depend on them. A rename needs an entry in the alias list (see D32).
- Query them with [`getRigControlChannels`](commands.md#getrigcontrolchannels). [`getRigControlInfo`](commands.md#getrigcontrolinfo) gives a control's module, side, usage and mirror.

## Shapes

A control's shape is set by these attributes:

| Attribute | Meaning |
|---|---|
| `shapeType` | The shape (list below). Templates set it per module with `shapeType`. |
| `shapeTransform` | Matrix from shape space to the control's object space: scale, rotate and offset the shape without moving the control |
| `filled` | Off by default. When on, fills the shape at 0.75 of the wire's alpha, with the wire drawn on top. Every shape can be filled. |
| `xray` | On by default. Draws the control in front of meshes and other geometry. Not saved in the shape file. |
| `wireframeColor`, `wireframeAlpha` | Color. The build sets the color from the module's side. `wireframeAlpha` (0 to 1) is saved in the shape file, the color is not. |

Every shape is a small mesh: its edges draw the wire and its faces draw the fill. At scale 1 every shape fits a cube from -1 to 1, and `shapeTransform` scales every shape freely on every axis, round ones included.

**Orientation**, so a shape on a joint with +X down the bone needs no rotation:

| Kind | Shapes | Orientation |
|---|---|---|
| Rings | `Circle`, `Square`, `Triangle`, `PointedCircle`, `HalfCircleArrow`, `CircleArrow` | Flat in the YZ plane, facing +X, so they wrap the bone. The circle arrows show a rotation about it. |
| Pointers | `SingleArrow`, `FourArrow`, `CircleStick`, `ArchedDoubleArrow` | Point down +X. The flat ones lie in the XZ plane. `ArchedDoubleArrow` arches over +Y with its tips at x = -1 and 1. |
| Solids | `Box`, `Sphere`, `Cylinder`, `Capsule`, `Dome`, `Diamond`, `Pivot` | Round ones run along +X. `Capsule` fills the cube like `Box` (its round ends are flattened along X), and `Dome` bulges toward +X. `Pivot` is three flat double arrows, one per axis. `Axis` is three arrows from the origin down +X, +Y and +Z, drawn red, green and blue like Maya's manipulator (the selection colour when selected, and `wireframeColor` is not used): it is the marker locators' shape. |

The shape data is generated by `tools/generate_control_shapes.py` into `cpp/core/ControlShapeLibrary.cpp`. To change a shape, edit the generator, rerun it with mayapy and rebuild.

The control's bounding box (`boundingBoxMin`, `boundingBoxMax`) follows `shapeTransform`, so framing a control (F) frames its shape.

Set the shape attributes. Don't drive them with a connection that animates: the control reads its shape and bounds again only when one of them is edited, and parallel evaluation doesn't report a change that arrives through a connection during playback.

A selected control draws in the selection color with a thicker wire. A control that is only highlighted, because a control above it is selected, draws fainter and keeps its normal wire width.

**Profiling:** the control's draw callbacks report to Maya's Profiler in the `RigModules` category (`rigControl prepareForDraw`, `rigControl addUIDrawables`, `rigControl boundingBox` and others). During playback they don't run: Viewport 2.0 keeps each control's drawing and moves it with the control's matrix. They run when a control is first drawn, when its shape attributes change and when its selection state changes.

## Shape file

Riggers edit control shapes per asset. The shape file saves them so a rig rebuilt from its template gets them back. There is one file per rig, keyed by control markup, so a renamed control keeps its shape as long as its joint keeps its markup.

```python
cmds.saveRigControlShapes("hero_body:rigRootModule1", file="C:/assets/hero/shapes.json")
# ... rebuild the rig from its template ...
cmds.loadRigControlShapes("hero_body:rigRootModule1", file="C:/assets/hero/shapes.json")
```

```json
{
    "formatVersion": 2,
    "controls": {
        "Left": {
            "arm": {
                "shapeType": "Circle",
                "shapeTransform": [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0],
                "filled": false,
                "wireframeAlpha": 0.5
            }
        }
    }
}
```

- `controls` maps side, then usage, to one control's `shapeType`, `shapeTransform` (16 numbers, row-major), `filled` and `wireframeAlpha` (0 to 1). Every key is required, and unknown keys are errors.
- Color is not saved. It comes from the side of the module a control is built on. Alpha is saved, because the build doesn't set it.
- Version 1 files had no `wireframeAlpha`. They still load, with alpha 1, the attribute's default. The next save writes version 2.
- Loading checks the whole file first. Any error loads nothing.
- A control with no entry keeps its shape, and an entry with no control is skipped. Both print a warning.

## Mirroring shapes

`mirrorRigControlShapes` copies each Left control's shape onto the Right control with the same `usage`, reflected across world X = 0, or Right onto Left with `leftToRight=False` ([commands.md](commands.md#mirrorrigcontrolshapes)). Edit one side, mirror it, then save the shape file.

```python
cmds.mirrorRigControlShapes("hero_body:rigRootModule1")
cmds.saveRigControlShapes("hero_body:rigRootModule1", file="C:/assets/hero/shapes.json")
```

`getRigMirrorControl` finds one control's mirror the same way, by markup.
