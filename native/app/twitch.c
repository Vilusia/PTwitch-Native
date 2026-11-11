/* PTwitch protocol handling. No credentials, JS challenges or helper servers.
 * Bounds and parsing are shared unchanged by PSP and freestanding host tests. */
#include "twitch.h"
#include <stdint.h>

static size_t len(const char *s) { size_t n = 0; while (s[n]) ++n; return n; }
static bool eq(const char *s, size_t n, const char *t)
{ size_t m = len(t); if (m != n) return false; for (size_t i=0;i<n;++i) if(s[i]!=t[i]) return false; return true; }
static bool prefix(const char *s, const char *p)
{ while (*p) { if (*s++ != *p++) return false; } return true; }
static bool copy(char *out, size_t cap, const char *s, size_t n)
{ if (n >= cap) return false; for(size_t i=0;i<n;++i) out[i]=s[i]; out[n]=0; return true; }
static bool namechar(char c)
{ return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'; }
bool tw_channel(const char *input, char output[TW_CHANNEL_SIZE])
{
    output[0]=0;
    if(!input) return false;
    while(*input==' '||*input=='\t') ++input;
    if(prefix(input,"https://www.twitch.tv/")) input+=22;
    else if(prefix(input,"https://twitch.tv/")) input+=18;
    else if(prefix(input,"twitch.tv/")) input+=10;
    size_t n=0;
    while(namechar(input[n])) { if(n>=TW_CHANNEL_SIZE-1) return false; ++n; }
    if(!n) return false;
    const char *end=input+n;
    if(*end=='/') ++end;
    while(*end==' '||*end=='\t'||*end=='\r'||*end=='\n') ++end;
    if(*end) return false;
    for(size_t i=0;i<n;++i) output[i]=(input[i]>='A'&&input[i]<='Z')?input[i]+32:input[i];
    output[n]=0; return true;
}

typedef struct { char *s; size_t cap,n; bool ok; } Writer;
static void put(Writer *w,char c)
{ if(w->n+1>=w->cap) {w->ok=false;return;} w->s[w->n++]=c; w->s[w->n]=0; }
static void text(Writer *w,const char *s) {while(*s) put(w,*s++);}
static void escaped_url(Writer *w,const char *s)
{
    const char hex[]="0123456789ABCDEF";
    while(*s) { unsigned char c=(unsigned char)*s++;
        if(namechar((char)c)||c=='-'||c=='.'||c=='~') put(w,(char)c);
        else {put(w,'%');put(w,hex[c>>4]);put(w,hex[c&15]);}
    }
}
bool tw_request(const char *channel,char *out,size_t capacity)
{
    char normalized[TW_CHANNEL_SIZE];
    if(!capacity||!tw_channel(channel,normalized)) return false;
    out[0]=0; Writer w={out,capacity,0,true};
    text(&w,"{\"operationName\":\"PlaybackAccessToken\",\"variables\":{\"isLive\":true,\"login\":\"");
    text(&w,normalized);
    text(&w,"\",\"isVod\":false,\"vodID\":\"\",\"playerType\":\"embed\",\"platform\":\"site\"},\"extensions\":{\"persistedQuery\":{\"version\":1,\"sha256Hash\":\"ed230aa1e33e07eebb8928504583da78a5173989fadfb1ac94be06a04f3cdbe9\"}}}");
    return w.ok;
}

/* Strict bounded JSON tokenizer. next skips an entire nested value. */
typedef struct {size_t start,end; unsigned next; char type;} Token;
typedef struct {const char *s;size_t n,p;unsigned count;Token tokens[192];} Json;
static void ws(Json *j) {while(j->p<j->n&&(j->s[j->p]==' '||j->s[j->p]=='\t'||j->s[j->p]=='\r'||j->s[j->p]=='\n')) ++j->p;}
static int hexval(char c)
{if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return -1;}
static bool value(Json *j,unsigned depth)
{
    ws(j); if(depth>16||j->p>=j->n||j->count>=192)return false;
    unsigned t=j->count++; Token *token=&j->tokens[t];
    token->start=j->p; char c=j->s[j->p++]; token->type=c;
    if(c=='"') {
        bool closed=false;
        while(j->p<j->n) {
            unsigned char ch=(unsigned char)j->s[j->p++];
            if(ch=='"'){closed=true;break;} if(ch<32)return false;
            if(ch=='\\') {
                if(j->p>=j->n)return false; char e=j->s[j->p++];
                if(e=='u') { for(unsigned k=0;k<4;++k) if(j->p>=j->n||hexval(j->s[j->p++])<0)return false; }
                else if(e!='"'&&e!='\\'&&e!='/'&&e!='b'&&e!='f'&&e!='n'&&e!='r'&&e!='t')return false;
            }
        }
        if(!closed)return false;
    } else if(c=='{'||c=='[') {
        ws(j); char close=c=='{'?'}':']';
        if(j->p<j->n&&j->s[j->p]==close)++j->p;
        else for(;;) {
            ws(j); if(c=='{'&&(j->p>=j->n||j->s[j->p]!='"'))return false;
            if(!value(j,depth+1))return false;
            if(c=='{') {ws(j);if(j->p>=j->n||j->s[j->p++]!=':')return false; if(!value(j,depth+1))return false;}
            ws(j); if(j->p>=j->n)return false;
            char delimiter=j->s[j->p++]; if(delimiter==close)break; if(delimiter!=',')return false;
        }
    } else {
        while(j->p<j->n&&j->s[j->p]!=','&&j->s[j->p]!=']'&&j->s[j->p]!='}'&&j->s[j->p]!=' '&&j->s[j->p]!='\r'&&j->s[j->p]!='\n'&&j->s[j->p]!='\t')++j->p;
        size_t n=j->p-token->start; const char *s=j->s+token->start;
        if(!eq(s,n,"true")&&!eq(s,n,"false")&&!eq(s,n,"null")) {
            size_t p=0; if(p<n&&s[p]=='-')++p; if(p>=n)return false;
            if(s[p]=='0')++p; else {if(s[p]<'1'||s[p]>'9')return false;while(p<n&&s[p]>='0'&&s[p]<='9')++p;}
            if(p<n&&s[p]=='.'){++p;size_t first=p;while(p<n&&s[p]>='0'&&s[p]<='9')++p;if(p==first)return false;}
            if(p<n&&(s[p]=='e'||s[p]=='E')){++p;if(p<n&&(s[p]=='+'||s[p]=='-'))++p;size_t first=p;while(p<n&&s[p]>='0'&&s[p]<='9')++p;if(p==first)return false;}
            if(p!=n)return false;
        }
    }
    token->end=j->p; token->next=j->count; return true;
}
static int member(const Json *j,int object,const char *key)
{
    if(object<0||j->tokens[object].type!='{')return -1;
    int found=-1;
    for(unsigned i=(unsigned)object+1;i<j->tokens[object].next;) {
        const Token *t=&j->tokens[i]; unsigned v=i+1;
        if(eq(j->s+t->start+1,t->end-t->start-2,key)) {if(found>=0)return -1;found=(int)v;}
        i=j->tokens[v].next;
    }
    return found;
}
static bool string(const Json *j,int index,char *out,size_t capacity)
{
    if(index<0||j->tokens[index].type!='"'||!capacity)return false;
    const Token *t=&j->tokens[index]; Writer w={out,capacity,0,true};out[0]=0;
    for(size_t p=t->start+1;p<t->end-1;++p) {
        unsigned char c=(unsigned char)j->s[p];
        if(c=='\\') {
            c=(unsigned char)j->s[++p];
            if(c=='u') {
                unsigned u=0; for(unsigned k=0;k<4;++k)u=(u<<4)|(unsigned)hexval(j->s[++p]);
                /* Playback tokens are ASCII JSON; refuse embedded NUL,
                   Unicode and surrogate escapes rather than corrupt them. */
                if(u<32||u>126)return false; c=(unsigned char)u;
            } else if(c=='n'||c=='r'||c=='t'||c=='b'||c=='f')return false;
        }
        if(c<32)return false;put(&w,(char)c);
    }
    return w.ok&&w.n>0;
}
TwResult tw_token_url(const char *channel,const char *json,size_t length,char out[TW_MASTER_SIZE])
{
    out[0]=0; char normalized[TW_CHANNEL_SIZE];
    if(!json||!length||length>TW_BODY_LIMIT||!tw_channel(channel,normalized))return TW_INVALID;
    Json j={0}; j.s=json;j.n=length;
    if(!value(&j,0))return TW_INVALID;ws(&j);if(j.p!=j.n||j.tokens[0].type!='{')return TW_INVALID;
    int errors=member(&j,0,"errors");
    if(errors>=0&&j.tokens[errors].type=='['&&j.tokens[errors].next>(unsigned)errors+1)return TW_DENIED;
    int data=member(&j,0,"data"), token=member(&j,data,"streamPlaybackAccessToken");
    if(token<0)return TW_INVALID;
    if(eq(j.s+j.tokens[token].start,j.tokens[token].end-j.tokens[token].start,"null"))return TW_OFFLINE;
    char signature[129],payload[4096];
    if(!string(&j,member(&j,token,"signature"),signature,sizeof signature)||!string(&j,member(&j,token,"value"),payload,sizeof payload))return TW_INVALID;
    for(size_t i=0;signature[i];++i)if(hexval(signature[i])<0)return TW_INVALID;
    Writer w={out,TW_MASTER_SIZE,0,true};
    text(&w,"https://usher.ttvnw.net/api/v2/channel/hls/");text(&w,normalized);
    text(&w,".m3u8?platform=web&allow_source=true&allow_audio_only=false&playlist_include_framerate=true&supported_codecs=h264&sig=");
    escaped_url(&w,signature);text(&w,"&token=");escaped_url(&w,payload);
    if(!w.ok){out[0]=0;return TW_INVALID;}return TW_OK;
}
bool tw_cdn_url(const char *url)
{
    if(!prefix(url,"https://"))return false;
    const char *h=url+8;size_t n=0;
    while(h[n]&&h[n]!='/') {char c=h[n];if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='-'))return false;++n;}
    if(!n||h[n]!='/')return false;
    const char *domains[]={"ttvnw.net","twitch.tv","twitchcdn.net"};
    bool admitted=false;
    for(unsigned i=0;i<3;++i){size_t d=len(domains[i]);if(n>=d&&eq(h+n-d,d,domains[i])&&(n==d||h[n-d-1]=='.'))admitted=true;}
    if(!admitted)return false;
    for(const char *p=h+n;*p;++p)if((unsigned char)*p<33||(unsigned char)*p>126||*p=='\\'||*p=='#')return false;
    return len(url)<TW_MASTER_SIZE;
}

/* HLS attribute scanner respects quoted CODECS lists containing commas. */
static bool attribute(const char *line,const char *key,char *out,size_t cap)
{
    while(*line) {
        while(*line==','||*line==' ')++line;
        const char *name=line;while(*line&&*line!='=')++line;if(!*line)return false;
        size_t n=(size_t)(line-name);++line;bool quoted=*line=='"';if(quoted)++line;
        const char *start=line;while(*line&&(quoted?*line!='"':*line!=','))++line;
        size_t length=(size_t)(line-start);if(quoted&&*line!='"')return false;
        if(eq(name,n,key))return copy(out,cap,start,length);
        if(quoted)++line;
        if(*line&&*line!=',')return false;
    }
    return false;
}
static bool number(const char **p,unsigned *out)
{
    unsigned n=0;const char *s=*p;if(*s<'0'||*s>'9')return false;
    while(*s>='0'&&*s<='9'){if(n>100000000u)return false;n=n*10u+(unsigned)(*s++-'0');}
    *p=s;*out=n;return true;
}
static bool compatible_codecs(const char *s)
{
    bool video=false,audio=false;
    while(*s){while(*s==' ')++s;const char *start=s;while(*s&&*s!=',')++s;size_t n=(size_t)(s-start);
        if(n==11&&(prefix(start,"avc1.")||prefix(start,"avc3."))) {
            for(size_t i=5;i<n;++i)if(hexval(start[i])<0)return false;
            unsigned profile=(unsigned)(hexval(start[5])*16+hexval(start[6]));
            unsigned level=(unsigned)(hexval(start[9])*16+hexval(start[10]));
            if((profile!=66&&profile!=77)||level>31)return false; video=true;
        } else if(eq(start,n,"mp4a.40.2"))audio=true;else return false;
        if(*s)++s;
    }
    return video&&audio;
}
TwResult tw_variant_excluding(const char *playlist,size_t length,TwVariant *out,
                             const char *const *excluded,size_t count)
{
    *out=(TwVariant){0};if(count>8||(!excluded&&count)||!playlist||length>TW_BODY_LIMIT||length<7||!eq(playlist,7,"#EXTM3U"))return TW_INVALID;
    if(length>7&&playlist[7]!='\r'&&playlist[7]!='\n')return TW_INVALID;
    for(size_t i=0;i<length;++i)if((unsigned char)playlist[i]<32&&playlist[i]!='\r'&&playlist[i]!='\n'&&playlist[i]!='\t')return TW_INVALID;
    bool pending=false,eligible=false;TwVariant candidate={0};
    size_t pos=0;char line[TW_URL_SIZE],a[256];
    while(pos<length) {
        size_t start=pos;while(pos<length&&playlist[pos]!='\n'&&playlist[pos]!='\r')++pos;
        size_t n=pos-start;while(pos<length&&(playlist[pos]=='\n'||playlist[pos]=='\r'))++pos;
        if(!n)continue;if(!copy(line,sizeof line,playlist+start,n))return TW_INVALID;
        if(prefix(line,"#EXT-X-STREAM-INF:")) {
            if(pending)return TW_INVALID;
            pending=true;eligible=false;candidate=(TwVariant){0};
            const char *attrs=line+18;const char *p;
            if(!attribute(attrs,"RESOLUTION",a,sizeof a))continue;p=a;
            if(!number(&p,&candidate.width)||*p++!='x'||!number(&p,&candidate.height)||*p)continue;
            if(!candidate.width||!candidate.height||candidate.width>640||candidate.height>360)continue;
            if(!attribute(attrs,"CODECS",a,sizeof a)||!compatible_codecs(a))continue;
            if(!attribute(attrs,"BANDWIDTH",a,sizeof a))continue;p=a;
            if(!number(&p,&candidate.bandwidth)||*p||!candidate.bandwidth||candidate.bandwidth>1800000u)continue;
            candidate.fps_milli=30000;
            if(attribute(attrs,"FRAME-RATE",a,sizeof a)) {
                p=a;unsigned whole=0,frac=0,scale=100;
                if(!number(&p,&whole)||whole>30)continue;
                if(*p=='.'){++p;const char *first=p;while(*p>='0'&&*p<='9'){if(scale){frac+=(unsigned)(*p-'0')*scale;scale/=10;}++p;}if(p==first)continue;}
                if(*p)continue;candidate.fps_milli=whole*1000+frac;if(!candidate.fps_milli||candidate.fps_milli>30000)continue;
            }
            eligible=true;
        } else if(line[0]!='#') {
            if(!pending)return TW_INVALID;
            pending=false;
            if(!eligible)continue;
            if(!tw_cdn_url(line))return TW_INVALID;
            bool skip=false;
            for(size_t i=0;i<count;++i)if(eq(line,n,excluded[i]))skip=true;
            if(skip)continue;
            /* Prefer the highest compatible rendition up to 360p. If the PSP
               refuses its first AVC access unit, the caller retries below
               that height; do not silently prefer 160p over an offered 360p. */
            if(!out->height||candidate.height>out->height||
               (candidate.height==out->height&&candidate.bandwidth<out->bandwidth)) {
                if(!copy(candidate.url,sizeof candidate.url,line,n))return TW_INVALID;*out=candidate;
            }
        }
    }
    if(pending)return TW_INVALID;
    return out->height?TW_OK:TW_UNSUPPORTED;
}
TwResult tw_variant(const char *playlist,size_t length,TwVariant *out)
{return tw_variant_excluding(playlist,length,out,NULL,0);}
/* Inspect whole tags, never URL substrings. Initialization maps are retained
   for the engine's live MP4 adapter; byte-range resources remain unsupported. */
TwResult tw_media_format(const char *playlist,size_t length)
{
    if(!playlist||length<8||length>TW_BODY_LIMIT||!eq(playlist,7,"#EXTM3U")||
       (playlist[7]!='\r'&&playlist[7]!='\n'))return TW_INVALID;
    bool segment=false;
    for(size_t pos=0;pos<length;){
        size_t start=pos;while(pos<length&&playlist[pos]!='\r'&&playlist[pos]!='\n'){
            if((unsigned char)playlist[pos]<32&&playlist[pos]!='\t')return TW_INVALID;
            ++pos;
        }
        size_t n=pos-start;while(pos<length&&(playlist[pos]=='\r'||playlist[pos]=='\n'))++pos;
        if(n>=17&&eq(playlist+start,17,"#EXT-X-BYTERANGE:"))return TW_UNSUPPORTED;
        if(n>=11&&eq(playlist+start,11,"#EXT-X-MAP:")){
            char map[TW_URL_SIZE];
            if(!copy(map,sizeof map,playlist+start,n))return TW_INVALID;
            char uri[TW_URL_SIZE],range[128];
            if(!attribute(map+11,"URI",uri,sizeof uri))return TW_INVALID;
            if(attribute(map+11,"BYTERANGE",range,sizeof range))return TW_UNSUPPORTED;
        }
        if(n>=8&&eq(playlist+start,8,"#EXTINF:"))segment=true;
    }
    return segment?TW_OK:TW_INVALID;
}
const char *tw_message(TwResult result)
{
    switch(result){case TW_OK:return "Ready";case TW_OFFLINE:return "Channel is offline or unavailable";
    case TW_DENIED:return "Twitch refused playback access";case TW_UNSUPPORTED:return "No compatible 360p/240p H.264 stream";
    default:return "Unexpected Twitch response";}
}
