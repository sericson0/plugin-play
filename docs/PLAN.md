# Plugin Play — Plan

A free/donation Windows tool that lets DJs route their software's output through a
chain of VST3 plugins and out to any WASAPI or ASIO device.

```
DJ software (Traktor / VirtualDJ / Mixxx / any)
        │  plays to a virtual audio device
        ▼
Virtual cable  (VB-CABLE or Virtual Audio Driver)
        │  captured as Plugin Play's INPUT device
        ▼
Plugin Play  —  VST3 chain: [Plugin 1] → [Plugin 2] → … (reorder, bypass, open GUIs)
        │
        ▼
Real sound card via WASAPI (shared/exclusive) or ASIO
```

## Decisions (2026-07-05)

| Topic | Decision |
|---|---|
| Stack | C++17 + JUCE 8.0.12 (CMake FetchContent), same toolchain as hisstory |
| App type | Standalone JUCE GUI app hosting VST3s via `AudioProcessorGraph` |
| Outputs | WASAPI + ASIO from day one. JUCE 8 bundles its own `iasiodrv.h`, so **no Steinberg SDK download is needed** — `JUCE_ASIO=1` just works |
| Input | Any input device the user picks. Primary workflow: virtual cable |
| Virtual cable | Support **VB-CABLE** (works today, but its license forbids bundling in our installer without a VB-Audio agreement) and **[Virtual Audio Driver](https://github.com/VirtualDrivers/Virtual-Audio-Driver)** (MIT-licensed, signed releases → *can* be bundled for the "single simple download" goal; must verify signing works without test mode on stock Win11) |
| DJ software targets | Traktor Pro, VirtualDJ, Mixxx — but generic (any app that can output to a chosen device) |
| V1 features | Serial VST3 chain, plugin GUIs, add/remove, drag-reorder, per-plugin bypass, in/out level meters, session persistence (devices + chain + plugin states) |
| Theme | hisstory palette: dark navy `#12151f`, orange accent `#D96C30`, golden `#F3A10F` |

## Milestones

- **M1 — Core app (this pass):** audio engine (device manager + graph + serial chain),
  VST3 scan, chain UI with drag-reorder/bypass/meters, plugin GUI windows,
  session persistence. Builds and runs.
- **M2 — Validation:** end-to-end test with Traktor/VirtualDJ/Mixxx over VB-CABLE;
  install Virtual Audio Driver on a stock Win11 machine and confirm the signed
  release loads *without* test-signing mode; measure round-trip latency.
- **M3 — Polish:** first-run setup helper (detect/install virtual cable, guide DJ-software
  output selection), crash-safe out-of-process plugin scanning, named presets,
  limiter/safety option on output.
- **M4 — Distribution:** Inno Setup installer bundling Virtual Audio Driver (MIT) with
  optional VB-CABLE download link; code-sign the app; donation link.

## Architecture

- `Source/Audio/AudioEngine` — owns `AudioDeviceManager`, `AudioProcessorGraph`
  (I/O nodes + one node per plugin), `AudioProcessorPlayer`, a metering wrapper
  callback, chain operations (add/remove/move/bypass), and session save/restore
  (`%APPDATA%/PluginPlay/session.xml`; plugin states stored base64).
- `Source/Plugins/PluginScanner` — `KnownPluginList` + background
  `PluginDirectoryScanner` over the default VST3 dirs, dead-man's-pedal blacklist,
  persisted in the app properties file.
- `Source/UI/*` — `MainComponent` (header bar, meters, status bar),
  `ChainView` (slot cards, drag-reorder), `PluginWindow` (native-title-bar window
  wrapping each plugin's editor).
- `Source/Theme.*` — palette + `LookAndFeel` ported from hisstory.

## Risks / open questions

- **Virtual Audio Driver signing** — README still mentions test-signing in places;
  if the signed release doesn't load cleanly on stock Win11, fall back to
  "download VB-CABLE" flow in the installer (still simple, one extra click).
- Does Virtual Audio Driver's speaker endpoint expose a matching capture path
  (loopback/mic mirror) that JUCE can open as an input device? Verify in M2;
  VB-CABLE definitely does (its "CABLE Output" capture device).
- In-process VST3 scanning can crash on broken plugins (mitigated by the
  dead-man's-pedal; proper out-of-process scan is M3).
- Exclusive-mode WASAPI input on the cable while the DJ app writes to it —
  use shared mode on the input side by default.
