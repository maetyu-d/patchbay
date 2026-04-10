#pragma once

#include "../Engine/ModuleNode.h"

std::unique_ptr<ModuleNode> createOscillatorModule();
std::unique_ptr<ModuleNode> createLfoModule();
std::unique_ptr<ModuleNode> createGainModule();
std::unique_ptr<ModuleNode> createOutputModule();
std::unique_ptr<ModuleNode> createAudioTrackModule();
std::unique_ptr<ModuleNode> createMidiTrackModule();
