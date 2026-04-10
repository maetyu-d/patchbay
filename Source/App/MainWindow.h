#pragma once

#include <JuceHeader.h>

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow();

    void closeButtonPressed() override;
};

