#include "MainWindow.h"
#include "../UI/MainComponent.h"

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
        if (! mainComponent->attemptWindowClose())
            return;
    }

    if (auto* app = juce::JUCEApplication::getInstance())
        app->quit();
}
