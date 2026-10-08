# Changelog

## 0.6.0 — 2026-10-08

- Restore automatic selection and installation when only one connected, enabled microphone is found. Compatibility checks still apply; multiple microphones keep the console checklist.
- Simple colored console setup with persistent microphone checks, number-based multiple selection and plain progress/results. Fresh installs default to Natural without a voice prompt; updates keep the saved profile. Setup explains single-click on/off and later voice changes from the tray.
- Independent restore backups for multiple inputs, including migration of the previous single-input backup. Removal keeps other microphones registered; uninstall restores disconnected inputs too. Batch setup checks every selected input and restores the batch if capture fails.
- Embedded microphone icon in the application and installer, with explicit icon references for Settings > Apps and shortcuts. Lossless PNG compression keeps all seven sizes without bloating the executables.
- Gentler Clear and Broadcast profiles: less treble emphasis, moderate Broadcast compression with a soft knee and lower makeup gain. New Deep profile retains low fundamentals, reduces low-mid mud and adds restrained consonant presence without changing pitch.
- Per-microphone activity counters and tray input selection prevent activity on one input from confirming another. Shared voice and enable/disable controls retain the 64-byte ABI.
- Compact tray submenus for voice, silence and original sound. Microphone activity checks capture/filtering asynchronously and explains results in plain language. Diagnostics, log, removal, uninstall and startup-toggle commands are no longer shown in the tray; uninstall remains in Windows Installed apps.
- The icon starts with Windows automatically. Startup and repeated launches preserve enable/disable state, fixing a second application launch unexpectedly toggling the filter. APO-reported version now matches the 0.6 registration.
- Other audio applications and manufacturer processing effects remain untouched and appear unavailable in setup. Windows' discovery-only proxy and driver association metadata no longer cause false conflicts; capture and per-input processing are still checked. Physical compatibility and subjective voice quality require capture/listening checks.

## 0.5.3 — 2026-10-01

- **Voice sound** presets in the tray menu: **Natural** (unchanged, default), **Clear** (removes rumble and low-mid boominess, adds presence and air) and **Broadcast** (stronger presence plus gentle compression for an even, polished voice). Both polished presets end in a soft limiter; switching presets crossfades without clicks. Processing cost is about 0.3% of one CPU core.
- Clearer tray menu: options grouped under **Voice sound**, **Silence between words** (Off / Balanced / Strict) and **Original microphone sound** (0% / 15%). "Start with Windows" is now "Show this icon when Windows starts", and Exit reads "Exit and disable filter".
- Friendlier setup window: short colored steps with a progress indicator, plain-language errors and a clear final message (for example "Almost done: restart Windows to start using the new version"). Technical details go to the log file only.
- Presets are stored in a new `options.bin`, so the existing `state.bin` format is unchanged.

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
