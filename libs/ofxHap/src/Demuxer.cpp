/*
 Demuxer.cpp
 ofxHapPlayer

 Copyright (c) 2016, Tom Butterworth. All rights reserved.
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.

 * Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in the
 documentation and/or other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// New Demuxer implementation using lightweight MOV parser (tcxMovParser)
#include <ofxHap/Demuxer.h>
#include <ofxHap/ffmpeg_compat.h>
#include <ofxHap/tcxMovParser.h>
#include <mutex>
#include <memory>
#include <algorithm>

using namespace tcx::hap;

ofxHap::Demuxer::Demuxer(const std::string& movie, PacketReceiver& receiver) :
    _lastRead(AV_NOPTS_VALUE), _lastSeek(AV_NOPTS_VALUE),
    _thread(&ofxHap::Demuxer::threadMain, this, movie, std::ref(receiver)),
    _finish(false), _active(false)
{

}

ofxHap::Demuxer::~Demuxer()
{
    { // scope for lock
        std::unique_lock<std::mutex> locker(_lock);
        _finish = true;
        _condition.notify_one();
    }
    _thread.join();
}

// Helper to create an AVStream based on MovTrack
static AVStream *create_stream_from_track(const MovTrack &track, int index)
{
    AVStream *s = static_cast<AVStream*>(std::malloc(sizeof(AVStream)));
    std::memset(s, 0, sizeof(AVStream));
    s->codecpar = static_cast<AVCodecParameters*>(std::malloc(sizeof(AVCodecParameters)));
    std::memset(s->codecpar, 0, sizeof(AVCodecParameters));
    s->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    // map FourCC to codec_tag
    s->codecpar->codec_tag = track.codecFourCC;
    // if FourCC is Hap*, mark codec id as HAP
    if (track.codecFourCC == FOURCC_HAP1 || track.codecFourCC == FOURCC_HAP5 || track.codecFourCC == FOURCC_HAPY || track.codecFourCC == FOURCC_HAPM || track.codecFourCC == FOURCC_HAPA) {
        s->codecpar->codec_id = AV_CODEC_ID_HAP;
    }
    s->codecpar->width = track.width;
    s->codecpar->height = track.height;
    s->time_base.num = 1;
    s->time_base.den = track.timescale > 0 ? static_cast<int>(track.timescale) : 1;
    s->duration = track.duration;
    s->start_time = 0;
    s->index = index;
    s->nb_frames = static_cast<int64_t>(track.samples.size());
    s->codec = nullptr;
    return s;
}

// free stream
static void free_stream(AVStream *s)
{
    if (!s) return;
    if (s->codecpar) std::free(s->codecpar);
    std::free(s);
}

void ofxHap::Demuxer::threadMain(const std::string movie, PacketReceiver& receiver)
{
    if (movie.empty()) return;

    MovParser parser;
    if (!parser.open(movie)) {
        receiver.error(-1);
        return;
    }

    const MovInfo &info = parser.getInfo();
    int64_t duration_us = 0;
    if (info.timescale > 0) {
        duration_us = static_cast<int64_t>( (static_cast<double>(info.duration) / info.timescale) * AV_TIME_BASE );
    }
    receiver.foundMovie(duration_us);

    // find video track
    int videoIndex = -1;
    for (size_t i = 0; i < info.tracks.size(); ++i) {
        if (info.tracks[i].isVideo()) { videoIndex = static_cast<int>(i); break; }
    }
    if (videoIndex == -1) {
        receiver.error(-1);
        return;
    }

    // create AVStream for video only (audio not advertised for now)
    AVStream *vstream = create_stream_from_track(info.tracks[videoIndex], 0);
    receiver.foundStream(vstream);

    receiver.foundAllStreams();

    // Build read loop: respond to actions queue similar to original Demuxer
    bool finish = false;
    std::queue<Action> actions;

    size_t sampleIndex = 0;
    const MovTrack &vtrack = info.tracks[videoIndex];

    while (!finish)
    {
        // move queued actions into local queue
        {
            std::unique_lock<std::mutex> locker(_lock);
            finish = _finish;
            while (_actions.size() > 0) {
                const auto action = _actions.front();
                actions.push(action);
                _actions.pop();
            }
            if (actions.empty() && !finish) {
                _active = false;
                _condition.wait(locker);
            }
        }

        while (!actions.empty() && !finish)
        {
            const Action &act = actions.front();
            if (act.kind == Action::Kind::Cancel) {
                // clear queued actions
                std::queue<Action> empty;
                std::swap(actions, empty);
                break;
            }

            if (act.kind == Action::Kind::SeekTime)
            {
                // seek to time (in AV_TIME_BASE units)
                int64_t target_us = act.pts;
                // convert to track timescale units and find nearest sample
                double target_seconds = static_cast<double>(target_us) / AV_TIME_BASE;
                size_t found = 0;
                for (size_t i = 0; i < vtrack.samples.size(); ++i) {
                    if (vtrack.samples[i].timestamp >= target_seconds) { found = i; break; }
                }
                sampleIndex = found;
                receiver.discontinuity();
            }
            else if (act.kind == Action::Kind::SeekFrame)
            {
                int64_t frame = act.pts;
                sampleIndex = static_cast<size_t>(std::max<int64_t>(0, std::min<int64_t>(frame, static_cast<int64_t>(vtrack.samples.size()-1))));
                receiver.discontinuity();
            }
            else if (act.kind == Action::Kind::Read)
            {
                // read until we cover act.pts (in AV_TIME_BASE)
                int64_t target = act.pts;
                int64_t lastRead = AV_NOPTS_VALUE;
                while (sampleIndex < vtrack.samples.size()) {
                    const MovSample &s = vtrack.samples[sampleIndex];
                    // create packet
                    AVPacket *pkt = av_packet_alloc();
                    pkt->size = static_cast<int>(s.size);
                    std::vector<uint8_t> tmp;
                    if (!parser.readSample(vtrack, sampleIndex, tmp)) {
                        av_packet_free(&pkt);
                        break;
                    }
                    pkt->size = static_cast<int>(tmp.size());
                    pkt->data = static_cast<uint8_t*>(std::malloc(pkt->size));
                    if (!pkt->data) { av_packet_free(&pkt); break; }
                    std::memcpy(pkt->data, tmp.data(), pkt->size);
                    pkt->stream_index = 0;
                    // pts/duration in stream time_base units
                    pkt->pts = static_cast<int64_t>(s.timestamp * vtrack.timescale);
                    pkt->duration = s.duration;
                    pkt->pos = static_cast<int64_t>(sampleIndex);

                    receiver.readPacket(pkt);

                    // compute lastRead in AV_TIME_BASE
                    lastRead = static_cast<int64_t>((s.timestamp) * AV_TIME_BASE);
                    sampleIndex++;
                    if (lastRead >= target) break;
                }
                if (sampleIndex >= vtrack.samples.size()) {
                    receiver.endMovie();
                }
            }

            actions.pop();
        }

    }

    free_stream(vstream);
}

void ofxHap::Demuxer::read(int64_t pts)
{
    std::unique_lock<std::mutex> locker(_lock);
    _lastRead = pts;
    _actions.emplace(Action::Kind::Read, pts);
    _active = true;
    _condition.notify_one();
}

void ofxHap::Demuxer::seekTime(int64_t time)
{
    std::unique_lock<std::mutex> locker(_lock);
    _lastSeek = time;
    _lastRead = AV_NOPTS_VALUE;
    _actions.emplace(Action::Kind::SeekTime, time);
    _active = true;
    _condition.notify_one();
}

void ofxHap::Demuxer::seekFrame(int64_t frame)
{
    std::unique_lock<std::mutex> locker(_lock);
    _actions.emplace(Action::Kind::SeekFrame, frame);
    _active = true;
    _condition.notify_one();
}

void ofxHap::Demuxer::cancel()
{
    std::unique_lock<std::mutex> locker(_lock);
    _actions.emplace(Action::Kind::Cancel, 0);
    _condition.notify_one();
}

int64_t ofxHap::Demuxer::getLastReadTime() const
{
    return _lastRead;
}

int64_t ofxHap::Demuxer::getLastSeekTime() const
{
    return _lastSeek;
}

bool ofxHap::Demuxer::isActive() const
{
    std::unique_lock<std::mutex> locker(_lock);
    return _active;
}

ofxHap::Demuxer::Action::Action(Kind k, int64_t p)
: kind(k), pts(p)
{

}
