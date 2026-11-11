/* PTwitch Native: standalone Twitch channel player for 64 MB PSP models.
 * Uses PTube's KOMI/Tilefinch runtime and firmware video/audio decoder. */
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
#include <psputility.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "komi_runtime.h"
#include "../common/ptube_guard.h"
#include "../common/ptube_power.h"
#include "../common/ptwitch_playback_policy.h"
#include "tilefinch/fetch.h"
#include "tilefinch/psp_threads.h"
#include "tilefinch/psp_media_hls.h"
#include "osk.h"
#include "ui.h"
#include "twitch.h"
#include "version.h"

PSP_MODULE_INFO("PTwitchNative", PSP_MODULE_USER, 0, PTWITCH_BUILD);
PSP_MAIN_THREAD_PRIORITY(TILEFINCH_PSP_THREAD_PRIORITY_BROWSER);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(2560);
PSP_HEAP_SIZE_KB(-1);
PSP_HEAP_THRESHOLD_SIZE_KB(2048);
#define W 480
#define H 272
#define FAVORITES 32
static Budget ui_budget;
static uint32_t last_buttons=~0u,pressed;
static uint32_t ok_button=PSP_CTRL_CROSS,back_button=PSP_CTRL_CIRCLE;
static const char *ok_icon=UI_ICON_CROSS,*back_icon=UI_ICON_CIRCLE;
static char favorites[FAVORITES][TW_CHANNEL_SIZE];
static size_t favorite_count;
static char last_channel[TW_CHANNEL_SIZE];
static char status[192];
static char response[TW_BODY_LIMIT+1];
static uint64_t generation;
static unsigned power_mode;
static bool transport_blocked,transport_fresh_next;
static const char *power_names[]={"Performance","Balanced","Battery saver"};

static void input(void)
{
    SceCtrlData pad; sceCtrlPeekBufferPositive(&pad,1);
    pressed=pad.Buttons&~last_buttons;last_buttons=pad.Buttons;
}
static void reset_input(void){last_buttons=~0u;pressed=0;}
static void power(bool playback,bool busy)
{
    static int applied;
    int cpu=ptube_power_cpu(power_mode,playback,busy);
    if(cpu!=applied&&scePowerSetClockFrequency(cpu,cpu,cpu/2)>=0)applied=cpu;
}
static void battery(uint16_t *vram)
{
    static uint64_t checked;static int percent=-1,ac;
    uint64_t now=komi_now_us();
    if(!checked||now-checked>=5000000u){checked=now;percent=scePowerGetBatteryLifePercent();ac=scePowerIsPowerOnline()>0;}
    char label[24];
    if(percent>=0&&percent<=100)snprintf(label,sizeof label,"%s%d%%",ac?"AC ":"",percent);
    else snprintf(label,sizeof label,"%s",ac?"AC":"--");
    ui_text(vram,W-80,6,W-4,label,16,UI_TEXT_MUTED);
}
static uint16_t *frame(const char *title,const char *hint)
{
    uint16_t *vram=psp_display_back_buffer(&komi.display);
    if(!vram)return NULL;
    ui_fill(vram,0,0,W,H,UI_BACKGROUND);ui_fill(vram,0,0,W,29,UI_BAR);
    ui_fill(vram,0,29,W,2,UI_ACCENT);ui_text(vram,10,6,W-85,title,16,UI_TEXT);battery(vram);
    ui_fill(vram,0,H-27,W,27,UI_BAR);ui_text(vram,8,H-21,W-5,hint,16,UI_TEXT_MUTED);
    return vram;
}
static void publish(void)
{
    (void)psp_display_publish(&komi.display);
    sceDisplayWaitVblankStart();fetch_background_transport_cooperate(komi_now_us());
}
static void message(const char *title,const char *detail)
{
    reset_input();
    for(;;){komi_progress("message");input();if(pressed&(back_button|ok_button|PSP_CTRL_START))break;
        uint16_t *vram=frame("PTwitch", "Press confirm or Back to return");
        if(vram){ui_text(vram,16,95,W-16,title,16,UI_TEXT);if(detail)ui_text(vram,16,127,W-16,detail,16,UI_TEXT_MUTED);publish();}
    }
    reset_input();
}
static void busy(const char *label)
{
    uint16_t *vram=frame("PTwitch", "Back: Cancel");
    if(vram){ui_text(vram,16,110,W-16,label,16,UI_TEXT);publish();}
}
/* Standalone app owns every transport lease. Cancel, then observe retirement
   without killing a worker that still owns curl/budget state. */
static bool retire_transport(void)
{
    komi_result("network cleanup begin active=%u",(unsigned)fetch_background_transport_active_operations());
    fetch_background_transport_set_media_priority(false);
    fetch_background_transport_request_quiesce();
    /* Cancellation can arrive while DNS/TLS is still unwinding. Allow more
       than the request's 10-second connect window before returning to menu. */
    uint64_t started=komi_now_us(),next_report=3000000u;bool cancelled=false;
    while(!fetch_background_transport_is_quiesced()&&komi_now_us()-started<15000000u){
        komi_progress("network-cleanup");input();
        if(pressed&back_button)cancelled=true;
        busy("Closing old stream connections...");
        fetch_background_transport_cooperate(komi_now_us());
        uint64_t elapsed=komi_now_us()-started;
        if(elapsed>=next_report){
            komi_result("network cleanup waiting elapsed-ms=%u active=%u",
                (unsigned)(elapsed/1000u),(unsigned)fetch_background_transport_active_operations());
            FetchBackgroundTransportMetrics metrics={0};
            if(fetch_background_transport_metrics(&metrics))
                komi_result("cleanup worker queued=%u running=%u complete=%u perform=%u poll=%u retired=%u",
                    (unsigned)metrics.queued_slots,(unsigned)metrics.running_slots,
                    (unsigned)metrics.complete_slots,(unsigned)metrics.worker_performs,
                    (unsigned)metrics.worker_polls,(unsigned)metrics.cancelled_retired);
            next_report+=3000000u;
        }
    }
    transport_blocked=!fetch_background_transport_is_quiesced();
    transport_fresh_next=true;
    komi_result("network cleanup retired=%d active=%u",!transport_blocked,
        (unsigned)fetch_background_transport_active_operations());
    if(transport_blocked)snprintf(status,sizeof status,"Connection still closing. Try the channel again.");
    else if(cancelled)snprintf(status,sizeof status,"Cancelled");
    reset_input();return !transport_blocked&&!cancelled;
}
static int favorite_index(const char *channel)
{for(size_t i=0;i<favorite_count;++i)if(!strcmp(favorites[i],channel))return (int)i;return -1;}
static void favorite_load(void)
{
    char path[256];komi_sibling_path(path,sizeof path,"ptwitch-favorites.txt");FILE *f=fopen(path,"r");
    if(!f){komi_sibling_path(path,sizeof path,"ptwitch-favorites.bak");f=fopen(path,"r");}
    if(!f)return;char line[128],channel[TW_CHANNEL_SIZE];
    while(favorite_count<FAVORITES&&fgets(line,sizeof line,f)) {
        if(!strchr(line,'\n')&&!feof(f)){int c;while((c=fgetc(f))!=EOF&&c!='\n'){}continue;}
        if(tw_channel(line,channel)&&favorite_index(channel)<0)snprintf(favorites[favorite_count++],TW_CHANNEL_SIZE,"%s",channel);
    }
    fclose(f);
}
static bool favorite_save(void)
{
    char path[256],tmp[256],bak[256];
    komi_sibling_path(path,sizeof path,"ptwitch-favorites.txt");komi_sibling_path(tmp,sizeof tmp,"ptwitch-favorites.tmp");komi_sibling_path(bak,sizeof bak,"ptwitch-favorites.bak");
    FILE *f=fopen(tmp,"w");if(!f)return false;bool good=true;
    for(size_t i=0;i<favorite_count;++i)if(fprintf(f,"%s\n",favorites[i])<0)good=false;
    if(fclose(f)!=0)good=false;
    if(!good){remove(tmp);return false;}
    /* Keep the previous complete list if interrupted during replacement. */
    FILE *existing=fopen(path,"r");bool have=existing!=NULL;if(existing)fclose(existing);
    if(have){remove(bak);if(rename(path,bak)!=0){remove(tmp);return false;}}
    if(rename(tmp,path)!=0){if(have)rename(bak,path);remove(tmp);return false;}
    return true;
}
static void toggle_favorite(const char *channel)
{
    char saved[FAVORITES][TW_CHANNEL_SIZE];memcpy(saved,favorites,sizeof saved);size_t count=favorite_count;
    int i=favorite_index(channel);
    if(i>=0){memmove(favorites[i],favorites[i+1],(favorite_count-(size_t)i-1)*TW_CHANNEL_SIZE);--favorite_count;}
    else {if(favorite_count==FAVORITES){snprintf(status,sizeof status,"Favorites full (32 channels)");return;}snprintf(favorites[favorite_count++],TW_CHANNEL_SIZE,"%s",channel);}
    if(!favorite_save()){memcpy(favorites,saved,sizeof saved);favorite_count=count;snprintf(status,sizeof status,"Could not save favorites");}
    else snprintf(status,sizeof status,"%s %s",i>=0?"Removed":"Saved",channel);
}

/* Worker owns a snapshot of request fields. Never blocks the UI on DNS/TLS.
   Bound each request independently of heartbeat progress; cancelled worker
   descriptors auto-retire. Signed response bodies and URLs are never logged. */
static bool request_body_once(const char *url,const char *body,const char *label,size_t *length,long *http,bool *retryable)
{
    *retryable=false;
    if(transport_blocked){
        if(!retire_transport())return false;
    }
    *length=0;*http=0;response[0]=0;
    FetchRequest request={
        .method=body?"POST":"GET",.body=body,.body_length=body?strlen(body):0,
        .content_type=body?"application/json":NULL,
        .extra_headers=body?"Client-ID: " TW_CLIENT_ID:NULL,
        .accept=body?"application/json":"application/vnd.apple.mpegurl",
        .origin="https://www.twitch.tv",.referer="https://www.twitch.tv/",
        .user_agent="Mozilla/5.0 PTwitch/0.1",
        .credentials=FETCH_CREDENTIALS_OMIT,.identity_encoding=true,
        .allow_http_errors=true,.block_private_network=true,
        .connect_timeout_ms=10000,.redirect_same_origin_only=true,
        .force_fresh_connection=transport_fresh_next,
        .redirect_url_validator=tw_cdn_url
    };
    if(!tw_cdn_url(url)){snprintf(status,sizeof status,"Invalid Twitch endpoint");return false;}
    komi_result("request begin stage=%s method=%s",label,request.method);
    /* PTube's YouTube scheduler starts this worker implicitly. PTwitch calls
       the worker directly and must initialize it with the runtime's budget.
       The fixed-response enqueue is GET-only and rejects POST/custom headers;
       the bounded streaming lane supports Twitch's GraphQL request. */
    if(!fetch_background_transport_initialize(&komi.budget)){
        komi_result("twitch transport initialization failed available=%d",fetch_background_transport_available());
        snprintf(status,sizeof status,"Could not initialize the network worker");return false;
    }
    komi_result("network worker initialized");
    FetchBackgroundEnqueueStatus admission=FETCH_BACKGROUND_ENQUEUE_UNAVAILABLE;
    uint64_t id=fetch_background_transport_enqueue_stream_diagnosed(url,&request,TW_BODY_LIMIT,25000,&admission);
    if(!id){
        komi_result("twitch request refused admission=%d method=%s body-bytes=%u",(int)admission,request.method,(unsigned)request.body_length);
        const char *reason=admission==FETCH_BACKGROUND_ENQUEUE_UNSUPPORTED?"Twitch request format was rejected":
            admission==FETCH_BACKGROUND_ENQUEUE_ADMISSION_CLOSED?"Network is not ready for requests":
            admission==FETCH_BACKGROUND_ENQUEUE_SATURATED?"Network worker is busy. Try again":
            admission==FETCH_BACKGROUND_ENQUEUE_MEMORY?"Not enough memory for a network request":"Network worker unavailable";
        snprintf(status,sizeof status,"%s",reason);return false;
    }
    komi_result("request queued stage=%s",label);
    transport_fresh_next=false;
    uint64_t started=komi_now_us();bool success=false;reset_input();ptube_operation_begin(32);power(false,true);
    for(;;){
        komi_progress("twitch-request");input();
        if(pressed&back_button){snprintf(status,sizeof status,"Cancelled");break;}
        if(komi_now_us()-started>=28000000u){
            *retryable=true;snprintf(status,sizeof status,"Twitch connection timed out");
            komi_result("request timeout stage=%s bytes=%u active=%u",label,(unsigned)*length,
                (unsigned)fetch_background_transport_active_operations());break;
        }
        /* Drain published chunks even before completion, so a response above
           the worker's publication threshold cannot deadlock its producer.
           One 16 KiB read per frame keeps input/cancellation responsive. */
        size_t chunk=0,remaining=TW_BODY_LIMIT-*length;
        size_t capacity=remaining>16384u?16384u:remaining;
        if(capacity&&fetch_background_transport_take_chunk(id,(unsigned char *)response+*length,capacity,&chunk)){
            if(chunk>capacity){snprintf(status,sizeof status,"Invalid network response length");break;}
            *length+=chunk;response[*length]=0;
        }
        FetchBackgroundProgress progress={0};
        if(!fetch_background_transport_progress(id,&progress)){snprintf(status,sizeof status,"Network request disappeared");break;}
        if(*length==TW_BODY_LIMIT&&progress.available_body_bytes){snprintf(status,sizeof status,"Twitch response exceeds the size limit");break;}
        if(progress.complete){
            if(progress.available_body_bytes){busy(label);continue;}
            FetchBackgroundMediaResponse result={0};
            bool taken=fetch_background_transport_take_media_result_consumed(id,&result,*length);
            *http=result.status_code;
            if(taken&&result.success&&result.length==*length){success=true;}
            else {
                *retryable=taken&&result.status_code==0;
                if(result.status_code==0)snprintf(status,sizeof status,"Connection failed before Twitch responded");
                else snprintf(status,sizeof status,"Twitch request failed (HTTP %ld)",result.status_code);
                komi_result("twitch transport failure timeout=%d detail=%.160s",result.timed_out,result.error);
            }
            komi_result("twitch request http=%ld bytes=%u success=%d",*http,(unsigned)*length,success);
            id=0;break;
        }
        busy(label);
    }
    if(id)fetch_background_transport_cancel(id,"PTwitch request cancelled");
    if(!success&&*http==0&&!retire_transport())*retryable=false;
    ptube_operation_end();power(false,false);reset_input();return success;
}
static bool request_body(const char *url,const char *body,const char *label,size_t *length,long *http)
{
    for(unsigned attempt=0;attempt<2;++attempt){
        bool retryable=false;
        if(request_body_once(url,body,label,length,http,&retryable))return true;
        if(!retryable||attempt!=0)return false;
        komi_result("retrying pre-response connection failure once stage=%s",label);
        uint64_t until=komi_now_us()+500000u;
        while(komi_now_us()<until){
            komi_progress("connection-retry");input();
            if(pressed&back_button){snprintf(status,sizeof status,"Cancelled");reset_input();return false;}
            busy("Retrying connection...");
        }
    }
    return false;
}
static bool resolve(const char *channel,TwVariant *variant,unsigned maximum_height)
{
    char body[1024],master[TW_MASTER_SIZE];size_t length;long http;
    if(!tw_request(channel,body,sizeof body)){snprintf(status,sizeof status,"Invalid channel name");return false;}
    if(!request_body("https://gql.twitch.tv/gql",body,"Contacting Twitch...",&length,&http))return false;
    if(http==401||http==403){snprintf(status,sizeof status,"Twitch requires browser verification");return false;}
    if(http!=200){snprintf(status,sizeof status,"Twitch API returned HTTP %ld",http);return false;}
    TwResult result=tw_token_url(channel,response,length,master);
    memset(response,0,length);
    if(result!=TW_OK){snprintf(status,sizeof status,"%s",tw_message(result));return false;}
    bool fetched=request_body(master,NULL,"Finding a PSP-compatible stream...",&length,&http);
    memset(master,0,sizeof master);
    if(!fetched)return false;
    if(http==404){snprintf(status,sizeof status,"Channel is offline or unavailable");return false;}
    if(http!=200){snprintf(status,sizeof status,"Twitch playlist refused (HTTP %ld)",http);return false;}
    /* request_body overwrites response; retain the master while probing at
       most eight distinct renditions in quality order. No segment downloads. */
    static char playlist[TW_BODY_LIMIT+1],rejected[8][TW_URL_SIZE];
    const char *excluded[8];size_t master_length=length;
    memcpy(playlist,response,length);playlist[length]=0;
    bool ready=false;
    bool transport_fallback_valid=false;
    TwVariant transport_fallback={0};
    for(size_t attempt=0;attempt<8;++attempt){
        result=tw_variant_excluding(playlist,master_length,variant,excluded,attempt);
        if(result!=TW_OK){
            if(transport_fallback_valid)break;
            snprintf(status,sizeof status,"%s",maximum_height<360?"No lower compatible quality after decoder refusal":attempt?"Channel has no supported HLS stream":tw_message(result));break;
        }
        if(variant->height>maximum_height){
            snprintf(rejected[attempt],sizeof rejected[attempt],"%s",variant->url);
            excluded[attempt]=rejected[attempt];
            snprintf(status,sizeof status,"No lower compatible quality after decoder refusal");continue;
        }
        /* Variants are considered highest-resolution first. Prefer fMP4 over
           TS at the same resolution, but do not trade an offered 360p TS
           rendition for a lower-resolution fMP4 one. A firmware refusal then
           re-enters resolve() with the failed height excluded. */
        if(transport_fallback_valid&&variant->height<transport_fallback.height)
            break;
        if(!request_body(variant->url,NULL,"Checking stream container...",&length,&http))break;
        if(http!=200){snprintf(status,sizeof status,"Stream playlist refused (HTTP %ld)",http);break;}
        result=tw_media_format(response,length);
        bool fragmented_mp4=strstr(response,"#EXT-X-MAP:")!=NULL;
        komi_result("container probe rendition=%up result=%d format=%s",variant->height,(int)result,
            fragmented_mp4?"fragmented-mp4":"mpeg-ts/adts");
        if(result==TW_OK&&fragmented_mp4){ready=true;break;}
        if(result==TW_OK){
            /* The PSP has demonstrated reliable startup with Twitch fMP4,
               while TS remains useful where it is the only compatible
               container at the preferred resolution. */
            if(!transport_fallback_valid){
                transport_fallback=*variant;
                transport_fallback_valid=true;
            }
        }else if(result!=TW_UNSUPPORTED){
            snprintf(status,sizeof status,"Unexpected stream playlist");break;
        }
        snprintf(rejected[attempt],sizeof rejected[attempt],"%s",variant->url);
        excluded[attempt]=rejected[attempt];
        if(!transport_fallback_valid)
            snprintf(status,sizeof status,"Channel has no supported HLS stream");
    }
    if(!ready&&transport_fallback_valid){
        *variant=transport_fallback;
        ready=true;
        status[0]=0;
        komi_result("using compatible MPEG-TS fallback rendition=%up",variant->height);
    }
    memset(playlist,0,master_length);memset(rejected,0,sizeof rejected);memset(response,0,length);
    if(!ready)return false;
    komi_result("rendition %ux%u bandwidth=%u fps-milli=%u",variant->width,variant->height,variant->bandwidth,variant->fps_milli);
    return true;
}
typedef struct {const char *channel;const TwVariant *variant;uint64_t overlay_until;bool failed;char error[192];} Player;
static void report_decoder(void)
{
    if(!komi.media.playback)return;
    MediaPlaybackJobStats job={0};MediaBackendStats backend={0};
    media_playback_job_stats(komi.media.playback,&job);
    komi_result("decoder progress clock-us=%llu buffered-us=%llu packets=%u horizon=%u submit-block=%u source-block=%u pending=%d phase=%d audio-bypass=%u",
        (unsigned long long)komi.media.clock_us,
        (unsigned long long)media_playback_buffered_until_us(komi.media.playback),
        (unsigned)job.packets_submitted,(unsigned)job.horizon_breaks,
        (unsigned)job.submit_block_calls,(unsigned)job.source_block_calls,
        komi.media.decode_job_pending,(int)komi.media.job_phase,(unsigned)job.head_alt_submitted);
    if(media_playback_backend_stats(komi.media.playback,&backend))
        komi_result("decoder backend video-packets=%u audio-packets=%u frames=%u take-busy=%u take-early=%u native=0x%08x",
            (unsigned)backend.submitted_video_packets,(unsigned)backend.submitted_audio_packets,
            (unsigned)backend.decoded_video_frames,(unsigned)backend.video_take_busy,
            (unsigned)backend.video_take_early,(unsigned)backend.last_native_error);
}
static void overlay(uint16_t *vram,void *context)
{
    Player *p=context;const PspUiMediaState *ui=&komi.media.ui;
    bool loading=!komi.media.frame.pixels;
    if(komi_now_us()<p->overlay_until||loading||p->failed||ui->buffering||!ui->playing){
        ui_shade(vram,0,0,W,30);ui_text(vram,10,6,W-90,p->channel,16,UI_TEXT);battery(vram);
        ui_shade(vram,0,H-30,W,30);char hint[160];
        snprintf(hint,sizeof hint,"LIVE %up  %s Controls  SELECT Reconnect  %s Back",p->variant->height,ok_icon,back_icon);
        ui_text(vram,6,H-23,W-4,hint,16,UI_TEXT_MUTED);
        if(loading||p->failed||ui->buffering){
            ui_shade(vram,8,88,W-16,82);
            ui_text(vram,16,103,W-16,p->failed?p->error:ui->buffering?"Buffering live stream...":"Opening live stream...",16,UI_TEXT);
            ui_text(vram,16,132,W-16,p->failed?"SELECT: Retry   Back: Return":"Triangle: Save channel   Back: Cancel",16,UI_TEXT_MUTED);
        }
    }
}
/* 0: menu, 1: reconnect, 2: lower-quality retry, 3: test higher quality,
   4: higher-quality test failed; restore the previous rendition. */
static int play_attempt(const char *channel,const TwVariant *variant,
                        bool monitor_lower_quality,uint64_t quality_check_delay_us,
                        bool quality_probe)
{
    Player view={.channel=channel,.variant=variant,.overlay_until=komi_now_us()+4000000u};
    KomiPlayback playback={0};char document[96];snprintf(document,sizeof document,"https://www.twitch.tv/%s",channel);
    ptube_operation_begin(65);power(true,true);
    bool accepted=psp_media_open_page_hls(&komi.media,variant->url,document,++generation,0,
        TILEFINCH_REQUEST_MODE_NO_CORS,TILEFINCH_CREDENTIALS_OMIT,true,true);
    komi_result("live open accepted=%d",accepted);
    if(!accepted){
        ptube_operation_end();
        if(quality_probe){komi_result("higher-quality probe rejected before open");return 4;}
        message("Player could not open the stream",NULL);return false;
    }
    reset_input();uint64_t started=komi_now_us(),starved=0,stable_since=0;
    bool opened=false;int retry=0;
    uint64_t next_report=started;
    for(;;){
        komi_progress("live-player");input();uint64_t now=komi_now_us();
        if(pressed&back_button)break;
        if(pressed&PSP_CTRL_SELECT){retry=true;break;}
        if(pressed) view.overlay_until=now+4000000u;
        if(pressed&PSP_CTRL_TRIANGLE){toggle_favorite(channel);view.overlay_until=now+4000000u;}
        const PspUiMediaState *ui=&komi.media.ui;
        if(now>=next_report){
            komi_result("live progress elapsed-ms=%u playing=%d buffering=%d failed=%d ended=%d status=%s",
                (unsigned)((now-started)/1000),ui->playing,ui->buffering,ui->failed,ui->ended,ui->status);
            {
                MediaHlsStats hls={0};
                if(psp_media_hls_stats(komi.media.hls,&hls))
                    komi_result("HLS delivery playlist=%u media=%u segments=%u/%u queued=%u active=%u worker-active=%u refresh=%u/%u ahead-ms=%u backpressured=%d",
                        (unsigned)hls.playlist_bytes_received,(unsigned)hls.bytes_received,
                        (unsigned)hls.segments_completed,(unsigned)hls.segments_started,
                        (unsigned)hls.queued_samples,hls.active_requests,
                        (unsigned)fetch_background_transport_active_operations(),
                        (unsigned)hls.playlist_refreshes,(unsigned)hls.playlist_refresh_failures,
                        (unsigned)(hls.buffered_until_us>komi.media.clock_us
                              ?(hls.buffered_until_us-komi.media.clock_us)/1000u:0),hls.delivery_backpressured);
            }
            next_report=now+5000000u;
            report_decoder();
            FetchBackgroundTransportMetrics metrics={0};
            if(fetch_background_transport_metrics(&metrics)){
                komi_result("network worker queued=%u running=%u complete=%u perform=%u poll=%u headers=%u body=%u finished=%u retired=%u",
                    (unsigned)metrics.queued_slots,(unsigned)metrics.running_slots,
                    (unsigned)metrics.complete_slots,(unsigned)metrics.worker_performs,
                    (unsigned)metrics.worker_polls,(unsigned)metrics.header_callbacks,
                    (unsigned)metrics.body_callbacks,(unsigned)metrics.completions,
                    (unsigned)metrics.cancelled_retired);
                komi_result("network worker timing setup-max-us=%u service-max-us=%u poll-max-us=%u multi=%d running=%d",
                    metrics.worker_setup_perform_max_us,metrics.worker_service_max_us,
                    metrics.worker_poll_max_us,metrics.last_multi_code,metrics.last_running);
            }
        }
        if(ui->playing&&komi.media.frame.pixels&&!ui->buffering){
            if(!opened){opened=true;ptube_operation_end();komi_result("first-frame channel=%s after-ms=%u",channel,(unsigned)((now-started)/1000));}
            starved=0;
            if((monitor_lower_quality||quality_probe)&&stable_since==0)
                stable_since=now;
            if(ptwitch_quality_recheck_due(monitor_lower_quality,
                    variant->height,stable_since,now,quality_check_delay_us)){
                komi_result("lower rendition %up stable for %ums; checking for higher quality",
                    variant->height,(unsigned)(quality_check_delay_us/1000u));
                retry=3;break;
            }
            if(quality_probe&&stable_since!=0&&now>=stable_since
               &&now-stable_since>=PTWITCH_QUALITY_RECHECK_STABLE_US){
                komi_result("higher rendition %up stable for 30s; keeping it",
                            variant->height);
                quality_probe=false;
            }
        }
        else {
            stable_since=0;
            if(opened&&!starved)starved=now;
        }
        if(quality_probe&&now>=started
           &&now-started>=PTWITCH_QUALITY_PROBE_WINDOW_US){
            komi_result("higher-quality probe exceeded %ums; restoring lower rendition",
                (unsigned)(PTWITCH_QUALITY_PROBE_WINDOW_US/1000u));
            retry=4;break;
        }
        if(!view.failed&&(ui->failed||ui->ended||(!opened&&now-started>60000000u)||(starved&&now-starved>45000000u))){
            view.failed=true;ptube_operation_end();
            snprintf(view.error,sizeof view.error,"%s",ui->failed?ui->status:ui->ended?"Stream ended":"Stream stalled. Reconnect to retry.");
            komi_result("live playback stopped failed=%d ended=%d",ui->failed,ui->ended);
            report_decoder();
            if(quality_probe){
                komi_result("higher-quality probe failed at %up; restoring prior rendition",
                            variant->height);
                retry=4;break;
            }
            bool avc_refused=ui->failed&&(strstr(ui->status,"AVC")
                ||strstr(ui->status,"H.264")||strstr(ui->status,"80628"));
            bool decoder_no_progress=ui->failed
                &&strstr(ui->status,"VIDEO DECODER MADE NO PROGRESS");
            if(ptwitch_decoder_fallback_allowed(variant->height,opened,
                avc_refused,decoder_no_progress)){
                komi_result("decoder %s at %up; trying lower quality once",
                    decoder_no_progress?"made no progress":"startup refused",
                    variant->height);
                retry=2;break;
            }
            /* Close failed HLS immediately, before displaying the error.
               Otherwise its network leases survive on the error screen. */
            break;
        }
        power(true,!ui->playing||ui->buffering);
        /* Live playback has no pause/seek: SELECT resolves a fresh live edge. */
        komi_playback_frame(&playback,overlay,&view,pressed!=0||now<view.overlay_until||view.failed||!ui->playing||ui->buffering);
    }
    komi_playback_close(&playback);power(false,false);reset_input();
    bool retired=retire_transport();
    if(!retired){message(status,"Return to the menu");return 0;}
    if(view.failed&&!retry){
        for(;;){
            komi_progress("playback-error");input();
            if(pressed&(back_button|ok_button|PSP_CTRL_START))break;
            if(pressed&PSP_CTRL_SELECT){retry=1;break;}
            uint16_t *vram=frame("PTwitch","SELECT: Retry   Back: Return");
            if(vram){ui_text(vram,16,95,W-16,view.error,16,UI_TEXT);publish();}
        }
    }
    reset_input();return retry;
}
static void watch(const char *channel)
{
    snprintf(last_channel,sizeof last_channel,"%s",channel);
    unsigned maximum_height=360;
    bool monitor_lower_quality=false;
    bool check_for_higher_quality=false;
    bool restore_saved_quality=false;
    unsigned quality_probe_retries=0;
    uint64_t quality_check_delay_us=PTWITCH_QUALITY_RECHECK_STABLE_US;
    TwVariant saved_lower_quality={0};
    for(;;){
        TwVariant variant={0};bool quality_probe=false;status[0]=0;
        if(restore_saved_quality){
            variant=saved_lower_quality;
            restore_saved_quality=false;
        }else if(check_for_higher_quality){
            check_for_higher_quality=false;
            TwVariant candidate={0};
            bool found=resolve(channel,&candidate,360);
            if(found&&candidate.height>saved_lower_quality.height){
                variant=candidate;
                quality_probe=true;
                maximum_height=360;
                komi_result("quality recovery probe lower=%up candidate=%up",
                    saved_lower_quality.height,candidate.height);
            }else{
                if(ptwitch_quality_retry_allowed(quality_probe_retries)){
                    quality_probe_retries++;
                    quality_check_delay_us=PTWITCH_QUALITY_RETRY_DELAY_US;
                    monitor_lower_quality=true;
                    komi_result("quality recovery found no higher rendition; retry %u/%u after 3min",
                        quality_probe_retries,PTWITCH_QUALITY_MAX_RETRIES);
                }else{
                    monitor_lower_quality=false;
                    komi_result("quality recovery exhausted %u retries; staying lower",
                        PTWITCH_QUALITY_MAX_RETRIES);
                }
                status[0]=0;
                variant=saved_lower_quality;
            }
        }else if(!resolve(channel,&variant,maximum_height)){
            message(status,"Back to the menu");break;
        }
        int retry=play_attempt(channel,&variant,monitor_lower_quality,
            quality_check_delay_us,quality_probe);
        if(retry==3){
            saved_lower_quality=variant;
            check_for_higher_quality=true;
            monitor_lower_quality=false;
            maximum_height=360;
            memset(&variant,0,sizeof variant);
            continue;
        }
        if(quality_probe&&retry==4){
            if(ptwitch_quality_retry_allowed(quality_probe_retries)){
                quality_probe_retries++;
                quality_check_delay_us=PTWITCH_QUALITY_RETRY_DELAY_US;
                monitor_lower_quality=true;
                komi_result("quality recovery failed; retry %u/%u after 3min at %up",
                    quality_probe_retries,PTWITCH_QUALITY_MAX_RETRIES,
                    saved_lower_quality.height);
            }else{
                monitor_lower_quality=false;
                komi_result("quality recovery exhausted %u retries; returning to %up",
                    PTWITCH_QUALITY_MAX_RETRIES,saved_lower_quality.height);
            }
            status[0]=0;
            maximum_height=saved_lower_quality.height;
            restore_saved_quality=true;
            memset(&variant,0,sizeof variant);
            continue;
        }
        if(retry==2){
            if(variant.height==360u){
                monitor_lower_quality=true;
                quality_probe_retries=0;
                quality_check_delay_us=PTWITCH_QUALITY_RECHECK_STABLE_US;
            }
            maximum_height=variant.height>0?variant.height-1u:360u;
            memset(&variant,0,sizeof variant);
            continue;
        }
        if(retry==1){
            maximum_height=360;
            monitor_lower_quality=false;
            check_for_higher_quality=false;
            restore_saved_quality=false;
            quality_probe_retries=0;
            quality_check_delay_us=PTWITCH_QUALITY_RECHECK_STABLE_US;
            memset(&variant,0,sizeof variant);
            continue;
        }
        memset(&variant,0,sizeof variant);
        if(!retry)break;
    }
}
static void ask_channel(bool save)
{
    char typed[128],channel[TW_CHANNEL_SIZE];
    if(!osk_input(save?"Add Twitch favorite":"Twitch channel name",last_channel,typed,sizeof typed))return;
    reset_input();if(!tw_channel(typed,channel)){message("Enter a Twitch channel name", "Example: thatpinklizard");return;}
    snprintf(last_channel,sizeof last_channel,"%s",channel);
    if(save){if(favorite_index(channel)<0)toggle_favorite(channel);else snprintf(status,sizeof status,"Already saved");}
    else watch(channel);
}
static void favorites_menu(void)
{
    int selected=0;reset_input();
    for(;;){komi_progress("favorites");input();if(pressed&back_button)break;
        if(!favorite_count)selected=0;
        else if(selected>=(int)favorite_count)selected=(int)favorite_count-1;
        if((pressed&PSP_CTRL_UP)&&selected>0)--selected;
        if((pressed&PSP_CTRL_DOWN)&&selected+1<(int)favorite_count)++selected;
        if(favorite_count){char channel[TW_CHANNEL_SIZE];snprintf(channel,sizeof channel,"%s",favorites[selected]);
            if(pressed&PSP_CTRL_TRIANGLE){toggle_favorite(channel);if(selected>=(int)favorite_count&&selected>0)--selected;}
            else if(pressed&ok_button)watch(channel);
        }
        uint16_t *vram=frame("PTwitch / Favorites","Confirm: Watch   Triangle: Remove   Back: Return");
        if(vram){if(!favorite_count)ui_text(vram,16,110,W-16,"Add a channel from the main menu",16,UI_TEXT_MUTED);
            int first=selected/5*5;
            for(int row=0;row<5&&first+row<(int)favorite_count;++row){int y=43+row*37;
                if(first+row==selected)ui_fill(vram,10,y-5,W-20,33,UI_ROW_SELECTED);
                ui_text(vram,20,y,W-20,favorites[first+row],16,UI_TEXT);
            }
            publish();
        }
    }
    reset_input();
}
int main(int argc,char **argv)
{
    komi_log_open(argc?argv[0]:NULL,"ptwitch.log");komi_result("%s",PTWITCH_BUILD_MARKER);
    komi_platform_init(false);sceCtrlSetSamplingCycle(0);sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);
    int assignment=0;
    if(sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_UNKNOWN,&assignment)>=0&&assignment==0){ok_button=PSP_CTRL_CIRCLE;back_button=PSP_CTRL_CROSS;ok_icon=UI_ICON_CIRCLE;back_icon=UI_ICON_CROSS;}
    ptube_set_back_button(back_button);budget_init(&ui_budget,4u*1024u*1024u);
    char path[256];komi_sibling_path(path,sizeof path,"fonts/TilefinchSans-Regular.ttf");ui_init(&ui_budget,path);
    favorite_load();komi_sibling_path(path,sizeof path,"ptwitch-power.txt");FILE *f=fopen(path,"r");
    if(f){unsigned value;if(fscanf(f,"%u",&value)==1&&value<3)power_mode=value;fclose(f);}
    busy("Connecting to saved Wi-Fi...");
    if(!komi_services_init()){message("Could not connect to Wi-Fi","Check WLAN and your saved PSP connection");goto quit;}
    komi_set_watchdog_seconds(90);reset_input();int selected=0;
    for(;;){komi_progress("menu");power(false,false);input();
        if(pressed&PSP_CTRL_START)break;
        if(pressed&PSP_CTRL_UP)selected=(selected+3)%4;
        if(pressed&PSP_CTRL_DOWN)selected=(selected+1)%4;
        if(pressed&ok_button){
            if(selected==0)ask_channel(false);else if(selected==1)ask_channel(true);else if(selected==2)favorites_menu();
            else {power_mode=(power_mode+1)%3;komi_sibling_path(path,sizeof path,"ptwitch-power.txt");f=fopen(path,"w");if(f){fprintf(f,"%u\n",power_mode);fclose(f);}}
        }
        uint16_t *vram=frame("PTwitch", "Up/Down: Choose   Confirm: Open   START: Exit");
        if(vram){const char *items[]={"Watch a Twitch channel","Add a favorite","Favorites",NULL};
            for(int row=0;row<4;++row){int y=43+row*39;char label[80];
                if(selected==row)ui_fill(vram,14,y-5,W-28,34,UI_ROW_SELECTED);
                if(row==2)snprintf(label,sizeof label,"Favorites (%u)",(unsigned)favorite_count);
                else if(row==3)snprintf(label,sizeof label,"Power: %s",power_names[power_mode]);else snprintf(label,sizeof label,"%s",items[row]);
                ui_text(vram,26,y,W-20,label,16,UI_TEXT);
            }
            ui_text(vram,16,214,W-16,status[0]?status:"Standalone streaming / experimental build",16,UI_TEXT_MUTED);publish();
        }
    }
quit:
    ptube_operation_begin(15);psp_media_shutdown(&komi.media);fetch_background_transport_shutdown(3000);
    komi_log_close("quit");sceKernelExitGame();return 0;
}
