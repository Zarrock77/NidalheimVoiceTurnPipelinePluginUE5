/*
 * Single-translation-unit build of miniaudio for the NidalheimVoiceTurnPipeline plugin.
 *
 * We only need WASAPI capture (push-to-talk mic and TTS playback for the voice-turn pipeline).
 * Disable every other backend and high-level subsystem so the compiled
 * footprint stays small and the dependency surface stays at "Windows
 * native APIs only".
 */

#define MINIAUDIO_IMPLEMENTATION

#define MA_NO_DSOUND
#define MA_NO_WINMM
#define MA_NO_NULL
#define MA_NO_JACK
#define MA_NO_COREAUDIO
#define MA_NO_SNDIO
#define MA_NO_AUDIO4
#define MA_NO_OSS
#define MA_NO_PULSEAUDIO
#define MA_NO_ALSA
#define MA_NO_AAUDIO
#define MA_NO_OPENSL
#define MA_NO_WEBAUDIO
#define MA_NO_CUSTOM

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE

#include "miniaudio.h"
