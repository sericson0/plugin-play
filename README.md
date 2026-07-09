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
- **Send an app through Plugin Play** — pick a running app (e.g. Spotify, VirtualDJ)
  and Plugin Play routes just that app's audio into the virtual cable for you, no
  Windows sound-settings fiddling required

## Building

Requires Visual Studio 2022 and CMake ≥ 3.22. JUCE is fetched automatically.

```
cmake -B build
cmake --build build --config Release
```

The exe lands in `build/PluginPlay_artefacts/Release/`.

## Routing setup

Plugin Play needs a virtual audio cable to receive your app's audio. Click
**VIRTUAL CABLE** in the app (or accept the step in the installer) to download and
install [VB-CABLE](https://vb-audio.com/Cable/) — it's a guided one-click install.

Then either:

- **Send an app through Plugin Play** *(easiest)* — set the INPUT dropdown to your
  app (e.g. "Spotify"). Plugin Play routes that app into the cable automatically and
  reads it back; your speakers stay selected as the OUTPUT.
- **Manual cable routing** — in your DJ software set the master output to the cable
  device (e.g. "CABLE Input"), then in Plugin Play set INPUT to the cable's capture
  side (e.g. "CABLE Output") and OUTPUT to your real sound card.

> The open-source *Virtual Audio Driver* was evaluated and ruled out for now: its
> release isn't Microsoft attestation-signed (error 52 on stock Windows 11). See
> [docs/PLAN.md](docs/PLAN.md).

See [docs/PLAN.md](docs/PLAN.md) for the roadmap.

## License

Plugin Play is free software, released under the **GNU General Public License v3.0**
— see [LICENSE](LICENSE). It uses JUCE and the Steinberg VST3 SDK under their GPLv3
grants, and installs VB-CABLE by fetching VB-Audio's own installer (never
redistributed). Full attributions are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
