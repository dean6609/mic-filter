<div align="center">

![MicFilter — native microphone noise suppression for Windows](assets/readme-banner.svg)

[![Download for Windows](https://img.shields.io/badge/Download-Windows_x64-14b8a6?style=for-the-badge&logo=windows)](https://github.com/dean6609/mic-filter/releases/latest/download/MicFilter-Setup.exe)
[![Release](https://img.shields.io/github/v/release/dean6609/mic-filter?style=for-the-badge&color=334155)](https://github.com/dean6609/mic-filter/releases/latest)
[![License](https://img.shields.io/badge/License-GPL--3.0-334155?style=for-the-badge)](LICENSE)

**Cleaner microphone audio. One installer. Native Windows integration.**

[Install](#install-in-a-minute) · [Controls](#your-filter-your-controls) · [Uninstall](#uninstall) · [Compatibility](#compatibility) · [Build from source](CONTRIBUTING.md)

</div>

## What you get

| | |
| --- | --- |
| **Native filtering** | RNNoise runs in the Windows microphone effects pipeline. |
| **Any microphone brand** | Choose from connected, enabled inputs. A single input is selected automatically. |
| **One `.exe`** | The model and all runtime components are included. Installation works offline. |
| **Persistent settings** | The filter keeps its enabled or disabled state after reboot. |
| **Lightweight controls** | A tray icon controls the filter; it does not need to stay open for processing. |

## Install in a minute

1. **[Download MicFilter-Setup.exe](https://github.com/dean6609/mic-filter/releases/latest/download/MicFilter-Setup.exe)** and double-click it.
2. Accept the Windows administrator prompt. Choose a microphone if more than one is available.
3. Follow the console progress. The installer checks audio capture and adds **MicFilter** to your desktop and Start menu.

The first installation enables the filter. Open the desktop shortcut whenever you want to show its tray controls. There is no setup wizard, location picker or separate dependency download.

## Your filter, your controls

| Action | Result |
| --- | --- |
| Open MicFilter | Show the tray icon and keep the current setting. |
| Click the icon | Enable or disable filtering. |
| **Exit (leave original audio)** | Disable the filter and close the app. |
| Restart Windows | Keep the last enabled/disabled setting. |
| Leave the app closed | Windows continues using the saved filter setting. |

Right-click for diagnostics, voice-detection profiles and dry/wet mix. The default uses RNNoise without an extra silence gate to reduce avoidable word cutoffs. **Filter confirmed** means the installed effect has recent processing activity.

One microphone is managed per installation. To switch, choose **Remove effect and restore configuration**, then run setup again.

## Uninstall

Open **Settings → Apps → Installed apps → MicFilter → Uninstall**, or right-click the tray icon and choose **Uninstall MicFilter**. Microphones return to their previous configuration, and MicFilter's files, shortcuts, startup entry and settings are deleted. A file still held by Windows audio is deleted at the next restart.

If the program folder is damaged, run `MicFilter-Setup.exe --uninstall`.

## Compatibility

**Windows x64 · 1–8 channels · 8–192 kHz · Any microphone brand**

MicFilter uses Windows shared capture and Audio Processing Objects (APOs). It resamples internally for RNNoise without changing your device format. Manufacturer effect chains can conflict with this integration, and RAW/exclusive-mode apps can bypass system effects. Setup checks the capture route and preserves unrelated effects. See [validation and device limitations](docs/validation.md).

The installer is currently unsigned, so Windows SmartScreen may show **Windows protected your PC**; choose **More info → Run anyway**. Installation never asks you to disable security protections. No voice recordings are saved during its checks.

If a driver or Windows update resets the microphone effects, the tray shows the effect as not installed. Run the setup again; your settings are kept.

## Explore the project

- **[Build and contribute](CONTRIBUTING.md)** — three build commands and a source map.
- **[Architecture](docs/architecture.md)** — audio pipeline, installation and shared-state ABI.
- **[Validation](docs/validation.md)** — automated tests and physical compatibility checks.
- **[Changelog](CHANGELOG.md)** — release history.

Built with C++/Win32, the RNNoise model bundled in [Werman v1.21](https://github.com/werman/noise-suppression-for-voice/releases/tag/v1.21), and the SpeexDSP resampler. Code is [GPL-3.0](LICENSE); bundled libraries retain their BSD notices. [Dependency revisions](docs/architecture.md#engine-provenance) are pinned for reproducible builds.
