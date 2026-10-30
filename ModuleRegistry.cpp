// This is the only file that includes every module header
#include "ModuleRegistry.h"
#include "RigModule.h"
#include "LayoutModule.h"
#include "AssetRoot.h"
#include "RigRoot.h"
#include "FkChain.h"

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
            1, nullptr
        },

        {
            "rigRootModule", "RigRoot", RigRootNode::id,
            RigRootNode::creator, RigRootNode::initialize,
            1, nullptr
        },

        {
            "layoutModule", "Layout", LayoutModuleNode::id,
            LayoutModuleNode::creator, LayoutModuleNode::initialize,
            1, nullptr   // built implicitly by RigRootNode::createRig, not from JSON
        },
        {
            "fkChainModule", "FkChain", FkChainNode::id,
            FkChainNode::creator, FkChainNode::initialize,
            1, FkChainNode::buildFromJson
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