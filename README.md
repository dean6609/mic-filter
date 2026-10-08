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
| **One or several microphones** | Choose connected, enabled inputs in a simple console checklist. Add more later without removing the existing installation. |
| **One `.exe`** | The model and all runtime components are included. Installation works offline. |
| **Persistent settings** | The filter keeps its enabled or disabled state after reboot. |
| **Lightweight controls** | The tray icon starts with Windows; click once to turn noise reduction off/on and right-click for voice options. |

## Install in a minute

1. **[Download MicFilter-Setup.exe](https://github.com/dean6609/mic-filter/releases/latest/download/MicFilter-Setup.exe)** and double-click it.
2. Accept the Windows administrator prompt. With only one connected, enabled microphone, setup selects it and continues automatically if it is available. With several microphones, type their numbers to check/uncheck them (`1,2` selects two), or `A` for all available inputs. Press Enter to continue with the checked `[x]` inputs.
3. Follow the short colored progress messages. Setup checks each microphone and adds **MicFilter**, with its microphone icon, to your desktop, Start menu and **Settings → Apps**. Voice sound starts at **Natural**; change it later from the tray menu.

The first installation enables the filter and configures the icon to show when Windows starts. Open the desktop shortcut whenever you want to show its tray controls; reopening it keeps the current state. Setup works offline and uses the standard Windows installation location. Existing inputs are checked by default and keep their saved controls and voice sound. `0` cancels microphone selection. The console stays open at the end so you can read the result.

An input managed by another audio app or a manufacturer effects chain is shown as unavailable. Choose another input, or remove the other app's effect from that input and run setup again. MicFilter updates its own existing effect safely and preserves other applications' effects.

Windows' discovery-only effects proxy and driver association metadata are kept; their presence alone no longer blocks setup. Capture and per-input processing checks still determine whether the filter is confirmed.

## Your filter, your controls

| Action | Result |
| --- | --- |
| Open MicFilter | Show the tray icon and keep the current setting. |
| Click the icon | Enable or disable filtering. |
| **Exit and turn off noise reduction** | Disable the filter and close the app. |
| Restart Windows | Show the icon automatically and keep the last enabled/disabled setting. |
| Leave the app closed | Windows continues using the saved filter setting. |

Right-click for options:

| Option | Choices |
| --- | --- |
| **Voice sound** | **Natural** (as captured, default) · **Clear** (gentle clarity without excessive brightness) · **Broadcast** (warm speech with moderate compression and even volume) · **Deep** (preserves the body of lower voices, reduces muddy resonance and keeps consonants clear). |
| **Silence between words** | **Off** (default, noise removal only) · **Balanced** and **Strict** also silence pauses, but can clip very soft words. |
| **Original microphone sound** | **0%** (cleanest, default) · **15%** (more natural, some noise returns). |

**Reducing noise now** means the selected input has recent processing activity. **Turned on - check microphone activity** means the setting is on but current processing is not confirmed. Choose an input under **Microphone activity** to check real capture and that input's filtering. The result explains whether filtering works, is off, or could not be detected; no recordings are saved.

The voice profile, noise controls and enable/disable setting apply to **all installed microphones**, including when two inputs are used at the same time. Each input has its own processing state and restore backup. **Deep** shapes the existing voice; it does not lower pitch.

Run your downloaded installer again to add or update microphones; **Add or update microphones** in the tray reminds you of the steps. Unchecked inputs already installed are kept. Voice sound, silence and original-sound choices use compact submenus; maintenance and log commands are kept out of the tray. Uninstall restores all inputs, including disconnected ones.

## Uninstall

Open **Settings → Apps → Installed apps → MicFilter → Uninstall**. Microphones return to their previous configuration, and MicFilter's files, shortcuts, startup entries and settings are deleted. A file still held by Windows audio is deleted at the next restart.

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
