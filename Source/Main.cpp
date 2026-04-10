#include <JuceHeader.h>
#include "App/MainWindow.h"

class PatchBayApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override
    {
        return "PatchBay DAW";
    }

    const juce::String getApplicationVersion() override
    {
        return ProjectInfo::versionString;
    }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>();
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(PatchBayApplication)
