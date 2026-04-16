#include "MainWindow.h"
#include "../UI/MainComponent.h"
#include <cstdlib>

MainWindow::MainWindow()
    : juce::DocumentWindow("PatchBay DAW",
                           juce::Colour(0xff10141c),
                           juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new MainComponent(), true);
    setResizable(true, true);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

void MainWindow::closeButtonPressed()
{
    if (auto* mainComponent = dynamic_cast<MainComponent*>(getContentComponent()))
    {
        auto safeWindow = juce::Component::SafePointer<MainWindow>(this);
        mainComponent->attemptWindowCloseAsync([safeWindow](MainComponent::CloseDecision decision)
        {
            if (decision == MainComponent::CloseDecision::cancel || safeWindow == nullptr)
                return;

            if (auto* component = dynamic_cast<MainComponent*>(safeWindow->getContentComponent()))
                component->prepareForQuit();

            if (decision == MainComponent::CloseDecision::discardAndQuit)
                std::_Exit(0);

            juce::MessageManager::callAsync([safeWindow]
            {
                if (safeWindow != nullptr)
                    safeWindow->setVisible(false);

                for (int index = juce::TopLevelWindow::getNumTopLevelWindows() - 1; index >= 0; --index)
                    if (auto* window = juce::TopLevelWindow::getTopLevelWindow(index))
                        window->setVisible(false);

                if (auto* app = juce::JUCEApplication::getInstance())
                    app->setApplicationReturnValue(0);

                if (auto* manager = juce::MessageManager::getInstanceWithoutCreating())
                    manager->stopDispatchLoop();
            });

            juce::Timer::callAfterDelay(250, []
            {
                std::_Exit(0);
            });
        });
        return;
    }
}
