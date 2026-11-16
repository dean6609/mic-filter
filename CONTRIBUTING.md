# Development

## Build on Windows x64

```powershell
.\bootstrap-toolchain.ps1
.\build.ps1 -RunTests
.\package.ps1 -RunTests
```

The toolchain is portable LLVM-MinGW, pinned in `compiler-source.json`. Bootstrap verifies the downloaded compiler before extraction. The build vendors RNNoise and the SpeexDSP resampler; end users do not need a compiler or network connection. Output goes to `dist/`; the shareable artifact is `MicFilter-Setup.exe`.

Use `-OutputDirectory dist-dev` with `build.ps1`, followed by `package.ps1 -DistributionDirectory dist-dev`, when an installed binary is in use.

## Build on Linux or macOS

```sh
./build.sh --test       # terminal app and engine tests in dist/
./build.sh --package    # also the archive users download
```

Only a C/C++ compiler is needed (`cc`/`c++`, or set `CC`/`CXX`); there is nothing to install or download. The app loads the system's PulseAudio client library (Linux) or Core Audio (macOS) when it runs.

## Automated checks on pull requests

`.github/workflows/ci.yml` builds and tests one job per system (Windows, Linux, macOS). Keep it short: documentation-only changes skip it, a new push cancels the run in progress, every job has a time limit and the Windows compiler is cached. Add a job or a matrix entry only when it checks something the three existing ones cannot. `release.yml` attaches the Linux and macOS archives when a release is published.

## Source map

| File | Responsibility |
| --- | --- |
| `src/setup.cpp`, `src/setup_selection.h` | Single-file bootstrap, colored console checklist, multiple-input selection, elevation, protected extraction. |
| `setup-install.ps1` | Batch installation transaction, per-input capture probes, shortcuts, Settings > Apps entry, rollback, cleanup of older files. |
| `install.ps1` | `Install`/`Remove` for one microphone (registry association, backup, COM/APO registration, control file) and full `Uninstall`. |
| `installer-registry.ps1` | Shared version and IDs, minimum registry rights, exact effect-value updates, device restart, delete-or-schedule-at-restart. |
| `src/tray.cpp` | Tray controls, selected endpoint, diagnostics and command-line control. |
| `src/tray_menu.h` | Compact native tray submenus and isolated menu checks. |
| `src/shared.h` | Stable 64-byte control/telemetry ABI (`state.bin`) and the options file (`options.bin`). |
| `src/apo.cpp` | COM aggregation, APO lifecycle, negotiation and audio callbacks. |
| `src/settings.h`, `src/platform.h`, `src/version.h` | Portable pieces shared by every system: per-callback controls, memory locking, denormal protection, version text. |
| `src/dsp.cpp` | Fixed-buffer adapter for unmodified RNNoise inference at 48 kHz, gate fades, dry/wet mix and the -20 dB floor held around speech. |
| `src/voice.cpp` | Voice presets after noise suppression: EQ, compressor, soft limiter, click-free preset changes. |
| `src/apo_sdk.h` | Project-written declarations of the public Windows APO interfaces. |
| `src/rate_processor.cpp` | Streaming conversion to/from 48 kHz, channel handling and bypass. |
| `src/audio_check.h` | WASAPI capture checks; counters and levels only. |
| `src/cli.cpp` | Linux/macOS terminal app: microphone picker, live view, keys, menu shortcut, self-test. |
| `src/cli_engine.h` | Microphone → engine → virtual microphone at 48 kHz mono through a lock-free ring. |
| `src/cli_virtual_mic.h` | Linux: creates/removes the PipeWire/PulseAudio virtual microphone. macOS: finds BlackHole. |
| `src/cli_terminal.h`, `src/cli_config.h` | Colors, keys, meters and the framed title; remembered choices. |
| `src/audio_backend.h`, `src/audio_backend.c` | The single place that configures and compiles miniaudio. |
| `build.ps1`, `package.ps1` | Compile Windows binaries and embed their payload into the installer. |
| `build.sh`, `packaging/` | Compile, test and package the Linux/macOS terminal app. |
| `tests/` | Isolated DSP, resampling, COM, registry and package checks. `dsp_tests.cpp` is portable and runs on all three systems; `tests.cpp` adds the Windows effect checks. |
| `assets/micfilter.ico`, `scripts/generate-icon.py` | Embedded multi-size icon and deterministic generator (Python standard library). |

Read [architecture](docs/architecture.md) before changing the audio path and [validation](docs/validation.md) before claiming device compatibility.

Work on a branch from current `main`, run the isolated build and package checks, then open a pull request documenting the results before merging. Physical capture and listening remain required before publishing a release.

## Working rules

- Keep user-facing text and maintained documentation in English.
- Preserve bit-exact bypass and stored enable/disable behavior. Opening the tray never enables the filter; exiting disables it.
- Keep the 64-byte shared-state ABI compatible across upgrades.
- Register only `IAudioProcessingObject` in `NumAPOInterfaces` and keep proper COM aggregation.
- Keep `dsp`, `voice` and `rate_processor` free of system headers; system-specific code goes in `platform.h` or the files of that system.
- Allocate models, resamplers and buffers before real-time processing. Do not add blocking work, file access or heap allocation to the callback.
- Do not replace unrelated manufacturer effects or change Windows security policy.
- Keep third-party sources and license notices intact; `SOURCE.md` in each vendor folder records what is included.
- Do not commit local logs, recordings, diagnostic scripts or device identifiers.
- Tests must use private state, never the installed microphone's shared controls.
- Update the installer, payload list and tests together when changing runtime filenames.

For releases, run the build and package checks, verify installation on a real capture endpoint, and upload only `MicFilter-Setup.exe`; publishing the release adds the Linux and macOS archives. Keep the exact source revision tagged. Check the downloaded binary internally; no separate checksum asset is needed in the download list.
