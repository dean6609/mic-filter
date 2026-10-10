# Guide for coding assistants

## What this project is

MicFilter is a small native Windows app that removes background noise from
microphones. It installs a Windows audio effect (APO) running RNNoise on the
microphones the user picks, so apps receive cleaner audio. A tray icon controls
it, and a single-file installer sets it up. It is generic: it works with any
microphone brand and must not favor or detect specific brands.

## Intentions

These are the qualities every change should protect:

- **Local and private.** Audio is processed on the PC and never recorded,
  saved or uploaded. Everything works offline.
- **Lightweight and real-time safe.** Native code, a small installer, and
  nothing in the audio path that can stall: no allocation, file access or
  blocking work while processing audio.
- **Simple.** Few options with clear names, in plain English. Prefer improving or
  removing over adding.
- **Careful with the user's system.** Never break a microphone, replace other
  audio effects or weaken Windows security. Updates keep settings; uninstall
  restores every microphone to how it was.
- **Easy to fork.** Anyone can clone, build and package on their own computer
  with the pinned portable compiler. Nothing depends on access to this
  repository or its services.

## Where to look

| Need | Read |
| --- | --- |
| What users see and expect | README.md, GETTING_STARTED.txt (shipped in the installer) |
| Build, source map and working rules | CONTRIBUTING.md |
| Audio path, installation and stored settings | docs/architecture.md |
| What is tested and what needs real devices | docs/validation.md |
| What changed between versions | CHANGELOG.md |

The application lives in src/. The installer logic is in the PowerShell scripts
at the root (setup-install.ps1, install.ps1, installer-registry.ps1); build.ps1
and package.ps1 compile and bundle everything.

## Keep context small

Source files are small and focused on one responsibility. Find the file in the
CONTRIBUTING.md source map and read it and its callers, not the whole tree.
When a file grows well past a few hundred lines or mixes responsibilities,
split it.

Skip vendor/ unless the task is about it: it is unmodified third-party code,
and vendor/rnnoise/src/rnnoise_data.c alone is hundreds of thousands of lines
of model weights. Skip LICENSE too. Never explore the ignored local folders
tools/, build/ and dist*/: they hold the compiler and build output.

## How to work

Check the working tree, branch and remotes first. Keep changes focused and
follow the existing structure. Respect the working rules in CONTRIBUTING.md;
they protect existing installations and the real-time audio path. Keep
recordings, logs and device identifiers out of Git.

Run the build and isolated tests that exercise what changed. Passing tests do
not prove that a real microphone works or sounds good; say so when a change
needs a physical check. Update these guides when behavior or structure changes,
keeping them general: intentions here, specifics beside the code that owns
them.
