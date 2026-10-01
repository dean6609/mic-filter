# Development

## Build on Windows x64

```powershell
.\bootstrap-toolchain.ps1
.\build.ps1 -RunTests
.\package.ps1 -RunTests
```

The toolchain is portable LLVM-MinGW, pinned in `compiler-source.json`. Bootstrap verifies the downloaded compiler before extraction. The build vendors RNNoise and the SpeexDSP resampler; end users do not need a compiler or network connection. Output goes to `dist/`; the shareable artifact is `MicFilter-Setup.exe`.

Use `-OutputDirectory dist-dev` with `build.ps1`, followed by `package.ps1 -DistributionDirectory dist-dev`, when an installed binary is in use.

## Source map

| File | Responsibility |
| --- | --- |
| `src/setup.cpp` | Single-file bootstrap, active endpoint enumeration, selection, elevation, protected extraction. |
| `setup-install.ps1` | Installation transaction, real capture probe, shortcuts, Settings > Apps entry, rollback, cleanup of older files. |
| `install.ps1` | `Install`/`Remove` for one microphone (registry association, backup, COM/APO registration, control file) and full `Uninstall`. |
| `installer-registry.ps1` | Shared version and IDs, minimum registry rights, exact effect-value updates, device restart, delete-or-schedule-at-restart. |
| `src/tray.cpp` | Tray controls, selected endpoint, diagnostics and command-line control. |
| `src/shared.h` | Stable 64-byte control/telemetry ABI (`state.bin`) and the options file (`options.bin`). |
| `src/apo.cpp` | COM aggregation, APO lifecycle, negotiation and audio callbacks. |
| `src/dsp.cpp` | Fixed-buffer adapter for unmodified RNNoise inference at 48 kHz, gate fades, dry/wet mix and the -20 dB floor held around speech. |
| `src/voice.cpp` | Voice presets after noise suppression: EQ, compressor, soft limiter, click-free preset changes. |
| `src/apo_sdk.h` | Project-written declarations of the public Windows APO interfaces. |
| `src/rate_processor.cpp` | Streaming conversion to/from 48 kHz, channel handling and bypass. |
| `src/audio_check.h` | WASAPI capture checks; counters and levels only. |
| `build.ps1`, `package.ps1` | Compile binaries and embed their payload into the installer. |
| `tests/` | Isolated DSP, resampling, COM, registry and package checks. |

Read [architecture](docs/architecture.md) before changing the audio path and [validation](docs/validation.md) before claiming device compatibility.

## Working rules

- Keep user-facing text and maintained documentation in English.
- Preserve bit-exact bypass and stored enable/disable behavior.
- Allocate models, resamplers and buffers before real-time processing. Do not add blocking work, file access or heap allocation to the callback.
- Do not replace unrelated manufacturer effects or change Windows security policy.
- Keep third-party sources and license notices intact.
- Tests must use private state, never the installed microphone's shared controls.
- Update the installer, payload list and tests together when changing runtime filenames.

For releases, run the build and package checks, verify installation on a real capture endpoint, and upload only `MicFilter-Setup.exe`. Keep the exact source revision tagged. Check the downloaded binary internally; no separate checksum asset is needed in the download list.
