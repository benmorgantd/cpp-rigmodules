// This is the only file that includes every module header
#include "maya/ModuleRegistry.h"
#include "maya/modules/AimModule.h"
#include "maya/modules/FkChainModule.h"
#include "maya/modules/LayoutModule.h"
#include "maya/modules/RibbonModule.h"
#include "maya/modules/TwoBoneIkModule.h"
#include "maya/nodes/Constraints.h"
#include "maya/nodes/LookRoot.h"
#include "maya/nodes/RigRoot.h"
#include "maya/shared/RigModule.h"

#include <vector>

const std::vector<ModuleDescriptor>& ModuleRegistry::all()
{
    // Function-local static: built on first call (during initializePlugin),
    // after every module's static MTypeId has been constructed.
    static const std::vector<ModuleDescriptor> descriptors =
    {
        // assetRootModule (0x00219) was merged into the rig root (D54). Its id is retired, never reused.
        {
            "rigRootModule", "RigRoot", RigRootNode::id,
            RigRootNode::creator, RigRootNode::initialize,
            {}, nullptr, nullptr, false
        },

        {
            "layoutModule", "Layout", LayoutModuleNode::id,
            LayoutModuleNode::creator, LayoutModuleNode::initialize,
            {}, nullptr, LayoutModuleNode::inverse, true   // built implicitly by RigRootNode::createRig, not from JSON
        },
        {
            "fkChainModule", "FkChain", FkChainModuleNode::id,
            FkChainModuleNode::creator, FkChainModuleNode::initialize,
            {}, FkChainModuleNode::planFromJson, FkChainModuleNode::inverse, true
        },
        {
            "aimModule", "Aim", AimModuleNode::id,
            AimModuleNode::creator, AimModuleNode::initialize,
            {}, AimModuleNode::planFromJson, AimModuleNode::inverse, true
        },
        {
            "twoBoneIkModule", "TwoBoneIk", TwoBoneIkNode::id,
            TwoBoneIkNode::creator, TwoBoneIkNode::initialize,
            TwoBoneIkNode::argUpdateTable(), TwoBoneIkNode::planFromJson, TwoBoneIkNode::inverse, true, TwoBoneIkNode::inverseChannels(), TwoBoneIkNode::spaceDelta
        },
        {
            "ribbonModule", "Ribbon", RibbonModuleNode::id,
            RibbonModuleNode::creator, RibbonModuleNode::initialize,
            RibbonModuleNode::argUpdateTable(), RibbonModuleNode::planFromJson, RibbonModuleNode::inverse, true, RibbonModuleNode::inverseChannels()
        },
        {
            "rigConstraint", "RigConstraint", RigConstraintNode::id,
            RigConstraintNode::creator, RigConstraintNode::initialize,
            {}, nullptr, nullptr, false   // built by RigConstraintNode::buildFromTemplate, not as a module
        },
        {
            "lookRoot", "LookRoot", LookRootNode::id,
            LookRootNode::creator, LookRootNode::initialize,
            {}, nullptr, nullptr, false   // created by setRigLook, not as a module
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