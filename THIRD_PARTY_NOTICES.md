# Third-party notices

Plugin Play is distributed under the GNU General Public License v3.0 (see
[LICENSE](LICENSE)). It builds on the following third-party components, whose
licenses are reproduced or linked below.

## JUCE

JUCE (https://juce.com) is used under the terms of the **GNU General Public
License v3.0**. JUCE is dual-licensed (GPLv3 **or** a commercial JUCE licence);
because Plugin Play is itself released under the GPLv3 with complete source
available, it uses JUCE under the GPLv3 grant. No commercial JUCE licence is
required for this configuration.

- Copyright © Raw Material Software Limited.
- Fetched at build time via CMake `FetchContent` (tag `8.0.12`).
- License text: `build/_deps/juce-src/LICENSE.md` after configuring, or
  https://github.com/juce-framework/JUCE/blob/master/LICENSE.md

> Note: if Plugin Play is ever distributed as **closed source**, this GPLv3 grant
> no longer applies and a paid JUCE licence (and a Steinberg VST3 agreement, below)
> must be obtained instead.

## Steinberg VST3 SDK

VST3 plugin hosting uses the Steinberg VST3 SDK, which ships inside JUCE's
`juce_audio_processors` module. The VST3 SDK is dual-licensed under the **GPLv3**
or Steinberg's proprietary VST3 licence. Under Plugin Play's GPLv3 distribution
the GPLv3 grant applies; a closed-source build would require a signed Steinberg
VST3 licensing agreement.

- "VST" is a trademark of Steinberg Media Technologies GmbH.
- https://www.steinberg.net/developers/

## VB-CABLE (VB-Audio Virtual Cable)

Plugin Play does **not** bundle or redistribute VB-CABLE. The optional virtual
cable is installed by fetching VB-Audio's own unmodified installer from
https://vb-audio.com/Cable/ at the user's request (in-app, or during the
Plugin Play installer). VB-CABLE is donationware © VB-Audio Software;
"VB-CABLE" and "VB-Audio" are trademarks of VB-Audio Software. Please support
them at https://shop.vb-audio.com/.

## ASIO

ASIO is a trademark and software of Steinberg Media Technologies GmbH. Plugin
Play builds ASIO support using the ASIO interface headers bundled with JUCE; no
Steinberg ASIO SDK download is redistributed here.
