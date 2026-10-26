// This is the only file that includes every module header
#include "ModuleRegistry.h"
#include "RigModule.h"
#include "LayoutModule.h"
#include "FkChain.h"

#include <vector>

const std::vector<ModuleDescriptor>& ModuleRegistry::all()
{
    // Function-local static: built on first call (during initializePlugin),
    // after every module's static MTypeId has been constructed.
    static const std::vector<ModuleDescriptor> descriptors =
    {
        {
            LayoutModuleNode::aCommmandString, "Layout", LayoutModuleNode::id,
            LayoutModuleNode::creator, LayoutModuleNode::initialize,
            1, nullptr   // built implicitly by RigRootNode::createRig, not from JSON
        },
        {
            FkChainNode::aCommandString, "FkChain", FkChainNode::id,
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