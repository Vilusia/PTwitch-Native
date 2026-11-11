#ifndef PTWITCH_HLS_BUFFER_POLICY_H
#define PTWITCH_HLS_BUFFER_POLICY_H
#include <stdbool.h>
#include <stdint.h>
/* A bounded source cannot build arbitrary runway while its consumer is held.
   The caller excludes deliberately backpressured TS delivery from pending:
   consuming the resident tail is required to release that producer too. */
static inline uint64_t ptwitch_hls_resume_cap(uint64_t ahead,bool pending,uint64_t normal_target)
{
    if(!ahead)return 0;
    return !pending&&ahead<normal_target?ahead:normal_target;
}
#endif
