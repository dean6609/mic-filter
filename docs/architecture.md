# Architecture

## Runtime

```mermaid
flowchart LR
  M[Selected microphone] --> W[Windows shared capture engine]
  W --> A[MicFilter APO]
  A --> R[SpeexDSP conversion to 48 kHz]
  R --> N[RNNoise]
  N --> B[Conversion to endpoint rate]
  B --> C[Recording or communication app]
  T[Tray or CLI controls] -. Shared state .-> A
```

The tray is a controller. Windows loads the DLL in its audio host when a capture application uses the configured endpoint. Enabled/disabled controls live in a shared file and survive reboots. Bypass copies the original samples without inference or resampling. At 48 kHz the adapter bypasses both resamplers.

The engine supports the Windows float32 processing format, 1–8 channels and 8–192 kHz. RNNoise always receives 480-sample frames at 48 kHz. Windows callback sizes can vary. Resamplers, network state, queues and scratch buffers are initialized before processing; the callback uses fixed storage and interlocked telemetry.

## Processing details

RNNoise output is 20 ms behind its input (overlap/add plus one delayed frame); with the 10 ms frame buffer the filtered path is 1440 samples late at 48 kHz. The original (dry) signal uses the same delay so partial mixes do not comb-filter. The optional voice gate fades in over 5 ms and out over 50 ms. Around speech a -20 dB share of the original signal (`kFloorGain` in `src/dsp.h`) is mixed in so word tails never drop into digital silence. It is held while RNNoise's voice probability reaches 0.6 and for 300 ms after, then fades out over 400 ms, so pauses get RNNoise's full noise reduction. The floor only adds the original signal; the voice itself always comes from RNNoise, so its fade cannot cut a word. Reported APO latency is 30 ms plus any resampler delay.

## Installation and upgrades

The installer embeds the application, APO, scripts, quick-start instructions and licenses. It enumerates only `DEVICE_STATE_ACTIVE` capture endpoints, selects the only input automatically or asks for a number, checks the format, elevates, extracts into a protected temporary folder and starts the installation transaction.

Registration uses one APO processing interface: `IAudioProcessingObject`. RT, configuration and system-effect interfaces remain available through COM, but are not entries in `NumAPOInterfaces`. The factory supports aggregation with a nondelegating inner IUnknown.

Only the chosen endpoint's effect value is changed. A backup preserves its previous value. Unrelated APO slots cause installation to stop. The audio host and interactive users can write controls; executable directories stay protected. A uniquely matching USB media function can be restarted to rebuild the chain. Other devices may require reconnection or a reboot.

Capture verification checks actual received frames and growth of APO callback/filtered-frame counters. Capture failure triggers restoration. Capture without observed processing is reported as pending confirmation. No audio recordings are saved.

MicFilter 0.4 uses a new COM class identity to prevent cached pre-rename classes from hiding the new DLL. It recognizes the prior two class IDs during upgrade and preserves settings and the original restore backup. Runtime binaries use `%ProgramFiles%\MicFilter`. Fresh installations use `%ProgramData%\MicFilter`; an existing `%ProgramData%\WavoFilter\state.bin` is retained for compatibility if the new state file does not exist. This legacy name is storage compatibility, not microphone selection. Legacy tray windows, startup entries and the old desktop shortcut are handled during upgrade.

Each installed APO is a hash-named copy (`MicFilterAPO-<hash>.dll`) so audiodg.exe never has a loaded DLL overwritten. After a confirmed install, setup deletes the other copies, along with the WavoFilter program folder and class registrations once no endpoint references them. A file Windows audio still holds is queued with `MoveFileEx(MOVEFILE_DELAY_UNTIL_REBOOT)`.

## Uninstall

Setup registers `HKLM\...\Uninstall\MicFilter`, whose `UninstallString` is `MicFilter.exe --uninstall`. That command, or **Uninstall MicFilter** in the tray menu, starts `install.ps1 -Action Uninstall` elevated and exits. The script waits for that process, closes any tray instance, then:

1. restores the effect slot on every capture endpoint that holds a MicFilter or WavoFilter class, using the backup for the configured endpoint and clearing the slot elsewhere. Disconnected endpoints keep their registry keys, so they are restored too;
2. removes the COM/APO registrations (current and legacy) and the Settings > Apps entry, and restarts the matching USB device;
3. removes shortcuts, startup entries and the program and data folders (current and WavoFilter), queuing held files for deletion at restart.

`MicFilter.exe` removes per-user data (startup entry, `%LOCALAPPDATA%\MicFilter`), because the elevated script may run as a different administrator. `MicFilter-Setup.exe --uninstall` runs the same script from the installer payload if the program folder is damaged. **Remove effect and restore configuration** only detaches one microphone and keeps the application installed.

## Shared-state ABI

Keep the magic and version fields compatible. Total size: **64 bytes**.

| Offset | Type | Field |
| ---: | --- | --- |
| 0 | int32 | Magic |
| 4 | int32 | ABI version |
| 8 | int32 | Enabled |
| 12 | int32 | Voice probability threshold, per mille |
| 16 | int32 | Gate grace blocks |
| 20 | int32 | Wet mix, per mille |
| 24 | int32 | Channels |
| 28 | int32 | Endpoint sample rate |
| 32 | int32 | Processing supported |
| 36 | int32 | Producing process ID |
| 40 | int64 | APO callbacks |
| 48 | int64 | Filtered endpoint frames |
| 56 | int64 | Last callback tick |

The producer ID and recent monotonic tick distinguish current callbacks from old data. Tests replace ProgramData in their own process and create a private fixture; they must never write installed telemetry.

## Engine provenance

- Werman v1.21: `4b0a6f76fc8bcfb5a3603ae2cb6619dfcb0e19f2`.
- Bundled RNNoise: `70f1d256acd4b34a572f999a05c87bf00b67730d`.
- SpeexDSP 1.2.1: `1b28a0f61bc31162979e1f26f3981fc3637095c8`, standalone float resampler with `RANDOM_PREFIX=micfilter`.
- APO interface declarations are written for this project in `src/apo_sdk.h` from Microsoft's public API reference; no Windows SDK headers are redistributed. Compile-time checks pin the struct layouts.
- `vendor/rnnoise` is the upstream RNNoise tree at the revision above. The unused alternative model `rnnoise_data_little.c` is omitted.

The network and weights are unchanged. No personal training or automatic model download is implemented. Engine updates should pin a reviewed revision and repeat equivalence, streaming and physical voice checks.
