/* Compile the actual HLS poll function with deterministic worker responses.
   The generated include is extracted from the engine by test-hls.py. */
#include <stdio.h>
#include <string.h>
#include "tilefinch/media_hls.h"
#include "tilefinch/fetch.h"
#include "tilefinch/session.h"
typedef struct { uint64_t background_id; size_t consumed; bool headers_admitted;
    char url[4096]; } PspHlsRequest;
typedef struct { Budget *budget; BrowserSession *session;
    TilefinchRequestMode mode; TilefinchCredentialsMode credentials;
    char document_url[4096]; long last_transport_code;
    bool last_transport_retryable; PspHlsRequest request; } PspMediaHlsContext;
static bool complete, headers, chunk, final_success;
static unsigned retired, chunks;
static FetchResult result_storage;
static PspHlsRequest *psp_hls_request(PspMediaHlsContext *c,uint64_t id)
{ return c->request.background_id==id?&c->request:NULL; }
static bool psp_hls_admit_headers(PspMediaHlsContext *c,PspHlsRequest *r,char *e,size_t n)
{ (void)c;(void)e;(void)n;return r->headers_admitted=headers; }
bool fetch_background_transport_progress(uint64_t id,FetchBackgroundProgress *p)
{ (void)id;p->complete=complete;return true; }
bool fetch_background_transport_take_chunk(uint64_t id,unsigned char *out,size_t n,size_t *len)
{ (void)id;chunks++;if(!chunk||!n)return false;*out=42;*len=1;chunk=false;return true; }
FetchResult *fetch_result_create(Budget *b)
{ (void)b;memset(&result_storage,0,sizeof(result_storage));return &result_storage; }
void fetch_result_free(FetchResult *r){(void)r;}
bool fetch_background_transport_take_fetch_result_consumed(uint64_t id,Budget *b,FetchResult *r,size_t n)
{ (void)id;(void)b;(void)n;retired++;r->transport_code=28;r->timed_out=true;
  snprintf(r->error,sizeof(r->error),"connection timed out before headers");return final_success; }
bool fetch_background_transport_cancel(uint64_t id,const char *why)
{(void)id;(void)why;return true;}
bool browser_session_cookie_set_http_context(BrowserSession *s,const TilefinchRequestContext *c,const char *v)
{(void)s;(void)c;(void)v;return true;}
#include "ptwitch_hls_poll.inc"
static bool transport_blocked,transport_fresh_next;
static unsigned pressed,back_button=1;
static char status[192];
static uint64_t now,release_at;
static bool cancel_cleanup;
bool fetch_background_transport_metrics(FetchBackgroundTransportMetrics *m)
{memset(m,0,sizeof(*m));return true;}
static uint64_t komi_now_us(void){return now;}
static void komi_result(const char *format,...){(void)format;}
static void komi_progress(const char *stage){(void)stage;}
static void input(void){pressed=cancel_cleanup?back_button:0;}
static void reset_input(void){pressed=0;}
static void busy(const char *s){(void)s;now+=100000;}
static void fetch_background_transport_cooperate(uint64_t time){(void)time;}
static size_t cleanup_active(void){return now<release_at?1:0;}
static bool cleanup_quiesced(void){return now>=release_at;}
static void cleanup_priority(bool on){(void)on;}
static void cleanup_quiesce(void){}
#define fetch_background_transport_active_operations cleanup_active
#define fetch_background_transport_is_quiesced cleanup_quiesced
#define fetch_background_transport_set_media_priority cleanup_priority
#define fetch_background_transport_request_quiesce cleanup_quiesce
#include "ptwitch_cleanup.inc"
#define CHECK(x) do {if(!(x))return __LINE__;}while(0)
int hls_transport_test(void)
{
    PspMediaHlsContext c={0};unsigned char out[8];size_t n=0;char error[192]={0};
    c.request.background_id=1;complete=false;headers=false;chunk=false;final_success=false;retired=chunks=0;
    CHECK(psp_hls_poll(&c,1,out,sizeof(out),&n,error,sizeof(error))==MEDIA_HLS_TRANSPORT_WAIT);
    CHECK(retired==0&&chunks==0);
    /* A completed pre-header failure must retire and expose its transport error. */
    complete=true;
    CHECK(psp_hls_poll(&c,1,out,sizeof(out),&n,error,sizeof(error))==MEDIA_HLS_TRANSPORT_ERROR);
    CHECK(retired==1&&chunks==0&&c.request.background_id==0&&c.last_transport_retryable);
    CHECK(strstr(error,"connection timed out")!=NULL);
    /* Never publish an unadmitted body, even if the worker claims success. */
    c.request.background_id=1;final_success=true;chunk=true;
    CHECK(psp_hls_poll(&c,1,out,sizeof(out),&n,error,sizeof(error))==MEDIA_HLS_TRANSPORT_ERROR);
    CHECK(chunks==0&&n==0);
    /* Admitted bodies still drain before their completion is collected. */
    c.request.background_id=1;headers=true;
    CHECK(psp_hls_poll(&c,1,out,sizeof(out),&n,error,sizeof(error))==MEDIA_HLS_TRANSPORT_CHUNK);
    CHECK(n==1&&out[0]==42&&c.request.consumed==1);
    CHECK(psp_hls_poll(&c,1,out,sizeof(out),&n,error,sizeof(error))==MEDIA_HLS_TRANSPORT_COMPLETE);
    /* Slow cancellation must finish without forcing a process restart. */
    now=0;release_at=4200000;cancel_cleanup=false;
    CHECK(retire_transport()&&!transport_blocked&&transport_fresh_next);
    /* A truly stuck worker stays protected and the wait remains bounded. */
    now=0;release_at=UINT64_MAX;transport_fresh_next=false;
    CHECK(!retire_transport()&&transport_blocked&&now<=15000000);
    /* A later attempt can finish the outstanding retirement safely. */
    release_at=now+100000;
    CHECK(retire_transport()&&!transport_blocked);
    now=0;release_at=200000;cancel_cleanup=true;
    CHECK(!retire_transport()&&!transport_blocked);
    return 0;
}
