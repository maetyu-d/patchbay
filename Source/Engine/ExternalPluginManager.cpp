#include "ExternalPluginManager.h"

ExternalPluginManager& ExternalPluginManager::getInstance()
{
    static ExternalPluginManager instance;
    return instance;
}

ExternalPluginManager::ExternalPluginManager() = default;
ExternalPluginManager::~ExternalPluginManager() = default;

void ExternalPluginManager::initialise()
{
    if (initialised)
        return;

    setupProperties();
    juce::addDefaultFormatsToManager(formatManager);
    knownPluginList.addChangeListener(this);
    loadKnownPluginList();
    initialised = true;
}

juce::Array<juce::PluginDescription> ExternalPluginManager::getKnownPlugins() const
{
    return knownPluginList.getTypes();
}

juce::StringArray ExternalPluginManager::getKnownPluginDisplayNames() const
{
    juce::StringArray names;

    for (const auto& plugin : knownPluginList.getTypes())
        names.add(plugin.name + " [" + plugin.pluginFormatName + "]");

    return names;
}

std::optional<juce::PluginDescription> ExternalPluginManager::getPluginByIdentifier(const juce::String& identifier) const
{
    if (auto description = knownPluginList.getTypeForIdentifierString(identifier))
        return *description;

    return std::nullopt;
}

std::optional<juce::PluginDescription> ExternalPluginManager::getPluginByDisplayName(const juce::String& name) const
{
    for (const auto& plugin : knownPluginList.getTypes())
    {
        const auto displayName = plugin.name + " [" + plugin.pluginFormatName + "]";
        if (displayName == name)
            return plugin;
    }

    return std::nullopt;
}

bool ExternalPluginManager::scanForPlugins(juce::String& report)
{
    initialise();

    auto before = knownPluginList.getNumTypes();
    juce::StringArray lines;

    for (auto* format : formatManager.getFormats())
    {
        const auto formatName = format->getName();

        if (formatName != "VST3" && formatName != "AudioUnit")
            continue;

        juce::PluginDirectoryScanner scanner(knownPluginList,
                                             *format,
                                             format->getDefaultLocationsToSearch(),
                                             true,
                                             properties.getUserSettings()->getFile().getSiblingFile("plugin_scan_dead_mans_pedal.txt"),
                                             false);

        juce::String pluginName;
        auto scannedCount = 0;

        while (scanner.scanNextFile(true, pluginName))
            ++scannedCount;

        lines.add(formatName + ": scanned " + juce::String(scannedCount)
                  + ", failed " + juce::String(scanner.getFailedFiles().size()));
    }

    knownPluginList.sort(juce::KnownPluginList::sortAlphabetically, true);
    saveKnownPluginList();

    const auto after = knownPluginList.getNumTypes();
    report = "Plugins known: " + juce::String(after) + " (" + juce::String(after - before) + " new)\n"
           + lines.joinIntoString("\n");
    return true;
}

std::unique_ptr<juce::AudioPluginInstance> ExternalPluginManager::createPluginInstance(const juce::PluginDescription& description,
                                                                                        double sampleRate,
                                                                                        int blockSize,
                                                                                        juce::String& errorMessage)
{
    initialise();
    return formatManager.createPluginInstance(description, sampleRate, blockSize, errorMessage);
}

void ExternalPluginManager::loadKnownPluginList()
{
    if (auto* settings = properties.getUserSettings())
    {
        if (auto xmlText = settings->getValue("knownPlugins"); xmlText.isNotEmpty())
        {
            juce::XmlDocument document(xmlText);
            std::unique_ptr<juce::XmlElement> xml(document.getDocumentElement());
            if (xml != nullptr)
                knownPluginList.recreateFromXml(*xml);
        }
    }
}

void ExternalPluginManager::saveKnownPluginList()
{
    if (auto* settings = properties.getUserSettings())
    {
        if (auto xml = knownPluginList.createXml())
        {
            settings->setValue("knownPlugins", xml.get());
            properties.saveIfNeeded();
        }
    }
}

void ExternalPluginManager::setupProperties()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "PatchBayDAW";
    options.filenameSuffix = "settings";
    options.folderName = "PatchBayDAW";
    options.osxLibrarySubFolder = "Application Support";
    properties.setStorageParameters(options);
}

void ExternalPluginManager::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &knownPluginList)
        saveKnownPluginList();
}
