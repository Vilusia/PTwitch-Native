#ifndef PTWITCH_PLAYBACK_POLICY_H
#define PTWITCH_PLAYBACK_POLICY_H

#include <stdbool.h>

/* Startup AVC refusals keep the historical downward retry. A decoder which
   stalls after playback has begun gets one lower-quality attempt only when
   the current rendition is above the 240p target. */
static inline bool ptwitch_decoder_fallback_allowed(
    unsigned height, bool opened, bool avc_refused, bool no_progress)
{
    if (!opened) return height > 144u && (avc_refused || no_progress);
    return height > 240u && no_progress;
}

#endif
