#include "game.h"

// The Xteink X4 has no LED or speaker, and its GPIO4 is the display's
// data/command line, so there is no signal feedback.
void audio_begin() {}
void audio_tick() {}
void audio_notice() {}
void audio_alert() {}
