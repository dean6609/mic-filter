# Changelog

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
