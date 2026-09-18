#pragma once

#pragma warning(push)
#pragma warning(disable: 26495) // Suppress Maya SDK static analysis warnings
#include <maya/MPxTransform.h>
#include <maya/MPxDrawOverride.h>
#include <maya/MUserData.h>
#include <maya/MColor.h>
#include <maya/MTypeId.h>
#pragma warning(pop)

// Data payload passed to Viewport 2.0 draw thread
// Read MPlugs in prepareForDraw, feed to this class, then read from it in addUIDrawables
class ControlDrawData : public MUserData {
public:
    ControlDrawData() : MUserData() {}
    ~ControlDrawData() override = default;

    int shapeType{ 0 };
    MColor color{ 1.0f, 0.0f, 0.0f, 1.0f };
    MColor dormantColor{ 1.0f, 0.0f, 0.0f, 1.0f };
    float lineWidth{ 1.0f };
    MPoint centerOffset{ 0.0f, 0.0f, 0.0f };
    MVector normalVector{ 1.0f, 0.0f, 0.0f };
    MVector upVector{ 0.0f, 0.0f, 1.0f };
    double width{ 1.0f };
    double height{ 1.0f };
    double depth{ 1.0f };
};

// Custom Transform DAG Node
class RigControlNode : public MPxTransform {
public:
    RigControlNode();
    ~RigControlNode() override;

    static void* creator();
    static MStatus initialize();

    // Override setDependentsDirty from MPxNode / MPxTransform so each draw doesn't trigger MPlug reads
    MStatus setDependentsDirty(const MPlug& plugBeingDirtied, MPlugArray& affectedPlugs) override;

    static MTypeId id;
    static MString drawDbClassification;
    static MString drawRegistrantId;

    static MObject aShapeType;
    static MObject aWireColor;
    static MObject aWireAlpha;
    static MObject aCenterOffset;
    static MObject aNormalVector;
    static MObject aUpVector;
    static MObject aWidth;
    static MObject aHeight;
    static MObject aDepth;

    bool isDrawDirty() const { return m_drawIsDirty; }
    void setDrawClean() { m_drawIsDirty = false; }
    void setDrawDirty() { m_drawIsDirty = true; }

private:
    bool m_drawIsDirty{ true };
};

// Viewport 2.0 Draw Override
class RigControlDrawOverride : public MHWRender::MPxDrawOverride {
public:
    static MHWRender::MPxDrawOverride* Creator(const MObject& obj);

    RigControlDrawOverride(const MObject& obj);
    ~RigControlDrawOverride() override = default;

    MHWRender::DrawAPI supportedDrawAPIs() const override {
        return (MHWRender::kOpenGL | MHWRender::kDirectX11 | MHWRender::kOpenGLCoreProfile);
    }

    bool isBounded(const MDagPath& objPath, const MDagPath& cameraPath) const override {
        return true;
    }

    MBoundingBox boundingBox(const MDagPath& objPath, const MDagPath& cameraPath) const override {
        return MBoundingBox(MPoint(-1.0, -1.0, -1.0), MPoint(1.0, 1.0, 1.0));
    }

    // Tell VP2 to place this drawable in the transparent render queue
    virtual bool isTransparent() const override { return true; }

    bool hasUIDrawables() const override { return true; }

    MUserData* prepareForDraw(
        const MDagPath& objPath,
        const MDagPath& cameraPath,
        const MHWRender::MFrameContext& frameContext,
        MUserData* oldData) override;

    void addUIDrawables(
        const MDagPath& objPath,
        MHWRender::MUIDrawManager& drawManager,
        const MHWRender::MFrameContext& frameContext,
        const MUserData* data) override;
};