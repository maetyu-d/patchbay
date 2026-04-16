#pragma once

#include <JuceHeader.h>

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow();
    ~MainWindow() override = default;

    void closeButtonPressed() override;
};
