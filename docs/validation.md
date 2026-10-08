# Validation and compatibility

## Automated checks

`build.ps1 -RunTests` checks exact bypass, direct RNNoise equivalence at 48 kHz, variable callbacks, dry/wet alignment against the measured model delay, click-free gate fades, the -20 dB floor around speech and its click-free fade, more than 35 dB of noise reduction away from speech, voice preset EQ, compression, limiting and click-free preset changes, in-place buffers, silence, resampling continuity, voice fundamental preservation, toggle reset, COM aggregation, reference counts, format negotiation and buffer bounds.

The tested rates are 8, 16, 22.05, 44.1, 48, 96 and 192 kHz; tested channel counts are 1, 2, 4 and 8. These check the processing implementation, not every audio driver.

`package.ps1 -RunTests` checks all nine embedded components, isolated extraction, Windows loading all seven compressed icon sizes, persistent console selection (multiple inputs, toggling, invalid answers, unavailable inputs, keep/continue/cancel) and PowerShell 5.1-compatible scripts. Scripted stdin also exercises the real console prompts, retaining earlier inputs and selecting a voice profile. Registry tests use disposable HKCU fixtures and do not affect real devices.

`tests/multi-device-tests.ps1` executes the installer against private HKCU keys and temporary files, substituting only elevation, process and hardware/shortcut boundaries. It checks legacy-backup migration, idempotent installation on two inputs, saved disabled/Deep settings, conflict preservation, capture-failure rollback of a selected batch, individual removal and full uninstall. APO tests initialize two synthetic endpoint property stores and verify that each input's processing increments only its own telemetry. DSP checks include low-fundamental retention and mud/treble control for Deep, restrained Clear brightness, compression, limiting, all eight linked channels and switching all four presets.

## Physical checks

Installation and voice capture have been checked with a Wavo POD on Windows x64. This is test hardware, not a brand restriction. More devices need physical checks before specific driver compatibility can be claimed.

Simultaneous physical capture on multiple microphones and revised voice profiles still need a physical installation/listening check. Isolated tests do not establish subjective voice quality or compatibility with every driver. Regression fixtures ensure that Microsoft's discovery-only proxy and driver metadata remain intact through install/uninstall and do not cause false conflicts; actual processing effects still block installation.

Use a shared capture application with effects enabled, verify growing APO counters, and listen to actual speech. Check consonants, quiet word endings, pauses and the intended dictation app. Counters alone do not establish voice quality.

| Observation | Meaning |
| --- | --- |
| Capture opens | Windows accepts this capture configuration. |
| Received frames grow | The application receives audio packets. |
| APO callbacks grow | Windows runs the installed effect. |
| Filtered frames grow | The enabled processing path runs. |
| Voice sounds clear | Listening confirms quality for this voice and setup. |

RAW and exclusive-mode applications can bypass system effects. Manufacturer effect chains may conflict with this association. The installer preserves those chains and explains the conflict instead of overwriting them.

Native x64 Windows is required; ARM64 audio hosts cannot load this x64 APO. The executable is unsigned, so Windows may show an unknown-publisher prompt. No security-policy changes are required or performed.
