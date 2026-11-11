#ifndef PTWITCH_PLAYBACK_POLICY_H
#define PTWITCH_PLAYBACK_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#define PTWITCH_QUALITY_RECHECK_STABLE_US UINT64_C(30000000)
#define PTWITCH_QUALITY_RETRY_DELAY_US UINT64_C(180000000)
#define PTWITCH_QUALITY_PROBE_WINDOW_US UINT64_C(90000000)
#define PTWITCH_QUALITY_MAX_RETRIES 5u

/* Startup AVC refusals keep the historical downward retry. A decoder which
   stalls after playback has begun gets one lower-quality attempt only when
   the current rendition is above the 240p target. */
static inline bool ptwitch_decoder_fallback_allowed(
    unsigned height, bool opened, bool avc_refused, bool no_progress)
{
    if (!opened) return height > 144u && (avc_refused || no_progress);
    return height > 240u && no_progress;
}

static inline bool ptwitch_quality_recheck_due(
    bool pending, unsigned height, uint64_t stable_since_us, uint64_t now_us,
    uint64_t required_stable_us)
{
    return pending && height < 360u && stable_since_us != 0
        && now_us >= stable_since_us
        && now_us - stable_since_us >= required_stable_us;
}

static inline bool ptwitch_quality_retry_allowed(unsigned retries)
{
    return retries < PTWITCH_QUALITY_MAX_RETRIES;
}

#endif
