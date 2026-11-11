#ifndef PTWITCH_HLS_CODEC_H
#define PTWITCH_HLS_CODEC_H
#include "tilefinch/media_mp4.h"

/* HLS is a delivery protocol, not an AVC packet representation. MPEG-TS
   supplies Annex-B parameter sets; fMP4 supplies an AVCDecoderConfigurationRecord. */
static inline MediaH264DecoderRoute ptwitch_hls_video_codec(
    const MediaMp4TrackInfo *video,uint8_t *profile,unsigned *constraints,unsigned *level)
{
    *profile=0;*constraints=0;*level=0;
    if(!video)return MEDIA_H264_DECODER_ROUTE_UNSUPPORTED;
    MediaH264DecoderRoute route=MEDIA_H264_DECODER_ROUTE_UNSUPPORTED;
    if(video->packet_format==MEDIA_PACKET_FORMAT_CONTAINER_NATIVE){
        route=media_h264_avcc_decoder_route(video->codec_config,video->codec_config_length,profile);
        if(route!=MEDIA_H264_DECODER_ROUTE_UNSUPPORTED&&video->codec_config_length>=4){
            *constraints=video->codec_config[2];*level=video->codec_config[3];
        }
    }else if(video->packet_format==MEDIA_PACKET_FORMAT_H264_ANNEX_B){
        route=media_h264_annexb_decoder_route(video->codec_config,video->codec_config_length,profile);
        const unsigned char *sps=NULL,*pps=NULL;size_t ns=0,np=0;
        if(route!=MEDIA_H264_DECODER_ROUTE_UNSUPPORTED&&media_h264_annexb_parameter_sets(
            video->codec_config,video->codec_config_length,&sps,&ns,&pps,&np)&&ns>=4){
            *constraints=sps[2];*level=sps[3];
        }
    }
    return route;
}
#endif
