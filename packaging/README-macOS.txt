MicFilter for macOS (Apple silicon)

BEFORE THE FIRST USE
macOS has no built-in way to hand filtered sound to other apps, so MicFilter
uses the free BlackHole 2ch audio driver. Install it once:

    brew install blackhole-2ch

or download it from https://existential.audio/blackhole/

START
Double-click MicFilter.command. A Terminal window opens.
  1. Choose your microphone with the arrow keys and press Enter.
  2. In your call or recording app, choose "BlackHole 2ch" as the microphone.
  3. Keep the window open while you talk. Press q to stop.

The first time, macOS asks whether Terminal may use the microphone: allow it.
This download is not signed by Apple yet. If macOS refuses to open it,
right-click MicFilter.command, choose Open, and confirm. If it still refuses,
run this once in Terminal from this folder:

    xattr -dr com.apple.quarantine .

KEYS
space  noise reduction on/off      v  voice sound (Natural, Clear, Broadcast, Deep)
s      silence between words       o  original microphone sound (0% or 15%)
m      change microphone           q  quit

Everything runs on your Mac. Nothing is recorded or uploaded.
Licenses for MicFilter, RNNoise, the SpeexDSP resampler and miniaudio are
included in this folder.
