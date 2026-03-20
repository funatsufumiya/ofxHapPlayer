// Minimal FFmpeg compatibility header for ofxHap lightweight build
#pragma once

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <climits>
#include <chrono>
#include <string>
#include <cstdio>

// Basic rational type
typedef struct AVRational {
    int num;
    int den;
} AVRational;

static inline int64_t av_rescale_q_rnd(int64_t a, AVRational bq, AVRational cq, int64_t /*round*/) {
    // compute a * bq.num/bq.den * cq.den/cq.num
    // Use long double arithmetic to avoid GCC-only __int128 on MSVC.
    if (bq.num == 0 || bq.den == 0 || cq.num == 0 || cq.den == 0) return 0;
    long double val = (long double)a * (long double)bq.num * (long double)cq.den;
    long double den = (long double)bq.den * (long double)cq.num;
    if (den == 0.0L) return 0;
    long double res = val / den;
    if (res > (long double)INT64_MAX) return INT64_MAX;
    if (res < (long double)INT64_MIN) return INT64_MIN;
    return static_cast<int64_t>(res);
}

static const int64_t AV_TIME_BASE = 1000000LL;
static const int64_t AV_NOPTS_VALUE = LLONG_MIN;

// media types
typedef enum AVMediaType { AVMEDIA_TYPE_UNKNOWN = -1, AVMEDIA_TYPE_VIDEO, AVMEDIA_TYPE_AUDIO } AVMediaType;

// codec id minimal
typedef enum AVCodecID { AV_CODEC_ID_NONE = 0, AV_CODEC_ID_HAP = 0x10000 } AVCodecID;

// minimal codec parameter/context structs
typedef struct AVCodecParameters {
    int codec_type; // AVMediaType
    int codec_id;   // AVCodecID
    int width;
    int height;
    int channels;
    int sample_rate;
    uint32_t codec_tag;
} AVCodecParameters;

typedef struct AVCodecContext {
    int codec_type;
    int codec_id;
    int width;
    int height;
    int channels;
    int sample_rate;
} AVCodecContext;

// minimal stream
typedef struct AVStream {
    AVCodecParameters *codecpar;
    AVCodecContext *codec;
    AVRational time_base;
    int64_t duration;
    int64_t start_time;
    int index;
    int64_t nb_frames;
} AVStream;

// minimal packet
typedef struct AVPacket {
    uint8_t *data;
    int size;
    int stream_index;
    int64_t pts;
    int64_t duration;
    int64_t pos;
} AVPacket;

static inline AVPacket *av_packet_alloc() {
    AVPacket *p = static_cast<AVPacket*>(std::malloc(sizeof(AVPacket)));
    if (p) std::memset(p, 0, sizeof(AVPacket));
    return p;
}
static inline void av_packet_free(AVPacket **p) {
    if (!p || !*p) return;
    if ((*p)->data) std::free((*p)->data);
    std::free(*p);
    *p = nullptr;
}
static inline void av_packet_unref(AVPacket *p) {
    if (!p) return;
    if (p->data) { std::free(p->data); p->data = nullptr; }
    p->size = 0;
}

// allocation helpers used by older code
static inline void *av_malloc(size_t s) { return std::malloc(s); }
static inline void av_freep(void *p) { std::free(p); }

// simple time functions
static inline int64_t av_gettime_relative() {
    using namespace std::chrono;
    auto now = steady_clock::now().time_since_epoch();
    return static_cast<int64_t>(duration_cast<microseconds>(now).count());
}

// minimal error string
static inline int av_strerror(int errnum, char *errbuf, size_t errbuf_size) {
    if (!errbuf || errbuf_size == 0) return -1;
    std::snprintf(errbuf, errbuf_size, "error %d", errnum);
    return 0;
}

// packet reference/clone helpers (simple deep-copy semantics)
static inline int av_packet_ref(AVPacket *dst, const AVPacket *src) {
    if (!dst || !src) return -1;
    dst->data = static_cast<uint8_t*>(std::malloc(src->size));
    if (!dst->data) return -1;
    std::memcpy(dst->data, src->data, src->size);
    dst->size = src->size;
    dst->stream_index = src->stream_index;
    dst->pts = src->pts;
    dst->duration = src->duration;
    dst->pos = src->pos;
    return 0;
}

static inline AVPacket *av_packet_clone(const AVPacket *src) {
    if (!src) return nullptr;
    AVPacket *p = av_packet_alloc();
    if (!p) return nullptr;
    if (av_packet_ref(p, src) < 0) { av_packet_free(&p); return nullptr; }
    return p;
}
