#ifndef PTWITCH_AVC_PACKET_H
#define PTWITCH_AVC_PACKET_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
/* Input is the four-byte-length AVCC staging copy converted from MPEG-TS.
   This seam deliberately does not touch native MP4 samples. */
static inline bool ptwitch_ts_avcc_picture(unsigned char *data,size_t length,
                                          size_t *output)
{
    if (!data || !output || !length || length>512u*1024u) return false;
    *output=0;
    size_t at=0;unsigned count=0;bool picture=false;
    /* Validate before compacting: failure must not leave half a packet. */
    while(at<length){
        if(++count>64u || length-at<4u)return false;
        size_t n=((uint32_t)data[at]<<24)|((uint32_t)data[at+1]<<16)
            |((uint32_t)data[at+2]<<8)|data[at+3];
        at+=4u;
        if(!n||n>length-at||(data[at]&0x80u))return false;
        unsigned type=data[at]&31u;
        if(!type)return false;
        if(type==1u||type==5u)picture=true;
        at+=n;
    }
    if(!picture)return false;
    size_t write=0;at=0;
    while(at<length){
        size_t n=((uint32_t)data[at]<<24)|((uint32_t)data[at+1]<<16)
            |((uint32_t)data[at+2]<<8)|data[at+3];
        unsigned type=data[at+4u]&31u;
        /* Frame boundaries/timing come from the demux. Parameter sets and
           every picture slice remain byte-for-byte intact. */
        if(type!=6u&&type!=9u){
            memmove(data+write,data+at,4u+n);write+=4u+n;
        }
        at+=4u+n;
    }
    *output=write;return true;
}
#endif
