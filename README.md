# PatchBay DAW

`PatchBay DAW` is a JUCE/C++ modular DAW prototype with a patch-cable canvas, a standalone host app, and separate plugin targets for each built-in module.

## What is implemented

- A JUCE desktop app with an audio callback and live output.
- Separate plugin targets for:
  - Oscillator
  - LFO
  - Gain
  - Output
- A graph engine that supports:
  - audio ports
  - modulation ports
  - draggable node positions
  - patch-cable connections between compatible sockets
  - topological processing for acyclic graphs
- Example modules:
  - Oscillator
  - LFO
  - Gain
  - Output
- A visual canvas where you can:
  - add modules
  - drag nodes
  - click one socket, then another compatible socket, to create a cable
  - select a node and press `Delete` or `Backspace` to remove it

## Build

This project expects a local JUCE checkout because the workspace does not currently include JUCE itself.

```bash
cmake -S . -B build -DJUCE_SOURCE_DIR=/absolute/path/to/JUCE
cmake --build build
```

On macOS, the build produces:

- `PatchBay DAW.app`
- `PatchBay Oscillator` as `VST3`, `AU`, and standalone
- `PatchBay LFO` as `VST3`, `AU`, and standalone
- `PatchBay Gain` as `VST3`, `AU`, and standalone
- `PatchBay Output` as `VST3`, `AU`, and standalone

## Recommended first patch

Create this chain after launching:

1. Add `Oscillator`
2. Add `Gain`
3. Add `Output`
4. Connect `Oscillator.audioOut -> Gain.audioIn`
5. Connect `Gain.audioOut -> Output.audioIn`

Then add an `LFO` and connect `LFO.value -> Gain.gainCV` for modulation.

## Important platform notes

- `VST3` and `AudioUnit` targets are built in the current CMake setup.
- `AUv3` is wired at the CMake level, but JUCE only enables it when generating with Xcode on macOS.
- Cross-plugin modulation between separate plugin binaries is not a standard DAW capability. The rich audio/modulation patching currently happens inside the standalone `PatchBay DAW` host.
- The module plugins are still useful as standalone building blocks and as the start of a reusable module SDK.

## Architectural direction

The project is structured so the graph engine and module API are reusable. That lets us move toward:

- standalone patchable DAW host
- per-module plugin shells
- a shared module SDK across app and plugin targets
- future persistence, automation lanes, tempo sync, and transport

See [docs/ARCHITECTURE.md](/Users/user/Documents/Plugin%20DAW/docs/ARCHITECTURE.md) for the roadmap and constraints.
