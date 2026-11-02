#pragma once

// Button and touch wording share the same game screens.
#if defined(SH_TOUCH_UI)
#define SH_UI_TEXT(keys, touch) touch
#else
#define SH_UI_TEXT(keys, touch) keys
#endif
