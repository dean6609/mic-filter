<div align="center">

![MicFilter — cleaner microphone audio for Windows](assets/readme-banner.svg)

**Less background noise. Your microphone, your voice.**

[![Download for Windows](https://img.shields.io/badge/Download-Windows_x64-14b8a6?style=for-the-badge&logo=windows)](https://github.com/dean6609/mic-filter/releases/latest/download/MicFilter-Setup.exe)
[![Latest release](https://img.shields.io/github/v/release/dean6609/mic-filter?style=for-the-badge&color=334155)](https://github.com/dean6609/mic-filter/releases/latest)

One installer · Works offline · One or several microphones

</div>

MicFilter reduces background noise in calls and recordings. Your settings stay saved after restarting Windows. Installation checks never save or upload your voice.

## Get started

1. **[Download the installer](https://github.com/dean6609/mic-filter/releases/latest/download/MicFilter-Setup.exe)**, open it and accept the Windows administrator prompt.
2. **Choose your microphones.** With only one connected, available microphone, setup continues automatically. With several, type their numbers to check or uncheck them, then press Enter.
3. **Follow the short progress messages.** Reconnect or restart only if setup asks you to. Your voice starts with **Natural**; you can change it later.

**Already using MicFilter?** Run the new installer directly. Updating from 0.5.x keeps your settings and installed microphones; no uninstall is needed. Run setup again whenever you want to add another microphone.

## Your everyday controls

The microphone icon starts with Windows. Open **MicFilter** from the desktop or Start menu if you need to show it again.

| What you do | What happens |
| --- | --- |
| Click the icon once | Turn noise reduction off or on. |
| Right-click the icon | Change voice sound, pauses and the amount of original audio. |
| Choose **Microphone activity** | Check whether filtering is working on that microphone. |
| Choose **Exit and turn off noise reduction** | Close the controls and turn the filter off. |

Opening MicFilter again keeps your current setting. Sound settings apply to all microphones you have installed it on.

## Find your voice sound

| Profile | Sound |
| --- | --- |
| **Natural** · default | Your voice without added tone shaping. |
| **Clear** | Gentle clarity with less boominess. |
| **Broadcast** | A more even speaking volume. |
| **Deep** | Body and clarity for lower voices; does not lower your pitch. |

Start with **Silence between words: Off** and **Original microphone sound: 0%**. Try 15% original sound if you prefer a more natural mix; some background noise will return.

## A few things to know

- **Windows x64 (Intel/AMD).** USB and Bluetooth microphones can work; compatibility depends on the driver. [Device details](docs/validation.md).
- **Other audio effects can conflict.** Setup keeps them and explains when a microphone is unavailable. Use **Microphone activity** to confirm filtering after installation.
- **Windows may warn about the unsigned installer.** For the file downloaded here, choose **More info → Run anyway**. You do not need to disable Windows security.
- **To uninstall:** Settings → Apps → Installed apps → MicFilter → Uninstall. Your microphones return to their previous configuration.

---

**For developers:** [Build & contribute](CONTRIBUTING.md) · [How it works](docs/architecture.md) · [Tests & compatibility](docs/validation.md) · [Release history](CHANGELOG.md)

Built with RNNoise and SpeexDSP. [GPL-3.0](LICENSE); bundled libraries retain their license notices.
