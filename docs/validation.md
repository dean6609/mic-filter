# Validation and compatibility

## Automated checks

`build.ps1 -RunTests` checks exact bypass, direct RNNoise equivalence at 48 kHz, variable callbacks, dry/wet alignment, in-place buffers, silence, resampling continuity, voice fundamental preservation, toggle reset, COM aggregation, reference counts, format negotiation and buffer bounds.

The tested rates are 8, 16, 22.05, 44.1, 48, 96 and 192 kHz; tested channel counts are 1, 2, 4 and 8. These check the processing implementation, not every audio driver.

`package.ps1 -RunTests` checks all nine embedded components, isolated extraction and PowerShell 5.1-compatible scripts. Registry tests use disposable HKCU fixtures and do not affect real devices.

## Physical checks

Installation and voice capture have been checked with a Wavo POD on Windows x64. This is test hardware, not a brand restriction. More devices need physical checks before specific driver compatibility can be claimed.

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
