#pragma once

#include "ModuleNode.h"
#include <vector>

struct NodeMenuSection
{
    juce::String title;
    juce::StringArray types;
};

class NodeFactory
{
public:
    static juce::StringArray getAvailableTypes();
    static std::vector<NodeMenuSection> getMenuSections();
    static juce::String getDisplayNameForType(const juce::String& type);
    static std::unique_ptr<ModuleNode> create(const juce::String& type);
    static std::unique_ptr<ModuleNode> createExternal();
    static std::unique_ptr<ModuleNode> createExternal(const juce::PluginDescription& description);
    static std::unique_ptr<ModuleNode> createFromState(const juce::ValueTree& state);
};
