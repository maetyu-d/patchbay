# Architecture

## Current shape

The prototype is split into four layers:

1. `ModuleNode`
   - A plugin-style processing unit with typed audio and modulation ports.
2. `PatchGraph`
   - Owns nodes, patch cables, positions, and real-time rendering.
3. `PatchCanvas`
   - Visual editor for nodes and cables.
4. `MainComponent`
   - Hosts audio I/O and module-add controls.

## Why this shape

JUCE gives us a solid cross-platform app shell, audio device abstraction, and UI toolkit. The graph engine is kept separate from the app window so we can later reuse it inside:

- standalone app
- plugin shells
- headless render tools

## Format support reality

The user goal includes `VST3`, `AU`, `AUv3`, and `AAX`. In practice these split into two different concerns:

### 1. Host/runtime

The standalone app acts as the modular patching environment. That is the part implemented here.

### 2. Deployable plugin elements

Individual modules are now wrapped as plugin binaries for `VST3`, `AU`, and `Standalone`, using shared DSP cores. `AUv3` remains conditional on Xcode generation. This requires:

- a shared processor core for each module
- parameter/state serialization
- format-specific JUCE plugin targets
- `AAX` SDK availability
- `AUv3` app-extension packaging

## Near-term roadmap

1. Add graph serialization to JSON or `ValueTree`.
2. Add editable module parameters on each node.
3. Add polyphonic note flow and MIDI/event routing.
4. Build a module package format so app-native and plugin-native modules share metadata.
5. Add transport, tempo, automation, and latency compensation.
6. Add hosted third-party plugin loading and patch-node wrappers.

## Real-time caveat

The current prototype uses a lock during graph render and is intentionally simple. That is fine for an architectural starter, but a production DAW would need:

- lock-free or copy-on-write graph snapshots
- sample-accurate modulation/event scheduling
- proper cycle handling and feedback delay nodes
- latency reporting and compensation
- background-safe plugin scanning and instantiation
