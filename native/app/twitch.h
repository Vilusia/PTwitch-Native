#ifndef PTWITCH_TWITCH_H
#define PTWITCH_TWITCH_H
#include <stdbool.h>
#include <stddef.h>
#define TW_CHANNEL_SIZE 26
#define TW_URL_SIZE 2048
#define TW_MASTER_SIZE 4096
#define TW_BODY_LIMIT (64u * 1024u)
#define TW_CLIENT_ID "kimne78kx3ncx6brgo4mv6wki5h1ko"
typedef enum { TW_OK, TW_INVALID, TW_OFFLINE, TW_DENIED, TW_UNSUPPORTED } TwResult;
typedef struct {
    char url[TW_URL_SIZE];
    unsigned width, height, bandwidth, fps_milli;
} TwVariant;
bool tw_channel(const char *input, char output[TW_CHANNEL_SIZE]);
bool tw_request(const char *channel, char *out, size_t capacity);
TwResult tw_token_url(const char *channel, const char *json, size_t length,
                      char out[TW_MASTER_SIZE]);
bool tw_cdn_url(const char *url);
TwResult tw_variant(const char *playlist, size_t length, TwVariant *out);
TwResult tw_variant_excluding(const char *playlist, size_t length, TwVariant *out,
                             const char *const *excluded, size_t count);
TwResult tw_media_format(const char *playlist, size_t length);
const char *tw_message(TwResult result);
#endif
