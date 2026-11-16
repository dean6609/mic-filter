#pragma once
// miniaudio configured for the terminal app: device capture/playback only, and only the
// backend that can reach the virtual microphone on each system.
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#if defined(__APPLE__)
#define MA_ENABLE_COREAUDIO
#else
#define MA_ENABLE_PULSEAUDIO
#endif
#include <miniaudio.h>
