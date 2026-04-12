#pragma once

#include "../Engine/ModuleNode.h"

std::unique_ptr<ModuleNode> createOscillatorModule();
std::unique_ptr<ModuleNode> createLfoModule();
std::unique_ptr<ModuleNode> createMetronomeModule();
std::unique_ptr<ModuleNode> createGainModule();
std::unique_ptr<ModuleNode> createAddModule();
std::unique_ptr<ModuleNode> createSubtractModule();
std::unique_ptr<ModuleNode> createMultiplyModule();
std::unique_ptr<ModuleNode> createDivideModule();
std::unique_ptr<ModuleNode> createComparatorModule();
std::unique_ptr<ModuleNode> createBpmToLfoModule();
std::unique_ptr<ModuleNode> createTimeSignatureModule();
std::unique_ptr<ModuleNode> createFilterModule();
std::unique_ptr<ModuleNode> createAdEnvelopeModule();
std::unique_ptr<ModuleNode> createAdsrEnvelopeModule();
std::unique_ptr<ModuleNode> createOutputModule();
std::unique_ptr<ModuleNode> createSumModule();
std::unique_ptr<ModuleNode> createRouterModule();
std::unique_ptr<ModuleNode> createAudioTrackModule();
std::unique_ptr<ModuleNode> createMidiTrackModule();
