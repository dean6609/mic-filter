# Changelog

## 0.5.2 — 2026-10-01

- Full noise reduction is back away from speech. Since 0.5.0 a fixed -20 dB share of the original signal was always mixed in, which capped noise reduction at 20 dB and let taps, clicks and background noise through. That share is now held only while RNNoise detects speech and for 300 ms after it, then fades out over 400 ms. Word endings keep the 0.5.0 behavior; in pauses, background noise drops by about 47 dB in tests (was 20 dB).
- Setup now says RESTART REQUIRED instead of READY when Windows audio still has the previous filter version loaded; until Windows restarts, an update keeps processing with the old version.
- Tests re-derive the floor from the model's voice probability, check that it fades out without clicks and require more than 35 dB of noise reduction away from speech.

## 0.5.1 — 2026-10-01

- MicFilter is listed in **Settings → Apps** with version and an **Uninstall** button.
- Full uninstall from Settings, the tray menu (**Uninstall MicFilter**) or `MicFilter-Setup.exe --uninstall`. It restores every microphone that uses the effect, even when disconnected, and deletes program files, settings, logs, shortcuts, the startup entry and registrations. Files Windows audio still holds are deleted at the next restart.
- Leftovers from WavoFilter builds (program folder, class registrations, shortcut) are removed by uninstall, and by updates once no microphone uses them.
- Updates delete effect DLLs from earlier versions instead of keeping one per release.
- Setup also adds MicFilter to the Start menu.
- The effect registration reports version 0.5 (was 0.4).
- README and GETTING_STARTED cover uninstalling, the SmartScreen prompt and driver updates that reset microphone effects.

## 0.5.0 — 2026-09-30

- Fixed the original-signal timing in the dry/wet mix. RNNoise v1.21 delays its output by 20 ms (overlap/add plus one delayed frame), not 10 ms; the 10 ms mismatch comb-filtered partial mixes with notches every 100 Hz, which hollowed low voices and made vibrato fluctuate.
- The voice-detection gate now fades out over 50 ms and in over 5 ms instead of switching at 10 ms block edges, removing clicks at word endings.
- Maximum attenuation is limited to -20 dB: a small amount of the original signal is always kept, so word tails and room tone no longer drop into digital silence.
- Reported latency corrected from 20 ms to 30 ms.
- Tests now verify the dry path against the model's measured delay and check that the gate closes without clicks.
- Replaced the bundled Windows SDK headers with project-written APO declarations (`src/apo_sdk.h`); the repository no longer redistributes Microsoft headers.
- Removed development-only leftovers: the diagnostic APO variant and its `diagnostic.bin` mapping. `MicFilter.exe --check-com` now probes the installed class.
- The APO payload is now `MicFilterAPO.dll`; upgrades remove the old `MicFilterAPO-v4.dll` copy.
- Removed the unused RNNoise "little" model source (29 MB).

## 0.4.0 — 2026-09-30

- Renamed the project, application and installer to **MicFilter**.
- English tray controls, installer progress, errors and documentation.
- A focused README with a visual header, direct download and quick-start guide.
- Generic endpoint discovery with no microphone-brand preference.
- Preserved saved controls and restore backups when upgrading older installations.
- Moved source navigation, architecture and validation details into dedicated documents.
- A regular release with a single installer asset.

The RNNoise model and audio processing are unchanged from 0.3.0.

## 0.3.0 — 2026-09-30

- Single-file native installer with embedded components and a desktop shortcut.
- Active microphone selection and automatic selection for a single input.
- Internal resampling for 8–192 kHz and 1–8 channels.
- Persistent enable/disable controls and tray profiles.
- Capture verification without saving audio, persistent logs and rollback.
- Correct COM aggregation and registration of one APO processing interface.
- Isolated DSP, resampling, COM, registry and package tests.
