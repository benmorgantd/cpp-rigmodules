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
class ControlDrawData : public MUserData {
public:
    ControlDrawData() : MUserData() {}
    ~ControlDrawData() override = default;

    int shapeType{ 0 };
    MColor color{ 1.0f, 0.0f, 0.0f, 1.0f };
};

// Custom Transform DAG Node
class RigControlNode : public MPxTransform {
public:
    RigControlNode();
    ~RigControlNode() override;

    static void* creator();
    static MStatus initialize();

    static MTypeId id;
    static MString drawDbClassification;
    static MString drawRegistrantId;

    static MObject aShapeType;
    static MObject aWireColor;
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