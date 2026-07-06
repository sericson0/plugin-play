# Process-loopback capture experiment (M2)

**Question:** can Plugin Play capture a DJ app's audio *without any virtual-cable
driver*, and kill the dry signal at the speakers while the capture keeps flowing?

**Answer (2026-07-05, Win11, verified by ear): YES.**

| Check | Result |
|---|---|
| Capture a PID's audio via `AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK` | ✅ works, zero drivers |
| Kill dry signal with **session** mute (`ISimpleAudioVolume`) | ❌ capture goes silent too (tap is post-session-volume) |
| Kill dry signal with **endpoint master** mute (`IAudioEndpointVolume`) | ✅ capture unaffected (peaks 0.3489 vs 0.3439); speakers confirmed silent by ear |

**Consequence:** v1 primary input = process loopback by PID + master-mute the
captured endpoint; Plugin Play outputs to a *different* device (ASIO/WASAPI on
the DJ's interface, unaffected by Windows master mute). The DJ app must output
in shared mode (not ASIO / exclusive WASAPI — those bypass the engine).

## Files

- `loopback_test.cpp` — dual capture (A: process loopback on target PID,
  B: endpoint loopback, informational) across three phases
  (unmuted → muted → unmuted), prints peaks and a verdict.
  This capture client is the seed of Plugin Play's input stage.
- `player.ps1` — child process looping a 440 Hz tone via `SoundPlayer`.
- `run_test.ps1` — orchestrates: start player → run test on its PID → cleanup.
  `-MuteLever session` reproduces the failed session-mute variant.
- `build.ps1` — cl.exe build (VS 2022 BuildTools, x64).

## Run

```powershell
./build.ps1
./run_test.ps1            # listen: tone → silence → tone = PASS
```

Known quirk: the endpoint-loopback B channel reads 0 frames while `SoundPlayer`
audio is clearly rendering — don't trust it as an audibility check; use ears.

## ASIO output probe (`asio_probe.cpp`)

Validates the OUTPUT side: can Plugin Play drive an external DAC over ASIO?
Built against JUCE's bundled ASIO SDK headers (no Steinberg download — confirms
`JUCE_ASIO=1` is self-sufficient).

```powershell
./build_asio.ps1
./asio_probe.exe --auto 4                 # find any live ASIO device, stream 4s tone
./asio_probe.exe "Universal Audio USB" 4  # target a specific driver by name substring
./asio_probe.exe --auto 6 --capture <pid> # ASIO out + process-loopback capture together
```

Status 2026-07-05: **PASSED LIVE** on the Apollo Twin X USB (Universal Audio USB
v1330, 24in/10out, 48 kHz, 256-sample buffer, 18.17 ms output latency, Int32LSB).
Tone streamed cleanly; `--capture` coexistence run captured 240k frames = exactly
5 s @ 48 kHz with zero dropouts *while* ASIO output was live. The full driverless
signal path (process-loopback capture → ASIO out to external DAC) is validated
end to end in one process.

Note: the real device is **Universal Audio USB** (Apollo Twin X USB); the
"Universal Audio Thunderbolt" driver + WDM endpoint are phantom leftovers. The
interface must be connected (PnP OK) for the ASIO driver to load; UAHelperService
being "stopped" did not block it once the USB device enumerated.

## Input-source enumerator (`list_sources.cpp`, req #1)

Lists every process with an audio session on the default render endpoint — the
data behind Plugin Play's "pick your DJ app" UI. Prints state / PID / exe / name;
the chosen PID goes to process-loopback capture.

```powershell
./build_extras.ps1
./list_sources.exe        # run with a DJ app open — it shows up as ACTIVE
```
Validated 2026-07-05: correctly listed the running **virtualdj.exe** as ACTIVE.

## Sound-quality measurement (`quality_test.cpp`, req #3)

Renders a precise 1 kHz tone through **WASAPI shared** (the path DJ audio now
takes), process-loopback-captures it, and measures fundamental level + THD+N
(Goertzel). Two trials: matched sample rate vs 44.1→48 resample.

```powershell
./quality_test.exe            # measure on the default endpoint
./quality_test.exe list       # list active render endpoints
./quality_test.exe ep=Apollo  # target a specific endpoint by name substring
./quality_test.exe raw        # bypass APO / audio enhancements (if endpoint allows)
```

Status 2026-07-05 (INTERIM — clean test deferred): **THD −97 dB in all cases →
the shared path adds no harmonic distortion**; float engine → no bit-depth loss;
resample cost <1 dB. But the only active endpoint was **SoundWire (virtual network
streamer)**, which added +4.75 dB gain + noise (its own DSP; peak sample 0.866 vs
expected 0.5; rejected RAW). Re-run `ep=<clean hardware>` for the definitive
number. Product rules: capture from a clean endpoint (no enhancements), keep the
DJ app's **session** volume at 100% (it scales the capture; endpoint master mute
does not), match sample rates end-to-end.
