/* Two independent read heads over one bounded TS download and payload arena. */
static size_t hls_view_head(const HlsTrackView *v)
{
    MediaHlsSource *s=v->source;
    for(size_t i=0;i<s->queue_count;++i){
        size_t at=(s->queue_head+i)%HLS_SAMPLE_LIMIT;
        if(!s->queue[at].consumed&&s->queue[at].kind==v->kind)return at;
    }
    return SIZE_MAX;
}
static size_t hls_view_count(const void *opaque){(void)opaque;return 1;}
static bool hls_view_info(const void *opaque,size_t index,MediaMp4TrackInfo *out)
{
    const HlsTrackView *v=opaque;
    if(index||!out)return false;
    return hls_track_info(v->source,v->kind==MEDIA_MP4_TRACK_AUDIO?1u:0u,out);
}
static bool hls_view_next(void *opaque,MediaMp4Sample *out)
{
    HlsTrackView *v=opaque;MediaHlsSource *s=v->source;
    if(!out||s->failed)return false;
    size_t at=hls_view_head(v);
    if(at==SIZE_MAX){media_hls_source_pump(s,0);at=hls_view_head(v);}
    if(at==SIZE_MAX)return false;
    const HlsQueuedSample *q=&s->queue[at];
    *out=(MediaMp4Sample){.track_index=0,.kind=q->kind,.offset=q->identity,
        .size=q->payload_length,.dts=q->raw_dts90k,
        .pts=q->raw_pts90k>INT64_MAX?INT64_MAX:(int64_t)q->raw_pts90k,
        .duration=q->duration90k,.timescale=90000u,.keyframe=q->keyframe,
        .packet_format=q->packet_format};
    return true;
}
static bool hls_view_resident(const void *opaque,const MediaMp4Sample *sample)
{
    const HlsTrackView *v=opaque;size_t at=hls_view_head(v);
    return sample&&at!=SIZE_MAX&&v->source->queue[at].identity==sample->offset;
}
static bool hls_view_read(void *opaque,const MediaMp4Sample *sample,void *out,size_t capacity)
{
    HlsTrackView *v=opaque;size_t at=hls_view_head(v);
    return at!=SIZE_MAX&&hls_read_queued(v->source,at,sample,out,capacity);
}
static bool hls_view_error(const void *opaque,char *error,size_t capacity)
{return hls_last_error(((const HlsTrackView*)opaque)->source,error,capacity);}
static bool hls_view_blocked(const void *opaque)
{
    const HlsTrackView *v=opaque;
    return !v->source->failed&&!v->source->ended&&hls_view_head(v)==SIZE_MAX;
}
static bool hls_view_seek(void *opaque,uint64_t us,uint64_t *actual)
{return hls_seek_common(((HlsTrackView*)opaque)->source,us,false,actual);}
static bool hls_view_after(void *opaque,uint64_t us,uint64_t *actual)
{return hls_seek_common(((HlsTrackView*)opaque)->source,us,true,actual);}
static void hls_view_rewind(void *opaque)
{hls_rewind(((HlsTrackView*)opaque)->source);}
static size_t hls_view_bytes(const void *opaque)
{
    const HlsTrackView *v=opaque;
    return v->kind==MEDIA_MP4_TRACK_VIDEO?hls_retained_bytes(v->source):0;
}
static const MediaSampleSourceOps hls_view_ops={
    .track_count=hls_view_count,.track_info=hls_view_info,.next_sample=hls_view_next,
    .last_error=hls_view_error,.would_block=hls_view_blocked,
    .sample_resident=hls_view_resident,.read_sample_waiting=hls_view_read,
    .read_sample=hls_view_read,.seek_us=hls_view_seek,.seek_after_us=hls_view_after,
    .rewind=hls_view_rewind,.retained_bytes=hls_view_bytes
};
bool media_hls_source_track_views(MediaHlsSource *s,MediaSampleSource *video,MediaSampleSource *audio)
{
    if(!s||!video||!audio||s->playlist->init_url[0]!='\0'
       ||s->track_selection!=MEDIA_HLS_TRACK_MIXED||!s->video_info_valid||!s->audio_info_valid)return false;
    s->views[0]=(HlsTrackView){s,MEDIA_MP4_TRACK_VIDEO};
    s->views[1]=(HlsTrackView){s,MEDIA_MP4_TRACK_AUDIO};
    *video=(MediaSampleSource){&s->views[0],&hls_view_ops};
    *audio=(MediaSampleSource){&s->views[1],&hls_view_ops};
    return true;
}
