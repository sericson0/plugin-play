# Plugin Play

Route your DJ software through VST3 effects, out to any WASAPI or ASIO device.

DJ software (Traktor, VirtualDJ, Mixxx, …) → virtual audio cable → **Plugin Play**
(VST3 chain) → your sound card.

## Features (v1)

- Serial VST3 plugin chain: add, remove, drag to reorder, per-plugin bypass
- Open every plugin's own GUI
- WASAPI (shared/exclusive) and ASIO output
- Input/output level meters
- Session persistence — chain, plugin settings, and audio devices restore on launch

## Building

Requires Visual Studio 2022 and CMake ≥ 3.22. JUCE is fetched automatically.

```
cmake -B build
cmake --build build --config Release
```

The exe lands in `build/PluginPlay_artefacts/Release/`.

## Routing setup

1. Install a virtual cable: [VB-CABLE](https://vb-audio.com/Cable/) or
   [Virtual Audio Driver](https://github.com/VirtualDrivers/Virtual-Audio-Driver).
2. In your DJ software, set the master output to the cable device
   (e.g. "CABLE Input").
3. In Plugin Play → Audio Settings: input = the cable's capture side
   (e.g. "CABLE Output"), output = your real sound card.

See [docs/PLAN.md](docs/PLAN.md) for the roadmap.
