// tcxMovParser - Lightweight QuickTime/ISO BMFF MOV Parser (copied from tcxHap)
#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <memory>
#include <cstring>

namespace tcx { namespace hap {

// FourCC constants for HAP codecs
constexpr uint32_t FOURCC_HAP1 = 0x48617031; // 'Hap1' - HAP (DXT1)
constexpr uint32_t FOURCC_HAP5 = 0x48617035; // 'Hap5' - HAP Alpha (DXT5)
constexpr uint32_t FOURCC_HAPY = 0x48617059; // 'HapY' - HAPQ (YCoCg DXT5)
constexpr uint32_t FOURCC_HAPM = 0x4861704D; // 'HapM' - HAPQ Alpha
constexpr uint32_t FOURCC_HAPA = 0x48617041; // 'HapA' - HAP Alpha Only

// Common atom types
constexpr uint32_t ATOM_FTYP = 0x66747970;
constexpr uint32_t ATOM_MOOV = 0x6D6F6F76;
constexpr uint32_t ATOM_MVHD = 0x6D766864;
constexpr uint32_t ATOM_TRAK = 0x7472616B;
constexpr uint32_t ATOM_TKHD = 0x746B6864;
constexpr uint32_t ATOM_MDIA = 0x6D646961;
constexpr uint32_t ATOM_MDHD = 0x6D646864;
constexpr uint32_t ATOM_HDLR = 0x68646C72;
constexpr uint32_t ATOM_MINF = 0x6D696E66;
constexpr uint32_t ATOM_STBL = 0x7374626C;
constexpr uint32_t ATOM_STSD = 0x73747364;
constexpr uint32_t ATOM_STTS = 0x73747473;
constexpr uint32_t ATOM_STSC = 0x73747363;
constexpr uint32_t ATOM_STSZ = 0x7374737A;
constexpr uint32_t ATOM_STCO = 0x7374636F;
constexpr uint32_t ATOM_CO64 = 0x636F3634;
constexpr uint32_t ATOM_MDAT = 0x6D646174;

// Handler types
constexpr uint32_t HANDLER_VIDE = 0x76696465; // 'vide'
constexpr uint32_t HANDLER_SOUN = 0x736F756E; // 'soun'

// Audio codec FourCCs
constexpr uint32_t FOURCC_SOWT = 0x736F7774; // 'sowt' - 16-bit LE PCM
constexpr uint32_t FOURCC_TWOS = 0x74776F73; // 'twos' - 16-bit BE PCM
constexpr uint32_t FOURCC_LPCM = 0x6C70636D; // 'lpcm' - Linear PCM
constexpr uint32_t FOURCC_FL32 = 0x666C3332; // 'fl32' - 32-bit float
constexpr uint32_t FOURCC_MP3  = 0x2E6D7033; // '.mp3' - MP3
constexpr uint32_t FOURCC_MP4A = 0x6D703461; // 'mp4a' - AAC

struct MovSample {
    uint64_t offset = 0;
    uint32_t size = 0;
    uint32_t duration = 0;
    double timestamp = 0.0;
};

struct MovTrack {
    uint32_t trackId = 0;
    uint32_t handlerType = 0;
    uint32_t codecFourCC = 0;
    uint32_t timescale = 0;
    uint64_t duration = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;
    std::vector<MovSample> samples;
    bool isVideo() const { return handlerType == HANDLER_VIDE; }
    bool isAudio() const { return handlerType == HANDLER_SOUN; }
    bool isHap() const {
        return codecFourCC == FOURCC_HAP1 || codecFourCC == FOURCC_HAP5 || codecFourCC == FOURCC_HAPY || codecFourCC == FOURCC_HAPM || codecFourCC == FOURCC_HAPA;
    }
    double getDurationSeconds() const { return timescale > 0 ? (double)duration / timescale : 0.0; }
};

struct MovInfo {
    uint32_t timescale = 0;
    uint64_t duration = 0;
    std::vector<MovTrack> tracks;
    double getDurationSeconds() const { return timescale > 0 ? (double)duration / timescale : 0.0; }
    const MovTrack* getVideoTrack() const { for (const auto& t : tracks) if (t.isVideo()) return &t; return nullptr; }
    const MovTrack* getAudioTrack() const { for (const auto& t : tracks) if (t.isAudio()) return &t; return nullptr; }
    bool hasHapVideo() const { auto* v = getVideoTrack(); return v && v->isHap(); }
};

class MovParser {
public:
    MovParser() = default;
    ~MovParser() { close(); }
    MovParser(const MovParser&) = delete;
    MovParser& operator=(const MovParser&) = delete;
    bool open(const std::string& path);
    void close();
    bool isOpen() const { return file_.is_open(); }
    const MovInfo& getInfo() const { return info_; }
    bool readSample(const MovTrack& track, size_t sampleIndex, std::vector<uint8_t>& data);
    static bool isHapFile(const std::string& path);
    static std::string fourccToString(uint32_t fourcc);
private:
    std::ifstream file_;
    uint64_t fileSize_ = 0;
    MovInfo info_;
    uint16_t readU16();
    uint32_t readU32();
    uint64_t readU64();
    float readFixed32();
    bool parse();
    void parseMoov(uint64_t endPos);
    void parseMvhd();
    void parseTrak(uint64_t endPos);
    void parseTkhd(MovTrack& track);
    void parseMdia(MovTrack& track, uint64_t endPos);
    void parseMdhd(MovTrack& track);
    void parseHdlr(MovTrack& track);
    void parseMinf(MovTrack& track, uint64_t endPos);
    void parseStbl(MovTrack& track, uint64_t endPos);
    void parseStsd(MovTrack& track);
    void parseStts(std::vector<std::pair<uint32_t, uint32_t>>& timeToSample);
    void parseStsc(std::vector<std::pair<uint32_t, uint32_t>>& sampleToChunk);
    void parseStsz(std::vector<uint32_t>& sampleSizes);
    void parseStco(std::vector<uint64_t>& chunkOffsets);
    void parseCo64(std::vector<uint64_t>& chunkOffsets);
    void buildSamples(MovTrack& track,const std::vector<uint32_t>& sampleSizes,const std::vector<uint64_t>& chunkOffsets,const std::vector<std::pair<uint32_t, uint32_t>>& sampleToChunk,const std::vector<std::pair<uint32_t, uint32_t>>& timeToSample);
    void buildSampleTimestamps(MovTrack& track);
};

} }
