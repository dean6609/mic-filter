# Repository guidance

MicFilter is a generic Windows microphone noise-suppression application. Use English for maintained documentation, source comments, logs and UI text. Do not add microphone-brand-specific discovery or positioning.

Start with CONTRIBUTING.md and docs/architecture.md for responsibilities and compatibility decisions. Source changes belong in src/, setup-install.ps1, install.ps1, installer-registry.ps1 or the build/package scripts; vendor/ contains third-party code and licenses.

Preserve the 64-byte shared-state ABI, exact bypass, the configured endpoint and saved enable/disable behavior. Opening the tray does not enable the filter; exiting disables it. Preserve these semantics across upgrades.

Keep all allocation, disk access and blocking work out of APOProcess. Initialize network state and resamplers before the audio thread. Register only IAudioProcessingObject in NumAPOInterfaces and preserve proper COM aggregation.

Run relevant isolated tests. A compilation or synthetic test is not proof of physical device compatibility. A working capture plus growing APO counters confirms the installed processing route; voice quality still requires listening.

Do not publish local logs, recordings, forensic scripts or device identifiers. Do not disable Windows security or replace unrelated effects. Keep compatibility-only legacy names where required for existing installations, and explain them in architecture documentation.
