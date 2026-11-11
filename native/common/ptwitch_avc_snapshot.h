#ifndef PTWITCH_AVC_SNAPSHOT_H
#define PTWITCH_AVC_SNAPSHOT_H
#include <stddef.h>
void ptwitch_avc_snapshot(const void *sps,size_t sps_length,
                         const void *pps,size_t pps_length,
                         const void *sample,size_t length,unsigned prefix,
                         unsigned width,unsigned height,int mode,int boot);
#endif
