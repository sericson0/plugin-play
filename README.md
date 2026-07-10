# Plugin Play

Run your DJ software — or any app — through a chain of VST3 effects, live.

DJ software (Traktor, VirtualDJ, Mixxx, …) or any audio app → **Plugin Play**
(your VST3 effect chain) → your speakers or audio interface.

Free and open source (GPLv3), supported by donations.

## Download & install

Grab the latest `PluginPlay-<version>-Setup.exe` from the releases page and run it.
The installer:

- installs Plugin Play (no other runtimes needed — everything is built in),
- optionally downloads and runs VB-Audio's own **VB-CABLE** installer, the virtual
  audio cable Plugin Play uses to route apps (recommended; needs a reboot after),
- opens a short first-run walkthrough in the app.

> **"Windows protected your PC"?** The installer isn't code-signed yet, so
> SmartScreen may warn on first run. Click **More info → Run anyway**. The source
> is public, so you can also build it yourself (below).

**Requirements:** 64-bit Windows 10 (May 2020 Update) or later. Your effects must
be **VST3** format.

## Quick start

1. **SCAN PLUGINS** — finds the VST3 effects installed on your PC (runs
   automatically on first launch).
2. Set your input, either way:
   - **Send an app through Plugin Play** *(easiest)* — open the INPUT dropdown and
     pick a running app (e.g. Spotify or VirtualDJ). Plugin Play routes that app's
     audio through the virtual cable for you — no Windows settings to touch. Your
     OUTPUT stays on your normal speakers.
   - **Manual cable routing** — in your DJ software set the master output to the
     cable device (e.g. "CABLE Input"), then set Plugin Play's INPUT to the cable's
     capture side (e.g. "CABLE Output") and OUTPUT to your sound card.
3. **+ Add Plugin** — build your chain, drag to reorder, open each plugin's own GUI.
4. Play. The built-in safety **LIMITER** guards your speakers; **FX ON/OFF** is the
   master kill switch.

The **HELP** button in the app covers all of this in more depth, and **GUIDE**
replays the first-run walkthrough.

## Building from source

Requires Visual Studio 2022 and CMake ≥ 3.22. JUCE is fetched automatically.

```
cmake -B build
cmake --build build --config Release
```

The exe lands in `build/PluginPlay_artefacts/Release/`. To build the installer,
compile `installer/PluginPlay.iss` with Inno Setup 6.1+ (see
[installer/README.md](installer/README.md)).

See [docs/PLAN.md](docs/PLAN.md) for architecture notes and the roadmap.

## Support

Questions, bugs, ideas: **TangoToolkit@gmail.com**. If Plugin Play helps your sets,
the DONATE button in the app takes tips (card / Apple Pay, Venmo, Zelle).

## License

Plugin Play is free software, released under the **GNU General Public License v3.0**
— see [LICENSE](LICENSE). It uses JUCE and the Steinberg VST3 SDK under their GPLv3
grants, and installs VB-CABLE by fetching VB-Audio's own installer (never
redistributed). Full attributions are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
