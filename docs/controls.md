# Controls

Every control is a `rigControlNode`: a transform that draws its own shape in Viewport 2.0. Its module creates it and computes its world matrix. This page covers what a control carries besides its transform: its markup, its shape, and the shape file that saves shapes between builds.

## Control markup

The build writes markup on every control, so tools find controls by what they are instead of parsing names.

| Control | Markup |
|---|---|
| A control made for a joint (FkChain, Aim target and up) | The joint's `side` and `usage` |
| Layout control *n* | `Center`, `layout_<n>` |

The attribute is `controlMarkup`, a JSON string: `{"markupVersion": 1, "side": "Left", "usage": "upperArm"}`. It is not keyable. The key names are public, so don't rename them.

- `(side, usage)` is unique across a rig's controls. The build fails if a joint's markup would give a control the same pair as a layout control (a `Center` joint with usage `layout_0`, for example).
- The control's color and name suffix come from its module's `side`, not from its markup.
- Find controls with [`getRigControls`](commands.md#getrigcontrols).

## Shapes

A control's shape is set by these attributes:

| Attribute | Meaning |
|---|---|
| `shapeType` | The shape (list below). Templates set it per module with `shapeType`. |
| `shapeTransform` | Matrix from shape space to the control's object space: scale, rotate and offset the shape without moving the control |
| `filled` | Draws the primitive shapes solid. Curve shapes ignore it. |
| `wireframeColor`, `wireframeAlpha` | Color. The build sets the color from the module's side. |

**One axis convention for every shape:** +X is the normal of a flat shape and the long axis of `Capsule` and `Cylinder`. Scale X of `shapeTransform` sizes the shape along that axis, and scale Y and Z size it across. On a joint with +X down the bone, a `Circle` wraps the bone without any rotation.

At scale 1 every shape fits a cube from -1 to 1, except `Capsule`, which is 3 long (a 1-long middle plus a cap of radius 1 at each end), and `Cylinder`, which is 1 long. Round shapes can't be squashed: `Circle`, `Capsule` and `Cylinder` take their radius from scale Y, and `Sphere` takes its radius from scale X. `Box`, `Square` and the curve shapes scale freely on every axis.

| Kind | Shapes |
|---|---|
| Primitives (can be `filled`) | `Box`, `Sphere`, `Circle`, `Capsule`, `Cylinder`, `Square` |
| Flat curves, in the YZ plane | `Triangle`, `HalfCircleArrow`, `PointedCircle`, `CircleArrow`, `SingleArrow`, `FourArrow`, `CircleStick` |
| 3D curves | `ArchedDoubleArrow`, `Diamond`, `Dome`, `Pivot` |

Curve shapes are scaled so their largest coordinate is 1, so a given `shapeTransform` scale gives a similar size on every shape.

The control's bounding box (`boundingBoxMin`, `boundingBoxMax`) follows `shapeTransform`, so framing a control (F) frames its shape.

## Shape file

Riggers edit control shapes per asset. The shape file saves them so a rig rebuilt from its template gets them back. There is one file per rig, keyed by control markup, so a renamed control keeps its shape as long as its joint keeps its markup.

```python
cmds.saveRigControlShapes("rig:rigRootModule1", file="C:/assets/hero/shapes.json")
# ... rebuild the rig from its template ...
cmds.loadRigControlShapes("rig:rigRootModule1", file="C:/assets/hero/shapes.json")
```

```json
{
    "formatVersion": 1,
    "controls": {
        "Left": {
            "arm": {
                "shapeType": "Circle",
                "shapeTransform": [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0],
                "filled": false
            }
        }
    }
}
```

- `controls` maps side, then usage, to one control's `shapeType`, `shapeTransform` (16 numbers, row-major) and `filled`. Every key is required, and unknown keys are errors.
- Color is not saved. It comes from the side of the module a control is built on.
- Loading checks the whole file first. Any error loads nothing.
- A control with no entry keeps its shape, and an entry with no control is skipped. Both print a warning.
