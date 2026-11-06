#pragma once
#include "maya/shared/RigModule.h"
#include <string>
#include <vector>

class ModuleRegistry
{
public:
    static const std::vector<ModuleDescriptor>& all();
    static const ModuleDescriptor* find(const std::string& moduleType);
};