// This is the only file that includes every module header
#include "maya/ModuleRegistry.h"
#include "maya/modules/AimModule.h"
#include "maya/modules/FkChain.h"
#include "maya/modules/LayoutModule.h"
#include "maya/nodes/AssetRoot.h"
#include "maya/nodes/Constraints.h"
#include "maya/nodes/RigRoot.h"
#include "maya/shared/RigModule.h"

#include <vector>

const std::vector<ModuleDescriptor>& ModuleRegistry::all()
{
    // Function-local static: built on first call (during initializePlugin),
    // after every module's static MTypeId has been constructed.
    static const std::vector<ModuleDescriptor> descriptors =
    {
        {
            "assetRootModule", "AssetRoot", AssetRootNode::id,
            AssetRootNode::creator, AssetRootNode::initialize,
            {}, nullptr, false
        },

        {
            "rigRootModule", "RigRoot", RigRootNode::id,
            RigRootNode::creator, RigRootNode::initialize,
            {}, nullptr, false
        },

        {
            "layoutModule", "Layout", LayoutModuleNode::id,
            LayoutModuleNode::creator, LayoutModuleNode::initialize,
            {}, nullptr, true   // built implicitly by RigRootNode::createRig, not from JSON
        },
        {
            "fkChainModule", "FkChain", FkChainNode::id,
            FkChainNode::creator, FkChainNode::initialize,
            {}, FkChainNode::planFromJson, true
        },
        {
            "aimModule", "Aim", AimModuleNode::id,
            AimModuleNode::creator, AimModuleNode::initialize,
            {}, AimModuleNode::planFromJson, true
        },
        {
            "rigConstraint", "RigConstraint", RigConstraintNode::id,
            RigConstraintNode::creator, RigConstraintNode::initialize,
            {}, nullptr, false   // built by RigConstraintNode::buildFromTemplate, not as a module
        }
    };
    return descriptors;
}

const ModuleDescriptor* ModuleRegistry::find(const std::string& moduleType)
{
    for (const ModuleDescriptor& descriptor : all())
    {
        if (moduleType == descriptor.moduleType)
        {
            return &descriptor;
        }
    }
    return nullptr;
}