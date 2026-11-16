MicFilter for Linux (x86_64, PipeWire or PulseAudio)

START
Double-click "micfilter" (if your file manager asks, choose Run), or run it
from a terminal:

    ./micfilter

  1. Choose your microphone with the arrow keys and press Enter.
  2. MicFilter creates a virtual microphone called "MicFilter-Microphone" and
     makes it the system default, so most apps use it right away. In apps that
     keep their own choice, pick "MicFilter-Microphone".
  3. Keep the window open while you talk. Press q to stop: the virtual
     microphone is removed and your previous default microphone comes back.

ADD IT TO YOUR APPLICATIONS MENU (optional, no administrator rights)

    ./micfilter --install        remove later with: micfilter --uninstall

KEYS
space  noise reduction on/off      v  voice sound (Natural, Clear, Broadcast, Deep)
s      silence between words       o  original microphone sound (0% or 15%)
m      change microphone           q  quit

REQUIREMENTS
A desktop with PipeWire (pipewire-pulse) or PulseAudio and the "pactl" tool,
which most distributions install by default. If it is missing, MicFilter shows
the command to install it.

Everything runs on your computer. Nothing is recorded or uploaded.
Licenses for MicFilter, RNNoise, the SpeexDSP resampler and miniaudio are
included in this folder.
