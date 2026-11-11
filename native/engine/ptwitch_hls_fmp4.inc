/* PTwitch live fMP4 adapter. Included by media_hls.c after the common
   transport/queue helpers. No transcoding, filesystem spool or blocking I/O.
   One init (64 KiB), one fragment (2 MiB), optional next fragment (2 MiB),
   all budget charged. Prefetch overlaps downloading with sample playback.
   The existing MP4 parser validates box/sample bounds and AVC/AAC metadata. */
#define HLS_FMP4_INIT_BYTES (64u * 1024u)

bool media_mp4_fragment_decode_origin(const MediaMp4Demux *demux,
                                     size_t index, uint64_t *origin);

static bool hls_fmp4_read(void *opaque,uint64_t offset,void *out,size_t length)
{
    MediaHlsSource *s=opaque;
    uint64_t total=(uint64_t)s->mp4_init_length+s->mp4_fragment_length;
    if(offset>total||length>total-offset)return false;
    unsigned char *destination=out;
    if(offset<s->mp4_init_length){
        size_t first=s->mp4_init_length-(size_t)offset;
        if(first>length)first=length;
        memcpy(destination,s->mp4_init+(size_t)offset,first);
        offset+=first;destination+=first;length-=first;
    }
    if(length)memcpy(destination,s->mp4_fragment+(size_t)(offset-s->mp4_init_length),length);
    return true;
}

static bool hls_fmp4_clock(uint64_t value,uint32_t scale,uint64_t *out)
{
    if(!scale||value/scale>UINT64_MAX/90000u)return false;
    uint64_t whole=(value/scale)*90000u;
    uint64_t fraction=(value%scale)*90000u/scale;
    if(fraction>UINT64_MAX-whole)return false;
    *out=whole+fraction;return true;
}

static bool hls_fmp4_info(MediaHlsSource *s)
{
    size_t tracks=media_mp4_track_count(s->mp4_demux);
    if(!tracks||tracks>2)return false;
    bool video=false,audio=false;
    uint64_t first=UINT64_MAX,last=UINT64_MAX;
    for(size_t i=0;i<tracks;++i){
        MediaMp4TrackInfo info={0};uint64_t origin=0;
        if(!media_mp4_track_info(s->mp4_demux,i,&info)
            ||!media_mp4_fragment_decode_origin(s->mp4_demux,i,&origin)
            ||!hls_fmp4_clock(origin,info.timescale,&s->mp4_track_origin90k[i])
            ||info.largest_sample>s->queue_capacity)return false;
        if(s->mp4_track_origin90k[i]<first)first=s->mp4_track_origin90k[i];
        MediaMp4TrackInfo *saved=NULL;unsigned char *config=NULL;bool was_valid=false;
        if(info.kind==MEDIA_MP4_TRACK_VIDEO){
            if(video||info.codec!=MEDIA_MP4_FOURCC('a','v','c','1'))return false;
            uint16_t width=0,height=0;uint8_t nal=0;
            if(!media_h264_avcc_dimensions(info.codec_config,info.codec_config_length,&width,&height,&nal)
                ||width>640||height>368
                ||media_h264_avcc_decoder_route(info.codec_config,info.codec_config_length,NULL)
                    !=MEDIA_H264_DECODER_ROUTE_PSP_FIRMWARE)return false;
            info.width=width;info.height=height;info.nal_length_size=nal;
            video=true;saved=&s->video_info;config=s->mp4_video_config;was_valid=s->video_info_valid;
        }else if(info.kind==MEDIA_MP4_TRACK_AUDIO){
            MediaAacStreamInfo aac={0};
            if(audio||info.codec!=MEDIA_MP4_FOURCC('m','p','4','a')
                ||!media_aac_esds_stream_info(info.codec_config,info.codec_config_length,&aac)
                ||aac.samples_per_frame!=1024||!aac.channels||aac.channels>2)return false;
            info.sample_rate=aac.sample_rate;info.channels=aac.channels;
            audio=true;saved=&s->audio_info;config=s->mp4_audio_config;was_valid=s->audio_info_valid;
        }else return false;
        bool selected=s->track_selection==MEDIA_HLS_TRACK_MIXED
            ||(s->track_selection==MEDIA_HLS_TRACK_VIDEO&&info.kind==MEDIA_MP4_TRACK_VIDEO)
            ||(s->track_selection==MEDIA_HLS_TRACK_AUDIO&&info.kind==MEDIA_MP4_TRACK_AUDIO);
        uint64_t duration=0;
        if(!hls_fmp4_clock(info.duration,info.timescale,&duration)
            ||duration>UINT64_MAX-s->mp4_track_origin90k[i])return false;
        uint64_t end=s->mp4_track_origin90k[i]+duration;
        if(selected&&end<last)last=end;
        if(!info.codec_config||!info.codec_config_length||info.codec_config_length>HLS_PARAMETER_SET_BYTES)return false;
        if(was_valid&&(saved->width!=info.width||saved->height!=info.height
            ||saved->channels!=info.channels||saved->sample_rate!=info.sample_rate
            ||saved->codec_config_length!=info.codec_config_length
            ||memcmp(config,info.codec_config,info.codec_config_length)!=0)){
            hls_source_fail(s,"HLS MP4 codec configuration changed; reconnect");return false;
        }
        memcpy(config,info.codec_config,info.codec_config_length);
        info.codec_config=config;info.timescale=90000u;info.duration=0;
        info.largest_sample=info.kind==MEDIA_MP4_TRACK_AUDIO?SWDEC_TS_MAX_ADTS:SWDEC_TS_MAX_AU;
        info.packet_format=MEDIA_PACKET_FORMAT_CONTAINER_NATIVE;
        *saved=info;
    }
    if(s->track_layout_known&&(video!=s->video_info_valid||audio!=s->audio_info_valid))return false;
    if((s->track_selection!=MEDIA_HLS_TRACK_AUDIO&&!video)
        ||(s->track_selection==MEDIA_HLS_TRACK_AUDIO&&!audio))return false;
    s->video_info_valid=video;s->audio_info_valid=audio;s->track_layout_known=true;
    s->mp4_end90k=last;
    if(!s->mp4_origin_valid){
        s->mp4_origin_valid=true;s->mp4_origin90k=first;s->mp4_base90k=s->segment_base90k;
    }
    return true;
}

static bool hls_fmp4_timestamp(MediaHlsSource *s,uint64_t value,
                                const MediaMp4Sample *sample,uint64_t *out)
{
    uint64_t local=0;
    if(sample->track_index>=2||!hls_fmp4_clock(value,sample->timescale,&local)
        ||local>UINT64_MAX-s->mp4_track_origin90k[sample->track_index])return false;
    uint64_t absolute=s->mp4_track_origin90k[sample->track_index]+local;
    if(absolute<s->mp4_origin90k)return false;
    uint64_t delta=absolute-s->mp4_origin90k;
    if(s->mp4_base90k>INT64_MAX||delta>(uint64_t)INT64_MAX-s->mp4_base90k)return false;
    *out=s->mp4_base90k+delta;return true;
}

/* A container sync flag is not proof of an independently decodable AVC
   picture. Live windows may begin with leading pictures from an open GOP.
   A fresh firmware decoder needs an actual IDR, not a dependent P/B picture. */
static int hls_fmp4_idr(const unsigned char *data,size_t length,unsigned prefix)
{
    if(prefix<1||prefix>4)return -1;
    bool idr=false;size_t at=0;unsigned count=0;
    while(at<length&&count++<64){
        if(prefix>length-at)return -1;
        uint32_t n=0;for(unsigned i=0;i<prefix;++i)n=(n<<8)|data[at++];
        if(!n||n>length-at||(data[at]&0x80u))return -1;
        if((data[at]&31u)==5u)idr=true;
        at+=n;
    }
    return at==length?(idr?1:0):-1;
}

static void hls_fmp4_prefetch(MediaHlsSource *s)
{
    if(!s->mp4_prefetch||!s->mp4_init_ready||!s->track_layout_known||s->failed||s->mp4_prefetch_complete
        ||s->segment_index>=s->playlist->entry_count)return;
    if(!s->requests[1].handle){
        uint64_t sequence=s->playlist->entries[s->segment_index].sequence;
        if(sequence==UINT64_MAX)return;
        const MediaHlsPlaylist *next=s->playlist;
        size_t index=hls_sequence_index(next,sequence+1u);
        if(s->pending_playlist&&strcmp(s->playlist->init_url,s->pending_playlist->init_url)==0
            &&(index==SIZE_MAX||(s->pending_playlist->entry_count
                &&s->pending_playlist->entries[0].sequence>sequence+1u))){
            next=s->pending_playlist;index=hls_sequence_index(next,sequence+1u);
        }
        if(index==SIZE_MAX)return;
        if(!hls_start_playlist_request(s,1,next,index))return;
        s->mp4_prefetch_duration_ms=next->entries[index].duration_ms;
    }
    char error[192]={0};size_t n=0;
    MediaHlsTransportPollResult result=s->transport.poll(s->transport.opaque,s->requests[1].handle,
        s->transport_chunk,MEDIA_HLS_TRANSPORT_CHUNK_BYTES,&n,error,sizeof(error));
    if(result==MEDIA_HLS_TRANSPORT_WAIT)return;
    if(result==MEDIA_HLS_TRANSPORT_COMPLETE){s->mp4_prefetch_complete=true;return;}
    if(result!=MEDIA_HLS_TRANSPORT_CHUNK){hls_source_fail(s,"%s",error[0]?error:"HLS MP4 prefetch failed");return;}
    if(!n||n>MEDIA_HLS_TRANSPORT_CHUNK_BYTES||n>MEDIA_HLS_MAXIMUM_SEGMENT_BYTES-s->mp4_prefetch_length){
        hls_source_fail(s,"HLS MP4 prefetch exceeds buffer limit");return;
    }
    memcpy(s->mp4_prefetch+s->mp4_prefetch_length,s->transport_chunk,n);
    s->mp4_prefetch_length+=n;s->stats.bytes_received+=n;
}

static void hls_fmp4_finish(MediaHlsSource *s)
{
    /* Carry speculative bytes by media sequence, never an index into a
       playlist that may slide underneath us. Release the old snapshot before
       adopting the successor so a pending refresh can advance here. */
    HlsRequest next=s->requests[1];
    s->requests[1]=(HlsRequest){0};
    s->mp4_fragment_length=0;s->mp4_fragment_complete=false;
    hls_complete_segment(s);
    bool adopt=next.handle&&!s->failed&&!s->ended&&s->mp4_init_ready
        &&s->segment_index<s->playlist->entry_count
        &&next.sequence==s->playlist->entries[s->segment_index].sequence;
    if(adopt){
        unsigned char *old=s->mp4_fragment;s->mp4_fragment=s->mp4_prefetch;s->mp4_prefetch=old;
        s->mp4_fragment_length=s->mp4_prefetch_length;
        s->mp4_fragment_complete=s->mp4_prefetch_complete;
        next.segment=s->segment_index;s->requests[0]=next;
    }else if(next.handle)s->transport.cancel(s->transport.opaque,next.handle);
    s->mp4_prefetch_length=0;s->mp4_prefetch_complete=false;
}

static void hls_fmp4_emit(MediaHlsSource *s)
{
    /* Preserve descriptor/payload headroom and yield after eight samples.
       Retain a pending descriptor across frames instead of losing a sample
       when the queue is full. MP4 payload stays in its fragment until copied. */
    for(unsigned step=0;step<8&&!s->failed;++step){
        if(!s->mp4_sample_pending){
            if(!media_mp4_next_sample(s->mp4_demux,&s->mp4_sample)){
                char error[192]={0};
                if(media_mp4_last_error(s->mp4_demux,error,sizeof(error))){hls_source_fail(s,"%s",error);return;}
                media_mp4_close(s->mp4_demux);s->mp4_demux=NULL;
                if(s->mp4_wait_idr&&s->playlist->end_list&&s->segment_index+1u==s->playlist->entry_count){
                    hls_source_fail(s,"HLS MP4 AVC has no startup IDR");return;
                }
                hls_fmp4_finish(s);return;
            }
            s->mp4_sample_pending=true;
        }
        const MediaMp4Sample *sample=&s->mp4_sample;
        if((s->track_selection==MEDIA_HLS_TRACK_AUDIO&&sample->kind!=MEDIA_MP4_TRACK_AUDIO)
            ||(s->track_selection==MEDIA_HLS_TRACK_VIDEO&&sample->kind!=MEDIA_MP4_TRACK_VIDEO)){
            s->mp4_sample_pending=false;continue;
        }
        if(sample->size>s->queue_capacity||sample->size>SWDEC_TS_MAX_AU
            ||(sample->kind==MEDIA_MP4_TRACK_AUDIO&&sample->size>SWDEC_TS_MAX_ADTS)){
            hls_source_fail(s,"HLS MP4 sample exceeds packet limit");return;
        }
        if(s->queue_count>=s->queue_sample_limit||sample->size>s->queue_capacity-s->queue_used)return;
        uint64_t pts=0,dts=0,duration=0;
        if(sample->track_index>=2||sample->offset<s->mp4_init_length
            ||sample->offset-s->mp4_init_length>s->mp4_fragment_length
            ||sample->size>s->mp4_fragment_length-(size_t)(sample->offset-s->mp4_init_length)){
            hls_source_fail(s,"HLS MP4 sample bounds or timestamp invalid");return;
        }
        const unsigned char *payload=s->mp4_fragment+(size_t)(sample->offset-s->mp4_init_length);
        uint64_t local_dts=0;
        if(!hls_fmp4_clock(sample->dts,sample->timescale,&local_dts)
            ||local_dts>UINT64_MAX-s->mp4_track_origin90k[sample->track_index]){
            hls_source_fail(s,"HLS MP4 decode timestamp invalid");return;
        }
        uint64_t absolute_dts=local_dts+s->mp4_track_origin90k[sample->track_index];
        if(s->mp4_wait_idr){
            if(sample->kind==MEDIA_MP4_TRACK_AUDIO){s->mp4_sample_pending=false;continue;}
            int idr=hls_fmp4_idr(payload,sample->size,s->video_info.nal_length_size);
            if(idr<0){hls_source_fail(s,"HLS MP4 AVC NAL framing invalid");return;}
            if(!idr){
                s->mp4_skipped_video++;s->mp4_sample_pending=false;
                if(s->mp4_skipped_video>=300){hls_source_fail(s,"HLS MP4 AVC has no startup IDR");return;}
                continue;
            }
            s->mp4_wait_idr=false;s->mp4_origin90k=absolute_dts;
            s->mp4_sample.keyframe=true;
            printf("ptwitch-fmp4: startup IDR bytes=%u skipped=%u size=%ux%u profile=%u nal-prefix=%u\n",
                (unsigned)sample->size,s->mp4_skipped_video,s->video_info.width,s->video_info.height,
                s->video_info.codec_config[1],s->video_info.nal_length_size);
        }
        if(absolute_dts<s->mp4_origin90k){s->mp4_sample_pending=false;continue;}
        if(sample->pts<0||!hls_fmp4_timestamp(s,(uint64_t)sample->pts,sample,&pts)
            ||!hls_fmp4_timestamp(s,sample->dts,sample,&dts)
            ||!hls_fmp4_clock(sample->duration,sample->timescale,&duration)||duration>UINT32_MAX){
            hls_source_fail(s,"HLS MP4 presentation timestamp invalid");return;
        }
        if(s->mp4_have_dts[sample->track_index]&&dts<=s->mp4_last_dts90k[sample->track_index]){
            hls_source_fail(s,"HLS MP4 decode clock moved backward; reconnect");return;
        }
        if(!hls_queue_push(s,sample->kind,MEDIA_PACKET_FORMAT_CONTAINER_NATIVE,
            payload,sample->size,pts,(uint32_t)(duration?duration:1),sample->keyframe))return;
        size_t tail=(s->queue_head+s->queue_count-1u)%HLS_SAMPLE_LIMIT;
        s->queue[tail].raw_dts90k=dts;s->mp4_sample_pending=false;
        s->mp4_last_dts90k[sample->track_index]=dts;s->mp4_have_dts[sample->track_index]=true;
    }
}

static void hls_fmp4_pump(MediaHlsSource *s)
{
    if(s->mp4_demux){hls_fmp4_emit(s);return;}
    hls_prepare_segment(s,s->segment_index);
    if(!s->mp4_init){
        s->mp4_init=budget_malloc_category(s->budget,BUDGET_CATEGORY_RESOURCE,HLS_FMP4_INIT_BYTES);
        s->mp4_fragment=budget_malloc_category(s->budget,BUDGET_CATEGORY_RESOURCE,MEDIA_HLS_MAXIMUM_SEGMENT_BYTES);
        if(!s->mp4_init||!s->mp4_fragment){hls_source_fail(s,"HLS MP4 buffer budget refused");return;}
        /* If the optional second buffer is refused, playback retains its
           bounded single-buffer path instead of failing memory admission. */
        s->mp4_prefetch=budget_malloc_category(s->budget,BUDGET_CATEGORY_RESOURCE,MEDIA_HLS_MAXIMUM_SEGMENT_BYTES);
    }
    char error[192]={0};uint64_t handle=0;
    if(s->mp4_fragment_complete)goto open_fragment;
    if(!s->mp4_init_ready){
        if(!s->mp4_init_handle){
            s->mp4_init_handle=s->transport.start(s->transport.opaque,s->playlist->init_url,
                HLS_FMP4_INIT_BYTES,error,sizeof(error));
            if(!s->mp4_init_handle){if(error[0])hls_source_fail(s,"%s",error);return;}
        }
        handle=s->mp4_init_handle;
    }else{
        if(!s->requests[0].handle){
            if(!hls_start_request(s,0,s->segment_index))return;
        }
        handle=s->requests[0].handle;
        hls_fmp4_prefetch(s);if(s->failed)return;
    }
    size_t length=0;
    MediaHlsTransportPollResult result=s->transport.poll(s->transport.opaque,handle,
        s->transport_chunk,MEDIA_HLS_TRANSPORT_CHUNK_BYTES,&length,error,sizeof(error));
    if(result==MEDIA_HLS_TRANSPORT_WAIT)return;
    if(result==MEDIA_HLS_TRANSPORT_ERROR){
        hls_source_fail(s,"%s",error[0]?error:"HLS MP4 download failed");return;
    }
    if(result==MEDIA_HLS_TRANSPORT_CHUNK){
        size_t *used=s->mp4_init_ready?&s->mp4_fragment_length:&s->mp4_init_length;
        size_t limit=s->mp4_init_ready?MEDIA_HLS_MAXIMUM_SEGMENT_BYTES:HLS_FMP4_INIT_BYTES;
        unsigned char *destination=s->mp4_init_ready?s->mp4_fragment:s->mp4_init;
        if(!length||length>MEDIA_HLS_TRANSPORT_CHUNK_BYTES||length>limit-*used){
            hls_source_fail(s,"HLS MP4 resource exceeds buffer limit");return;
        }
        memcpy(destination+*used,s->transport_chunk,length);*used+=length;
        s->stats.bytes_received+=length;return;
    }
    if(result!=MEDIA_HLS_TRANSPORT_COMPLETE){hls_source_fail(s,"HLS MP4 transport state invalid");return;}
    if(!s->mp4_init_ready){
        s->mp4_init_handle=0;
        if(s->mp4_init_length<8){hls_source_fail(s,"HLS MP4 initialization is empty");return;}
        s->mp4_init_ready=true;return;
    }
    s->mp4_fragment_complete=true;
open_fragment: ;
    MediaRangeReader reader={.opaque=s,.length=(uint64_t)s->mp4_init_length+s->mp4_fragment_length,.read=hls_fmp4_read};
    MediaMp4Limits limits=media_mp4_default_limits();
    limits.maximum_metadata_bytes=HLS_FMP4_INIT_BYTES;
    limits.maximum_track_table_bytes=256u*1024u;
    limits.maximum_tracks=2;limits.maximum_samples_per_track=2048;
    limits.maximum_chunks_per_track=2048;limits.maximum_table_entries=2048;
    limits.maximum_sample_bytes=SWDEC_TS_MAX_AU;
    s->mp4_demux=media_mp4_open(s->budget,&reader,&limits,error,sizeof(error));
    if(!s->mp4_demux){hls_source_fail(s,"HLS MP4: %s",error);return;}
    if(!hls_fmp4_info(s)){hls_source_fail(s,"HLS MP4 tracks or codec unsupported");return;}
    hls_fmp4_emit(s);
}
