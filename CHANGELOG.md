# Changelog

## 0.5.0 — 2026-09-30

- Fixed the original-signal timing in the dry/wet mix. RNNoise v1.21 delays its output by 20 ms (overlap/add plus one delayed frame), not 10 ms; the 10 ms mismatch comb-filtered partial mixes with notches every 100 Hz, which hollowed low voices and made vibrato fluctuate.
- The voice-detection gate now fades out over 50 ms and in over 5 ms instead of switching at 10 ms block edges, removing clicks at word endings.
- Maximum attenuation is limited to -20 dB: a small amount of the original signal is always kept, so word tails and room tone no longer drop into digital silence.
- Reported latency corrected from 20 ms to 30 ms.
- Tests now verify the dry path against the model's measured delay and check that the gate closes without clicks.

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
