// Minimal libav compatibility header for ofxHap lightweight build
#pragma once

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <climits>
#include <chrono>
#include <string>
#include <cstdio>
#include <algorithm>

// If Windows headers defined min/max as macros, they break std::min/std::max.
#if defined(_WIN32)
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#endif // defined(_WIN32)

// Basic rational type
typedef struct AVRational {
    int num;
    int den;
} AVRational;

typedef enum AVRounding {
    AV_ROUND_ZERO = 0,
    AV_ROUND_INF  = 1,
    AV_ROUND_DOWN = 2,
    AV_ROUND_UP   = 3,
    AV_ROUND_NEAR_INF = 5,
    AV_ROUND_PASS_MINMAX = 8192
} AVRounding;

static inline int64_t av_rescale_q_rnd(int64_t a, AVRational bq, AVRational cq, AVRounding /*round*/) {
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

static inline int64_t av_rescale_q(int64_t a, AVRational bq, AVRational cq) {
    return av_rescale_q_rnd(a, bq, cq, AV_ROUND_NEAR_INF);
}

static const int64_t AV_TIME_BASE = 1000000LL;
static const int64_t AV_NOPTS_VALUE = LLONG_MIN;

// media types (use int for MSVC compatibility)
typedef int AVMediaType;
#define AVMEDIA_TYPE_UNKNOWN (-1)
#define AVMEDIA_TYPE_VIDEO 0
#define AVMEDIA_TYPE_AUDIO 1

// codec id minimal (use int)
typedef int AVCodecID;
#define AV_CODEC_ID_NONE 0
#define AV_CODEC_ID_HAP 0x10000

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
    uint32_t codec_tag;
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
// av_freep should accept the address of any pointer type (e.g. AVPacket**).
// Use a template to accept T** and free the pointed allocation, then null the pointer.
template<typename T>
static inline void av_freep(T **p) {
    if (!p || !*p) return;
    std::free(static_cast<void*>(*p));
    *p = nullptr;
}

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

// Error macros
#ifndef AV_ERROR_MAX_STRING_SIZE
#define AV_ERROR_MAX_STRING_SIZE 128
#endif

#ifndef AVERROR_INVALIDDATA
// Use EINVAL-like value for invalid data
#define AVERROR_INVALIDDATA (-22)
#endif

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

// Compatibility: initialize an AVPacket (older FFmpeg API)
static inline void av_init_packet(AVPacket *p) {
    if (!p) return;
    std::memset(p, 0, sizeof(AVPacket));
    p->data = nullptr;
    p->size = 0;
}

// Sample formats (minimal set)
typedef enum AVSampleFormat {
    AV_SAMPLE_FMT_U8 = 0,
    AV_SAMPLE_FMT_S16 = 1,
    AV_SAMPLE_FMT_S32 = 2,
    AV_SAMPLE_FMT_FLT = 3,
    AV_SAMPLE_FMT_DBL = 4,
    AV_SAMPLE_FMT_U8P = 5,
    AV_SAMPLE_FMT_S16P = 6,
    AV_SAMPLE_FMT_S32P = 7,
    AV_SAMPLE_FMT_FLTP = 8,
    AV_SAMPLE_FMT_DBLP = 9
} AVSampleFormat;

static inline int av_get_bytes_per_sample(AVSampleFormat fmt) {
    switch (fmt) {
        case AV_SAMPLE_FMT_U8: case AV_SAMPLE_FMT_U8P: return 1;
        case AV_SAMPLE_FMT_S16: case AV_SAMPLE_FMT_S16P: return 2;
        case AV_SAMPLE_FMT_S32: case AV_SAMPLE_FMT_S32P: return 4;
        case AV_SAMPLE_FMT_FLT: case AV_SAMPLE_FMT_FLTP: return 4;
        case AV_SAMPLE_FMT_DBL: case AV_SAMPLE_FMT_DBLP: return 8;
        default: return 0;
    }
}

static inline bool av_sample_fmt_is_planar(AVSampleFormat fmt) {
    switch (fmt) {
        case AV_SAMPLE_FMT_U8P:
        case AV_SAMPLE_FMT_S16P:
        case AV_SAMPLE_FMT_S32P:
        case AV_SAMPLE_FMT_FLTP:
        case AV_SAMPLE_FMT_DBLP:
            return true;
        default:
            return false;
    }
}

static inline void av_samples_set_silence(uint8_t **audio_data, int offset, int nb_samples, int channels, AVSampleFormat sample_fmt) {
    if (!audio_data || nb_samples <= 0) return;
    int bytes_per_sample = av_get_bytes_per_sample(sample_fmt);
    if (bytes_per_sample <= 0) return;
    if (av_sample_fmt_is_planar(sample_fmt)) {
        for (int ch = 0; ch < channels; ++ch) {
            if (audio_data[ch]) {
                std::memset(audio_data[ch] + (size_t)offset * bytes_per_sample, 0, (size_t)nb_samples * bytes_per_sample);
            }
        }
    } else {
        // interleaved
        if (audio_data[0]) {
            std::memset(audio_data[0] + (size_t)offset * channels * bytes_per_sample, 0, (size_t)nb_samples * channels * bytes_per_sample);
        }
    }
}

// Minimal AVFrame for code that queries timestamps/samples only
typedef struct AVFrame {
    uint8_t **data; // optional audio data pointers (may be null)
    int nb_samples;
    int sample_rate;
    int channels;
    int64_t best_effort_timestamp;
} AVFrame;

static inline AVFrame *av_frame_clone(const AVFrame *src) {
    if (!src) return nullptr;
    AVFrame *f = static_cast<AVFrame*>(std::malloc(sizeof(AVFrame)));
    if (!f) return nullptr;
    std::memcpy(f, src, sizeof(AVFrame));
    // do not duplicate audio buffers here; leave data null in cloned frame to avoid double-free
    f->data = nullptr;
    return f;
}

static inline void av_frame_free(AVFrame **f) {
    if (!f || !*f) return;
    // We don't own referenced audio buffers in this minimal implementation
    std::free(*f);
    *f = nullptr;
}

