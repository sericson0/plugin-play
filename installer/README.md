# Plugin Play installer

Builds a single Windows installer (`PluginPlay-<version>-Setup.exe`) for the
standalone app, with an optional in-flow VB-CABLE install.

## Prerequisites

- [Inno Setup 6.1+](https://jrsoftware.org/isdl.php) (needs `CreateDownloadPage`,
  added in 6.1.0).
- A **Release** build of the app (the installer bundles the statically-linked exe,
  so it runs on a clean PC without the Visual C++ redistributable).

## Build

```
cmake -B build
cmake --build build --config Release
ISCC.exe installer\PluginPlay.iss
```

`ISCC.exe` lives in `C:\Program Files (x86)\Inno Setup 6\` (all-users install) or
`%LOCALAPPDATA%\Programs\Inno Setup 6\` (per-user install).

The installer is written to `installer\Output\`.

If your Release exe is somewhere else, override the source dir:

```
ISCC.exe /DSourceDir="path\to\folder\with\Plugin Play.exe" installer\PluginPlay.iss
```

## What it does

- Installs `Plugin Play.exe` + `LICENSE.txt` + notices + README to
  `Program Files\Plugin Play`, with Start-menu (and optional desktop) shortcuts
  and an uninstaller.
- Shows the GPLv3 license page (required for a GPL distribution).
- Optional task **"Download & install VB-CABLE"**: fetches VB-Audio's own
  `VBCABLE_Driver_Pack` zip at install time (never re-hosted here), extracts it,
  and runs VB-Audio's elevated installer. A failed or declined download is
  non-fatal — the app can still install the cable later from its VIRTUAL CABLE
  button.
- Drops a `.show-welcome` marker so the app shows its first-run walkthrough once
  after every install/upgrade.
- On **uninstall**, runs `Plugin Play.exe --cleanup-redirects` (headless) first:
  if a crashed run left another app's audio routed into the virtual cable, its
  normal output is restored before the exe is removed.

## Code signing

The exe and the installer should be Authenticode-signed before release to avoid
SmartScreen warnings. Sign the app exe before compiling the installer, then sign
`installer\Output\PluginPlay-<version>-Setup.exe` afterwards with `signtool.exe`
(or wire `SignTool=` into `[Setup]`).
