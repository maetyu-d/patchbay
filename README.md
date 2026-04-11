# PatchBay DAW

`PatchBay DAW` is a JUCE/C++ modular DAW prototype with a patch-cable canvas, a standalone host app, detachable editors, track modules, and separate plugin targets for the built-in modules.

## What is implemented

- A JUCE desktop app with an audio callback and live output.
- Edit and Performance modes, toggled with `Cmd+E`.
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
  - BPM to LFO
  - Time Signature
  - AD Envelope
  - ADSR Envelope
  - Filter
  - Gain
  - Add / Subtract / Multiply / Divide
  - Sum
  - Router
  - Output
  - Audio Track
  - MIDI Track
  - External Plugin host node for AU and VST3
- A visual canvas where you can:
  - right-click anywhere to add modules
  - drag nodes
  - click one socket, then another compatible socket, to create a cable
  - select a cable or node and press `Delete` or `Backspace` to remove it
  - zoom with `=` and `-`
  - pan with the arrow keys
  - double-click track nodes and plugin nodes to open their detached editors
  - resize the inspector by dragging its left divider

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

## Demo Patch

The app now boots into a fuller demo session:

1. `BPM to LFO -> Time Signature`
2. `Time Signature` and `BPM to LFO` drive the track playback rates automatically
3. `AD` and `ADSR` envelopes open the filter and shape gain
4. `LFO` still modulates loop points
5. `Audio Track`, `MIDI Track`, and `Oscillator` are summed, filtered, amplified, and sent to output

The default patch is immediately audible from the oscillator and MIDI track. To bring the audio track in, select it and load a clip, then double-click the node to edit its waveform range and loop markers.

Newly created audio and MIDI tracks are auto-wired into the existing timing and mix graph when suitable clocking and mix nodes already exist, so building out larger patches is faster.

Useful controls:

- `Cmd+E`: switch between `Edit` and `Performance`
- Right-click canvas: create nodes at the click position
- `=` / `-`: zoom patch canvas
- Arrow keys: pan patch canvas
- `Delete` / `Backspace`: remove selected cable or node

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
