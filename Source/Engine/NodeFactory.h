#pragma once

#include "ModuleNode.h"

class NodeFactory
{
public:
    static juce::StringArray getAvailableTypes();
    static std::unique_ptr<ModuleNode> create(const juce::String& type);
    static std::unique_ptr<ModuleNode> createExternal();
    static std::unique_ptr<ModuleNode> createExternal(const juce::PluginDescription& description);
    static std::unique_ptr<ModuleNode> createFromState(const juce::ValueTree& state);
};
