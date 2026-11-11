/* Isolated Windows host harness: real HLS/MP4/TS/URL code, fake bounded
   transport and a quota allocator in place of the full browser/PSP runtime. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "tilefinch/media_hls.h"
#include "ptwitch_hls_codec.h"
#include "media_backend_psp_policy.h"
#include "ptwitch_hls_buffer_policy.h"
#include "../engine/ptwitch_avc_packet.h"

int hls_ts_packet_prepare(unsigned char *data,size_t n,size_t *out)
{return ptwitch_ts_avcc_picture(data,n,out);}
int hls_ts_packet_test(void)
{
    unsigned char packet[]={0,0,0,2,9,0xf0,0,0,0,2,7,1,0,0,0,2,8,2,
        0,0,0,2,6,3,0,0,0,2,0x65,4,0,0,0,2,0x65,5};
    const unsigned char expected[]={0,0,0,2,7,1,0,0,0,2,8,2,
        0,0,0,2,0x65,4,0,0,0,2,0x65,5};
    size_t n=0;
    if(!ptwitch_ts_avcc_picture(packet,sizeof(packet),&n)
       ||n!=sizeof(expected)||memcmp(packet,expected,n))return 1;
    unsigned char malformed[]={0,0,0,2,9,0xf0,0,0,0,8,0x65};
    unsigned char saved[sizeof(malformed)];memcpy(saved,malformed,sizeof(saved));
    if(ptwitch_ts_avcc_picture(malformed,sizeof(malformed),&n)
       ||memcmp(saved,malformed,sizeof(saved)))return 2;
    unsigned char no_picture[]={0,0,0,2,9,0xf0};
    if(ptwitch_ts_avcc_picture(no_picture,sizeof(no_picture),&n))return 3;
    unsigned char zero[]={0,0,0,0};
    if(ptwitch_ts_avcc_picture(zero,sizeof(zero),&n))return 4;
    return 0;
}

int __stdio_common_vsprintf(uint64_t,char *,size_t,const char *,void *,va_list);
int __stdio_common_vfprintf(uint64_t,FILE *,const char *,void *,va_list);
int fprintf(FILE *out,const char *format,...){
    va_list args;va_start(args,format);int r=__stdio_common_vfprintf(0,out,format,NULL,args);va_end(args);return r;
}
static char snapshot_root[512];
void ptwitch_test_snapshot_dir(const char *path){snprintf(snapshot_root,sizeof(snapshot_root),"%s",path);}
void komi_sibling_path(char *out,size_t n,const char *name){snprintf(out,n,"%s/%s",snapshot_root,name);}
void komi_result(const char *format,...){(void)format;}
int printf(const char *format,...){(void)format;return 0;}
int vsnprintf(char *out,size_t n,const char *format,va_list args){
    return __stdio_common_vsprintf(2,out,n,format,NULL,args);
}
int snprintf(char *out,size_t n,const char *format,...){
    va_list args;va_start(args,format);int result=vsnprintf(out,n,format,args);va_end(args);return result;
}

void budget_init(Budget *b,size_t limit){memset(b,0,sizeof(*b));b->limit=limit;}
void *budget_malloc_category(Budget *b,BudgetCategory category,size_t n){
    (void)category;
    if(n>b->limit-b->current||n>SIZE_MAX-sizeof(size_t))return NULL;
    size_t *p=malloc(n+sizeof(size_t));if(!p)return NULL;
    *p=n;b->current+=n;if(b->current>b->peak)b->peak=b->current;
    return p+1;
}
void budget_free(Budget *b,void *p){if(p){size_t *header=(size_t*)p-1;b->current-=*header;free(header);}}
void *budget_calloc_category(Budget *b,BudgetCategory c,size_t count,size_t size){
    if(size&&count>SIZE_MAX/size)return NULL;
    void *p=budget_malloc_category(b,c,count*size);if(p)memset(p,0,count*size);return p;
}

typedef struct {const unsigned char *bytes;size_t length,offset;bool active,wait;} Request;
typedef struct {const unsigned char *init,*first,*second;size_t ni,nf,ns,slice,initializations,early_prefetch;Request r[4];} Transport;
static uint64_t start(void *opaque,const char *url,size_t maximum,char *error,size_t cap){
    Transport *t=opaque;const unsigned char *bytes=NULL;size_t n=0;
    if(strstr(url,"init.mp4")){bytes=t->init;n=t->ni;t->initializations++;}
    else if(strstr(url,"0.mp4")){bytes=t->first;n=t->nf;}
    else if(strstr(url,"1.mp4")){bytes=t->second;n=t->ns;}
    if(bytes==t->second&&t->initializations){
        for(size_t i=0;i<4;++i)if(t->r[i].active&&t->r[i].bytes==t->first)t->early_prefetch++;
    }
    if(!bytes||n>maximum){snprintf(error,cap,"test resource unavailable/too large");return 0;}
    for(size_t i=0;i<4;++i)if(!t->r[i].active){t->r[i]=(Request){bytes,n,0,true,false};return i+1;}
    return 0;
}
static MediaHlsTransportPollResult poll(void *opaque,uint64_t handle,unsigned char *out,size_t cap,size_t *n,char *error,size_t ec){
    (void)error;(void)ec;Transport *t=opaque;*n=0;
    if(!handle||handle>4||!t->r[handle-1].active)return MEDIA_HLS_TRANSPORT_ERROR;
    Request *r=&t->r[handle-1];
    /* SIZE_MAX models a published worker chunk that can be drained in
       multiple small takes; other fixtures deliberately alternate WAIT. */
    if(t->slice!=SIZE_MAX){
        if(r->wait){r->wait=false;return MEDIA_HLS_TRANSPORT_WAIT;}r->wait=true;
    }
    if(r->offset==r->length){r->active=false;return MEDIA_HLS_TRANSPORT_COMPLETE;}
    *n=r->length-r->offset;if(*n>cap)*n=cap;if(*n>t->slice)*n=t->slice;
    memcpy(out,r->bytes+r->offset,*n);r->offset+=*n;return MEDIA_HLS_TRANSPORT_CHUNK;
}
static void cancel(void *opaque,uint64_t h){Transport *t=opaque;if(h&&h<=4)t->r[h-1].active=false;}

typedef struct {uint64_t dts,pts;uint32_t duration,kind,size;uint32_t byte;} Seen;
typedef struct {size_t count,peak,leaked,completed,active,initializations,early_prefetch;uint64_t paused_horizon;size_t paused_active,paused_resumable,steps;int failed;char error[256];Seen seen[1024];} Result;

int hls_test(const char *playlist,size_t np,const unsigned char *init,size_t ni,
             const unsigned char *first,size_t nf,const unsigned char *second,size_t ns,
             size_t slice,size_t quota,const char *refresh,size_t nr,size_t cancel_after,
             size_t pause_after,size_t pause_steps,Result *out)
{
    memset(out,0,sizeof(*out));Budget b;budget_init(&b,quota);
    Transport t={.init=init,.first=first,.second=second,.ni=ni,.nf=nf,.ns=ns,.slice=slice};
    MediaHlsTransport transport={&t,start,poll,cancel};
    MediaHlsPlaylistStream *capture=media_hls_playlist_stream_create(&b,"https://example.ttvnw.net/live.m3u8",out->error,sizeof(out->error));
    MediaHlsPlaylist *p=NULL;MediaHlsSource *s=NULL;
    unsigned char *payload=malloc(512u*1024u);
    if(!payload){snprintf(out->error,sizeof(out->error),"test payload allocation failed");goto done;}
    if(!capture)goto done;
    for(size_t i=0;i<np;){size_t n=np-i;if(n>slice)n=slice;
        if(!media_hls_playlist_stream_feed(capture,(const unsigned char*)playlist+i,n,out->error,sizeof(out->error)))goto done;
        i+=n;
    }
    p=media_hls_playlist_stream_finish(capture,out->error,sizeof(out->error));
    media_hls_playlist_stream_destroy(capture);capture=NULL;
    if(!p)goto done;
    s=media_hls_source_create(&b,p,&transport,out->error,sizeof(out->error));
    if(!s)goto done;p=NULL;
    bool primed=false;size_t paused=0;
    for(unsigned step=0;step<200000;++step){
        if(cancel_after&&step==cancel_after)break;
        if(!primed){
            MediaHlsPrimeStatus status=media_hls_source_prime(s,out->error,sizeof(out->error));
            if(status==MEDIA_HLS_PRIME_FAILED)break;
            primed=status==MEDIA_HLS_PRIME_READY;
            if(!primed)continue;
            MediaMp4TrackInfo video={0},audio={0};uint8_t profile=0;unsigned constraints=0,level=0;
            if(!media_hls_source_stream_info(s,&video,&audio)
                ||ptwitch_hls_video_codec(&video,&profile,&constraints,&level)!=MEDIA_H264_DECODER_ROUTE_PSP_FIRMWARE
                ||(profile!=66&&profile!=77)||level!=30){snprintf(out->error,sizeof(out->error),"HLS open codec-route mismatch");break;}
            if(video.packet_format==MEDIA_PACKET_FORMAT_CONTAINER_NATIVE&&constraints!=video.codec_config[2]){
                snprintf(out->error,sizeof(out->error),"AVCC constraint flags were lost");break;
            }
            if(video.packet_format==MEDIA_PACKET_FORMAT_CONTAINER_NATIVE
                &&media_h264_annexb_decoder_route(video.codec_config,video.codec_config_length,NULL)!=MEDIA_H264_DECODER_ROUTE_UNSUPPORTED){
                snprintf(out->error,sizeof(out->error),"fixture must expose old Annex-B routing failure");break;
            }
        }
        out->steps=step+1;
        media_hls_source_pump(s,step*16667u);
        if(pause_steps&&out->count>=pause_after&&paused<pause_steps){
            /* Model the player holding its clock/decode pump while buffering.
               Delivery and manifest refresh must keep advancing independently. */
            if(refresh){
                MediaHlsPlaylist *next=media_hls_playlist_parse(&b,"https://example.ttvnw.net/live.m3u8",(const unsigned char*)refresh,nr,out->error,sizeof(out->error));
                refresh=NULL;
                if(!next||!media_hls_source_update_playlist(s,next,step*16667u,out->error,sizeof(out->error)))break;
            }
            MediaHlsStats held={0};media_hls_source_stats(s,&held);
            out->paused_horizon=held.buffered_until_us;out->paused_active=held.active_requests;
            uint64_t clock=out->seen[out->count-1].dts*1000000u/90000u;
            PspMediaBufferPolicyDecision decision=psp_media_buffer_policy((PspMediaBufferPolicyInput){
                .playing=true,.buffering=true,.buffer_events=1,.now_us=(uint64_t)step*16667u,
                .ready_since_us=1,.decoded_ahead_us=100000u,
                .network_ahead_us=held.buffered_until_us>clock?held.buffered_until_us-clock:0,
                .network_target_cap_us=ptwitch_hls_resume_cap(
                    held.buffered_until_us>clock?held.buffered_until_us-clock:0,
                    held.active_requests!=0&&!held.delivery_backpressured,PSP_MEDIA_BUFFER_TARGET_US),
                .fill_pending=held.active_requests!=0});
            out->paused_resumable=decision.action==PSP_MEDIA_BUFFER_END;
            paused++;continue;
        }
        MediaSampleSource samples={0};media_hls_source_sample_source(s,&samples);
        MediaMp4Sample sample={0};
        if(samples.ops->next_sample(samples.opaque,&sample)){
            if(out->count>=1024||!samples.ops->read_sample(samples.opaque,&sample,payload,512u*1024u)){snprintf(out->error,sizeof(out->error),"sample read failed");break;}
            out->seen[out->count++]=(Seen){sample.dts,(uint64_t)sample.pts,sample.duration,sample.kind,sample.size,sample.size?payload[sample.size-1]:0};
        }
        MediaHlsStats stats={0};media_hls_source_stats(s,&stats);
        if(refresh&&stats.segments_completed>=1){
            MediaHlsPlaylist *next=media_hls_playlist_parse(&b,"https://example.ttvnw.net/live.m3u8",(const unsigned char*)refresh,nr,out->error,sizeof(out->error));
            refresh=NULL;
            if(!next||!media_hls_source_update_playlist(s,next,step*16667u,out->error,sizeof(out->error)))break;
        }
        if(media_hls_source_failed(s)){
            samples.ops->last_error(samples.opaque,out->error,sizeof(out->error));break;
        }
        if(stats.ended&&stats.queued_samples==0){out->completed=stats.segments_completed;break;}
        if(step==199999)snprintf(out->error,sizeof(out->error),"test pump did not finish");
    }
done:
    free(payload);
    media_hls_playlist_stream_destroy(capture);media_hls_playlist_destroy(p);media_hls_source_destroy(s);
    out->peak=b.peak;out->leaked=b.current;out->failed=out->error[0]!=0;
    out->initializations=t.initializations;
    out->early_prefetch=t.early_prefetch;
    for(size_t i=0;i<4;++i)out->active+=t.r[i].active;
    return out->failed;
}

int hls_repeat_resume_test(void)
{
    PspMediaBufferPolicyInput input={.playing=true,.buffering=true,.buffer_events=4,
        .now_us=2000000,.ready_since_us=1,.decoded_ahead_us=100000,
        .network_ahead_us=1500000,.fill_pending=false};
    /* The old three-second repeat target could never be reached by this
       completed fragment tail while the source was backpressured. */
    if(psp_media_buffer_policy(input).action==PSP_MEDIA_BUFFER_END)return 1;
    input.network_target_cap_us=ptwitch_hls_resume_cap(input.network_ahead_us,
        input.fill_pending,PSP_MEDIA_BUFFER_TARGET_US);
    return psp_media_buffer_policy(input).action==PSP_MEDIA_BUFFER_END?0:2;
}
