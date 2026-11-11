#include "twitch.h"
/* Freestanding tests run the real C implementation on the host. */
#define CHECK(x) do {if(!(x))return __LINE__;}while(0)
static int same(const char *a,const char *b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
static int contains(const char *s,const char *needle){for(;*s;++s){const char *a=s,*b=needle;while(*a&&*a==*b){++a;++b;}if(!*b)return 1;}return 0;}
static size_t length(const char *s){size_t n=0;while(s[n])++n;return n;}
static TwResult variant(const char *s,TwVariant *v){return tw_variant(s,length(s),v);}
static TwResult token(const char *s,char *url){return tw_token_url("Test_Channel",s,length(s),url);}
#define HEADER "#EXTM3U\n"
#define V360 "#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360,CODECS=\"avc1.4D401E,mp4a.40.2\",FRAME-RATE=30.000\n"
#define V240 "#EXT-X-STREAM-INF:BANDWIDTH=400000,RESOLUTION=426x240,CODECS=\"avc1.42E01E,mp4a.40.2\",FRAME-RATE=29.970\n"
#define V160 "#EXT-X-STREAM-INF:BANDWIDTH=230000,RESOLUTION=284x160,CODECS=\"avc1.4D400C,mp4a.40.2\",FRAME-RATE=30.000\n"
#define CDN "https://video-weaver.example.hls.ttvnw.net/v1/playlist/test.m3u8"
#define TOKEN "{\"data\":{\"streamPlaybackAccessToken\":{\"signature\":\"abcdef0123456789\",\"value\":\"{\\\"channel\\\":\\\"test_channel\\\",\\\"x\\\":\\\"a&b+c%\\\"}\"}}}"
int test_core(void)
{
    char channel[TW_CHANNEL_SIZE],url[TW_MASTER_SIZE],request[1024];TwVariant v;
    CHECK(tw_channel("Test_Channel",channel)&&same(channel,"test_channel"));
    CHECK(tw_channel(" https://www.twitch.tv/Test_Channel/ ",channel)&&same(channel,"test_channel"));
    CHECK(tw_channel("https://twitch.tv/a",channel)&&same(channel,"a"));
    CHECK(tw_channel("twitch.tv/test",channel));
    CHECK(!tw_channel("https://evil.test/twitch.tv/name",channel));
    CHECK(!tw_channel("test/other",channel));CHECK(!tw_channel("name?x=1",channel));
    CHECK(!tw_channel("",channel));CHECK(!tw_channel("a\"b",channel));
    CHECK(!tw_channel("abcdefghijklmnopqrstuvwxyz",channel));
    CHECK(tw_request("Test_Channel",request,sizeof request));
    CHECK(contains(request,"\"login\":\"test_channel\""));CHECK(contains(request,"PlaybackAccessToken"));
    CHECK(!tw_request("name",request,10));CHECK(!tw_request("bad\"",request,sizeof request));
    CHECK(token(TOKEN,url)==TW_OK);CHECK(contains(url,"/api/v2/channel/hls/test_channel.m3u8?"));
    CHECK(contains(url,"%7B%22channel%22%3A%22test_channel%22"));
    CHECK(contains(url,"a%26b%2Bc%25"));CHECK(!contains(url,"&b+"));
    CHECK(token("{\"data\":{\"streamPlaybackAccessToken\":null}}",url)==TW_OFFLINE);
    CHECK(token("{\"errors\":[{\"message\":\"integrity required\"}]}",url)==TW_DENIED);
    CHECK(token("{\"data\":null}",url)==TW_INVALID);
    CHECK(token("{\"value\":\"fake\",\"signature\":\"abcdef\"}",url)==TW_INVALID);
    CHECK(token(TOKEN "x",url)==TW_INVALID);CHECK(token("{bad}",url)==TW_INVALID);
    CHECK(token("{\"data\":{\"streamPlaybackAccessToken\":{\"signature\":\"abcd\",\"value\":\"\\u0000\"}}}",url)==TW_INVALID);
    CHECK(token("{\"data\":{\"streamPlaybackAccessToken\":{\"signature\":\"abcd\",\"value\":\"\\u007b\\u007d\"}}}",url)==TW_OK);
    CHECK(token("{\"data\":{\"streamPlaybackAccessToken\":{\"signature\":\"abc?\",\"value\":\"x\"}}}",url)==TW_INVALID);
    CHECK(token("{\"data\":{\"streamPlaybackAccessToken\":{\"signature\":\"abcd\",\"value\":\"x\",\"value\":\"y\"}}}",url)==TW_INVALID);
    CHECK(token("{\"data\":{\"streamPlaybackAccessToken\":null},}",url)==TW_INVALID);
    CHECK(tw_cdn_url(CDN));CHECK(tw_cdn_url("https://usher.ttvnw.net/a"));
    CHECK(!tw_cdn_url("https://ttvnw.net.evil.test/a"));CHECK(!tw_cdn_url("https://evilttvnw.net/a"));
    CHECK(!tw_cdn_url("https://user@ttvnw.net/a"));CHECK(!tw_cdn_url("http://ttvnw.net/a"));
    CHECK(!tw_cdn_url("https://127.0.0.1/a"));CHECK(!tw_cdn_url("https://ttvnw.net:443/a"));
    CHECK(!tw_cdn_url("https://ttvnw.net/a\nInjected"));
    CHECK(variant(HEADER V360 CDN "\n",&v)==TW_OK&&v.height==360&&v.fps_milli==30000);
    CHECK(variant(HEADER V240 CDN "?240\n" V360 CDN "?360\n",&v)==TW_OK&&v.height==360);
    CHECK(contains(v.url,"?360"));
    CHECK(variant(HEADER V160 CDN "?160\n" V360 CDN "?360\n",&v)==TW_OK&&v.height==360);
    CHECK(variant(HEADER V240 CDN "\r\n",&v)==TW_OK&&v.fps_milli==29970);
    const char *excluded[]={CDN "?360",CDN "?240"};
    CHECK(tw_variant_excluding(HEADER V240 CDN "?240\n" V360 CDN "?360\n",length(HEADER V240 CDN "?240\n" V360 CDN "?360\n"),&v,excluded,1)==TW_OK&&v.height==240);
    CHECK(tw_variant_excluding(HEADER V240 CDN "?240\n" V360 CDN "?360\n",length(HEADER V240 CDN "?240\n" V360 CDN "?360\n"),&v,excluded,2)==TW_UNSUPPORTED);
    CHECK(tw_media_format(HEADER "#EXTINF:2,\nsegment.ts\n",length(HEADER "#EXTINF:2,\nsegment.ts\n"))==TW_OK);
    CHECK(tw_media_format(HEADER "#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:2,\na.mp4\n",length(HEADER "#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:2,\na.mp4\n"))==TW_OK);
    CHECK(tw_media_format(HEADER "#EXT-X-MAP:URI=\"init.mp4\",BYTERANGE=\"500@0\"\n#EXTINF:2,\na.mp4\n",length(HEADER "#EXT-X-MAP:URI=\"init.mp4\",BYTERANGE=\"500@0\"\n#EXTINF:2,\na.mp4\n"))==TW_UNSUPPORTED);
    CHECK(tw_media_format(HEADER "#EXT-X-BYTERANGE:200@0\n#EXTINF:2,\na.ts\n",length(HEADER "#EXT-X-BYTERANGE:200@0\n#EXTINF:2,\na.ts\n"))==TW_UNSUPPORTED);
    CHECK(tw_media_format(HEADER,length(HEADER))==TW_INVALID);
    CHECK(variant(HEADER "#EXT-X-STREAM-INF:BANDWIDTH=3000000,RESOLUTION=1280x720,CODECS=\"avc1.4D401F,mp4a.40.2\"\n" CDN "\n",&v)==TW_UNSUPPORTED);
    CHECK(variant(HEADER "#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360,CODECS=\"avc1.64001E,mp4a.40.2\"\n" CDN "\n",&v)==TW_UNSUPPORTED);
    CHECK(variant(HEADER "#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360,CODECS=\"hvc1.1.6.L93.B0,mp4a.40.2\"\n" CDN "\n",&v)==TW_UNSUPPORTED);
    CHECK(variant(HEADER "#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360,CODECS=\"avc1.4D401E,mp4a.40.2\",FRAME-RATE=60\n" CDN "\n",&v)==TW_UNSUPPORTED);
    CHECK(variant(HEADER "#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360,CODECS=\"avc1.4D401E,mp4a.40.2\",FRAME-RATE=30.001\n" CDN "\n",&v)==TW_UNSUPPORTED);
    CHECK(variant(HEADER "#EXT-X-STREAM-INF:BANDWIDTH=800000,CODECS=\"avc1.4D401E,mp4a.40.2\"\n" CDN "\n",&v)==TW_UNSUPPORTED);
    CHECK(variant(HEADER V360 "https://evil.test/a.m3u8\n",&v)==TW_INVALID);
    CHECK(variant(HEADER V360 "relative.m3u8\n",&v)==TW_INVALID);
    CHECK(variant(HEADER V360,&v)==TW_INVALID);CHECK(variant("<html>error</html>",&v)==TW_INVALID);
    CHECK(variant("#EXTM3UBAD\n" V360 CDN "\n",&v)==TW_INVALID);
    CHECK(tw_variant(HEADER V360 CDN "\0bad\n",sizeof(HEADER V360 CDN "\0bad\n")-1,&v)==TW_INVALID);
    CHECK(variant(HEADER V360 CDN "\n#EXT-X-STREAM-INF:BANDWIDTH=600000,RESOLUTION=640x360,CODECS=\"avc1.4D401E,mp4a.40.2\"\n" CDN "?lower\n",&v)==TW_OK&&v.bandwidth==600000);
    /* Exercise every truncated prefix and malformed byte; must terminate and
       never parse a partial token as success. */
    for(size_t n=0;n<length(TOKEN);++n)CHECK(tw_token_url("name",TOKEN,n,url)!=TW_OK);
    CHECK(tw_token_url("name",TOKEN,TW_BODY_LIMIT+1,url)==TW_INVALID);
    return 0;
}
