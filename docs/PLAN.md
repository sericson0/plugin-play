# Plugin Play — Plan

A free/donation Windows tool that lets DJs route their software's output through a
chain of VST3 plugins and out to any WASAPI or ASIO device.

```
PRIMARY (driverless, validated 2026-07-05 — see experiments/process-loopback/):

DJ software (Traktor / VirtualDJ / Mixxx / any, output = default device, SHARED mode)
        │  captured by PID via process loopback (zero drivers, Win10 2004+)
        │  dry signal killed by master-muting that endpoint (capture unaffected)
        ▼
Plugin Play  —  VST3 chain: [Plugin 1] → [Plugin 2] → … (reorder, bypass, open GUIs)
        │
        ▼
Different real device via WASAPI or ASIO (unaffected by Windows master mute)

FALLBACK (odd setups / app uses ASIO or exclusive WASAPI):

DJ software → VB-CABLE (user-installed, guided) → Plugin Play input → out
```

## Decisions (2026-07-05)

| Topic | Decision |
|---|---|
| Stack | C++17 + JUCE 8.0.12 (CMake FetchContent), same toolchain as hisstory |
| App type | Standalone JUCE GUI app hosting VST3s via `AudioProcessorGraph` |
| Outputs | WASAPI + ASIO from day one. JUCE 8 bundles its own `iasiodrv.h`, so **no Steinberg SDK download is needed** — `JUCE_ASIO=1` just works |
| Input / routing | **Support BOTH (decided 2026-07-05).** *Default — driverless process-loopback capture* (`AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK`, Win10 2004+): user picks the DJ app inside Plugin Play (source enumerator lists running audio apps incl. VirtualDJ), we capture by PID and master-mute the endpoint to kill the dry signal. No install. *Secondary — virtual-cable mode*: for the familiar "select Plugin Play as the output device in the DJ software" workflow; needs a virtual cable present. Plugin Play cannot itself be a selectable device without a driver — that's what the cable path provides |
| Virtual cable | For the virtual-device workflow. **VB-CABLE** via guided user install (license forbids bundling without a VB-Audio agreement). **Virtual Audio Driver ruled out for now (2026-07-05):** its release is SignPath-signed, not Microsoft attestation-signed — error 52 on stock Win11 unless Secure Boot off + test signing (maintainer: "stuck in the attestation phase at Microsoft"); also its speaker/mic are not internally paired. Re-check attestation status later |
| Sound quality | Shared-WASAPI path adds **no harmonic distortion** (measured THD −97 dB) and no bit-depth loss (float engine); only real lever is sample-rate conversion (<1 dB), avoided by matching rates. Rules: capture from a **clean endpoint** (no "Audio Enhancements"), keep the DJ app's **session volume at 100%** (session volume scales the capture; endpoint master mute does not), match sample rates end-to-end. Clean-hardware confirmation still pending (test endpoint was the colored SoundWire virtual device) |
| DJ software targets | Traktor Pro, VirtualDJ, Mixxx — but generic (any app that can output to a chosen device) |
| V1 features | Serial VST3 chain, plugin GUIs, add/remove, drag-reorder, per-plugin bypass, in/out level meters, session persistence (devices + chain + plugin states) |
| Theme | hisstory palette: dark navy `#12151f`, orange accent `#D96C30`, golden `#F3A10F` |

## Milestones

- **M1 — Core app (this pass):** audio engine (device manager + graph + serial chain),
  VST3 scan, chain UI with drag-reorder/bypass/meters, plugin GUI windows,
  session persistence. Builds and runs.
- **M2 — Validation:** ✅ capture path validated 2026-07-05 (process loopback works;
  session mute kills capture, endpoint master mute does not — by-ear confirmed;
  Virtual Audio Driver rejected: not attestation-signed, error 52 on stock Win11).
  ✅ ASIO **output** validated LIVE on Apollo Twin X USB (Universal Audio USB v1330,
  48kHz, 256 buffer, 18.17 ms output latency, Int32LSB) — tone streamed cleanly.
  ✅ **Coexistence proven**: one process ran ASIO output to the Apollo + process-
  loopback capture together, 240k frames = 5 s @ 48 kHz with zero dropouts. The
  entire driverless signal path is validated end to end.
  (experiments/process-loopback/asio_probe.cpp, built on JUCE's bundled ASIO SDK —
  no Steinberg download.)
  Remaining: port capture + ASIO output into the app as real nodes, end-to-end with
  Mixxx/VirtualDJ in shared-mode output, measure full round-trip latency.
- **M3 — Polish:** first-run setup helper (detect/install virtual cable, guide DJ-software
  output selection), crash-safe out-of-process plugin scanning, named presets,
  limiter/safety option on output.
- **M4 — Distribution:** Inno Setup installer — **no driver bundled** (driverless
  primary path); VB-CABLE guided-download link for the fallback; code-sign the app;
  donation link.

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

- **Process loopback can't see ASIO or exclusive-WASAPI output** — the DJ app must
  render through the shared Windows engine. Traktor/VirtualDJ/Mixxx all support
  this, but first-run helper must guide the setting (and detect silence → hint).
- **DJ app and Plugin Play must use different output devices** — the captured
  endpoint gets master-muted, so Plugin Play's own output (ASIO/WASAPI on the
  DJ's interface) must be elsewhere. Single-device laptops: fallback to VB-CABLE.
- Master mute silences ALL system audio on that endpoint (system dings too) —
  arguably a feature mid-set; must restore mute state on exit/crash (RAII +
  startup sanity check).
- JUCE `AudioDeviceManager` can't do process-loopback capture — the experiment's
  capture client (float32/48k, event-driven) gets ported as a custom input node
  feeding the graph, clock-decoupled from the output device via a resampling
  ring buffer (two independent clocks!).
- Windows engine adds ~10–20 ms on the capture side; measure in M2 remainder.
- In-process VST3 scanning can crash on broken plugins (mitigated by the
  dead-man's-pedal; proper out-of-process scan is M3).
- Virtual Audio Driver: re-check Microsoft attestation status before M4
  (github.com/VirtualDrivers/Virtual-Audio-Driver issue #15); sponsoring their
  attestation fee is an option if the driverless path hits trouble.
