/*
 * Fooyin
 * Copyright © 2026, fooyin contributors
 *
 * Fooyin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Fooyin is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Fooyin.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "sacdimage.h"

#include <QLoggingCategory>
#include <QStringDecoder>

#include <algorithm>
#include <cstring>

Q_LOGGING_CATEGORY(SACD_IMG, "fy.sacdiso")

using namespace Qt::StringLiterals;

namespace Fooyin::SacdIso {
namespace {
constexpr int BlockSectors = 32;

constexpr const char* const GenreNames[] = {
    "Not used",    "Not defined",    "Adult Contemporary", "Alternative Rock", "Children's Music",
    "Classical",   "Contemporary Christian", "Country",    "Dance",            "Easy Listening",
    "Erotic",      "Folk",           "Gospel",             "Hip Hop",          "Jazz",
    "Latin",       "Musical",        "New Age",            "Opera",            "Operetta",
    "Pop Music",   "Rap",            "Reggae",             "Rock Music",       "Rhythm & Blues",
    "Sound Effects", "Sound Track",  "Spoken Word",        "World Music",      "Blues",
};
constexpr int GenreCount = static_cast<int>(sizeof(GenreNames) / sizeof(GenreNames[0]));

uint16_t be16(const uint8_t* p)
{
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint32_t be32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16)
         | (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

uint32_t timecodeToFrames(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) * 60 * SacdFrameRate + static_cast<uint32_t>(p[1]) * SacdFrameRate + p[2];
}

QString trimmed(const uint8_t* p, size_t len)
{
    size_t n = 0;
    while(n < len && p[n] != 0) {
        ++n;
    }
    return QString::fromLatin1(reinterpret_cast<const char*>(p), static_cast<qsizetype>(n)).trimmed();
}

//! Decodes a NUL-terminated string (bounded by [p, end)) using one of the Scarletbook character sets.
QString decodeText(const uint8_t* p, const uint8_t* end, int charset)
{
    if(!p || p >= end) {
        return {};
    }

    size_t len = 0;
    while(p + len < end && p[len] != 0) {
        ++len;
    }
    if(len == 0) {
        return {};
    }

    const QByteArray raw{reinterpret_cast<const char*>(p), static_cast<qsizetype>(len)};

    const char* codec{nullptr};
    switch(charset & 0x07) {
        case 3:
            codec = "Shift-JIS";
            break;
        case 4:
            codec = "EUC-KR";
            break;
        case 5:
            codec = "GB2312";
            break;
        case 6:
            codec = "Big5";
            break;
        default:
            break;
    }

    if(codec) {
        QStringDecoder decoder{codec};
        if(decoder.isValid()) {
            const QString text = decoder(raw);
            if(!decoder.hasError()) {
                return text.trimmed();
            }
        }
    }

    return QString::fromLatin1(raw).trimmed();
}

struct AreaToc
{
    SacdArea area;
    bool valid{false};
};

AreaToc parseAreaToc(const QByteArray& data)
{
    AreaToc out;

    if(data.size() < SacdSectorSize) {
        return out;
    }

    const auto* base = reinterpret_cast<const uint8_t*>(data.constData());

    const bool isTwoCh = std::memcmp(base, "TWOCHTOC", 8) == 0;
    const bool isMulCh = std::memcmp(base, "MULCHTOC", 8) == 0;
    if(!isTwoCh && !isMulCh) {
        return out;
    }

    if(base[8] > 1) {
        qCWarning(SACD_IMG) << "Unsupported area TOC version" << base[8] << base[9];
        return out;
    }

    SacdArea& area = out.area;

    int tocSectors = be16(base + 10);
    const int available = static_cast<int>(data.size() / SacdSectorSize);
    if(tocSectors <= 0 || tocSectors > available) {
        tocSectors = available;
    }

    area.frameFormat       = static_cast<SacdFrameFormat>(base[21] & 0x0F);
    area.channelCount      = base[32];
    area.loudspeakerConfig = base[33] >> 3;
    area.areaStartLsn      = be32(base + 72);
    area.areaEndLsn        = be32(base + 76);
    const int trackCount   = base[69];
    const int charset      = base[88 + 2] & 0x07;

    if(area.channelCount < 1 || area.channelCount > 6) {
        qCWarning(SACD_IMG) << "Unsupported channel count" << area.channelCount;
        return out;
    }

    // The TOC identifier is authoritative: TWOCHTOC is the stereo area, MULCHTOC the multichannel one.
    area.multichannel = isMulCh;

    const auto descOffset = be16(base + 144);
    const auto copyOffset = be16(base + 146);
    if(descOffset > 0 && descOffset < SacdSectorSize) {
        area.description = decodeText(base + descOffset, base + SacdSectorSize, charset);
    }
    if(copyOffset > 0 && copyOffset < SacdSectorSize) {
        area.copyright = decodeText(base + copyOffset, base + SacdSectorSize, charset);
    }

    area.tracks.resize(static_cast<size_t>(trackCount));
    for(int i = 0; i < trackCount; ++i) {
        area.tracks[static_cast<size_t>(i)].number = i + 1;
    }

    int textIndex = 0;
    int sector    = 1;
    while(sector < tocSectors) {
        const uint8_t* p = base + static_cast<size_t>(sector) * SacdSectorSize;
        const uint8_t* sectorEnd = p + SacdSectorSize;

        if(std::memcmp(p, "SACDTTxt", 8) == 0) {
            if(textIndex == 0) {
                for(int i = 0; i < trackCount; ++i) {
                    const size_t posOffset = 8 + static_cast<size_t>(i) * 2;
                    if(posOffset + 2 > SacdSectorSize) {
                        break;
                    }
                    const uint16_t pos = be16(p + posOffset);
                    if(pos == 0 || pos >= SacdSectorSize) {
                        continue;
                    }

                    const uint8_t* t = p + pos;
                    const int amount = *t;
                    t += 4;

                    auto& track = area.tracks[static_cast<size_t>(i)];

                    for(int j = 0; j < amount && t + 2 < sectorEnd; ++j) {
                        const uint8_t type = *t;
                        t += 2; // type + unknown byte

                        if(t < sectorEnd && *t != 0) {
                            const QString text = decodeText(t, sectorEnd, charset);
                            switch(type) {
                                case 0x01:
                                    track.title = text;
                                    break;
                                case 0x02:
                                    track.performer = text;
                                    break;
                                case 0x03:
                                    track.songwriter = text;
                                    break;
                                case 0x04:
                                    track.composer = text;
                                    break;
                                case 0x05:
                                    track.arranger = text;
                                    break;
                                case 0x06:
                                    track.message = text;
                                    break;
                                case 0x07:
                                    track.extraMessage = text;
                                    break;
                                default:
                                    break; // phonetic variants are ignored
                            }
                        }

                        if(j < amount - 1) {
                            while(t < sectorEnd && *t != 0) {
                                ++t;
                            }
                            while(t < sectorEnd && *t == 0) {
                                ++t;
                            }
                        }
                    }
                }
            }
            ++textIndex;
            sector += 1;
        }
        else if(std::memcmp(p, "SACD_IGL", 8) == 0) {
            for(int i = 0; i < trackCount && i < 255; ++i) {
                const uint8_t* isrc = p + 8 + static_cast<size_t>(i) * 12;
                if(isrc + 12 > p + SacdSectorSize) {
                    break;
                }
                bool any = false;
                for(int k = 0; k < 12; ++k) {
                    if(isrc[k] > 0x20) {
                        any = true;
                        break;
                    }
                }
                if(any) {
                    const QString code = QString::fromLatin1(reinterpret_cast<const char*>(isrc), 12).trimmed();
                    area.tracks[static_cast<size_t>(i)].isrc = code;
                }
            }
            sector += 2;
        }
        else if(std::memcmp(p, "SACD_ACC", 8) == 0) {
            sector += 32;
        }
        else if(std::memcmp(p, "SACDTRL1", 8) == 0) {
            for(int i = 0; i < trackCount && i < 255; ++i) {
                auto& track     = area.tracks[static_cast<size_t>(i)];
                track.startLsn  = be32(p + 8 + static_cast<size_t>(i) * 4);
                track.lengthLsn = be32(p + 8 + 255 * 4 + static_cast<size_t>(i) * 4);
            }
            sector += 1;
        }
        else if(std::memcmp(p, "SACDTRL2", 8) == 0) {
            for(int i = 0; i < trackCount && i < 255; ++i) {
                auto& track           = area.tracks[static_cast<size_t>(i)];
                const uint8_t* start  = p + 8 + static_cast<size_t>(i) * 4;
                const uint8_t* length = p + 8 + 255 * 4 + static_cast<size_t>(i) * 4;
                track.startFrame      = timecodeToFrames(start);
                track.durationFrames  = timecodeToFrames(length);
                track.hasTimes        = track.durationFrames > 0;
            }
            sector += 1;
        }
        else {
            // Padding or a sector type we do not know: skip it and keep scanning, as text and
            // track lists are not guaranteed to follow the header back to back.
            sector += 1;
        }
    }

    // Drop tracks which never received a track list entry.
    std::erase_if(area.tracks, [](const SacdTrack& track) { return track.startLsn == 0 || track.lengthLsn == 0; });

    // Sector ranges follow the way sacd-ripper reads tracks: the first track starts at the beginning of the
    // area (including any pause), every track runs up to and including the first sector of the next one
    // (which completes its final frame), and the last track runs to the end of the area.
    for(size_t i = 0; i < area.tracks.size(); ++i) {
        auto& track = area.tracks[i];

        track.rangeStartLsn = track.startLsn;
        if(i == 0 && area.areaStartLsn > 0 && area.areaStartLsn <= track.startLsn) {
            track.rangeStartLsn = area.areaStartLsn;
        }

        uint32_t lastLsn = track.startLsn + track.lengthLsn - 1;
        if(i + 1 < area.tracks.size()) {
            if(area.tracks[i + 1].startLsn >= track.startLsn) {
                lastLsn = std::max(lastLsn, area.tracks[i + 1].startLsn);
            }
        }
        else if(area.areaEndLsn >= lastLsn) {
            lastLsn = area.areaEndLsn;
        }

        track.rangeEndLsn = lastLsn + 1;
    }

    out.valid = !area.tracks.empty();
    return out;
}

bool loadArea(QIODevice* device, uint32_t toc1, uint32_t toc2, uint32_t sizeSectors, SacdArea& area)
{
    if(sizeSectors == 0 || sizeSectors > 96) {
        sizeSectors = 96;
    }

    for(const uint32_t lsn : {toc1, toc2}) {
        if(lsn == 0) {
            continue;
        }

        QByteArray data;
        if(!readSacdSectors(device, lsn, sizeSectors, data)) {
            continue;
        }

        auto toc = parseAreaToc(data);
        if(toc.valid) {
            area = std::move(toc.area);
            return true;
        }
    }

    return false;
}

struct PacketInfo
{
    bool frameStart{false};
    int type{0};
    int length{0};
};

struct SectorHeader
{
    bool dst{false};
    int packetCount{0};
    int frameInfoCount{0};
    PacketInfo packets[7];
    uint32_t timecodes[7]{};
    const uint8_t* payload{nullptr};
    bool valid{false};
};

SectorHeader parseSector(const uint8_t* sector)
{
    SectorHeader h;

    const uint8_t hdr = sector[0];
    h.dst            = (hdr & 0x01) != 0;
    h.frameInfoCount = (hdr >> 2) & 0x07;
    h.packetCount    = (hdr >> 5) & 0x07;

    const int frameInfoSize = h.dst ? 4 : 3;
    const int headerBytes   = 1 + h.packetCount * 2 + h.frameInfoCount * frameInfoSize;
    if(headerBytes > SacdSectorSize) {
        return h;
    }

    const uint8_t* p = sector + 1;
    for(int i = 0; i < h.packetCount; ++i) {
        h.packets[i].frameStart = ((p[0] >> 7) & 1) != 0;
        h.packets[i].type       = (p[0] >> 3) & 7;
        h.packets[i].length     = ((p[0] & 7) << 8) | p[1];
        p += 2;
    }
    for(int i = 0; i < h.frameInfoCount; ++i) {
        h.timecodes[i] = timecodeToFrames(p);
        p += frameInfoSize;
    }

    h.payload = p;
    h.valid   = true;
    return h;
}

bool sectorFirstTimecode(const uint8_t* sector, uint32_t& timecode)
{
    const auto h = parseSector(sector);
    if(!h.valid || h.frameInfoCount == 0) {
        return false;
    }

    for(int i = 0; i < h.packetCount; ++i) {
        if(h.packets[i].type == 2 && h.packets[i].frameStart) {
            timecode = h.timecodes[0];
            return true;
        }
    }

    return false;
}
} // namespace

int SacdDisc::totalTracks() const
{
    int total{0};
    for(const auto& area : areas) {
        total += static_cast<int>(area.tracks.size());
    }
    return total;
}

bool readSacdSectors(QIODevice* device, uint32_t lsn, uint32_t count, QByteArray& out)
{
    if(!device || count == 0) {
        return false;
    }

    if(!device->seek(static_cast<qint64>(lsn) * SacdSectorSize)) {
        return false;
    }

    const qint64 wanted = static_cast<qint64>(count) * SacdSectorSize;
    out.resize(wanted);

    qint64 got{0};
    while(got < wanted) {
        const qint64 n = device->read(out.data() + got, wanted - got);
        if(n <= 0) {
            break;
        }
        got += n;
    }

    if(got != wanted) {
        out.clear();
        return false;
    }

    return true;
}

bool isSacdImage(QIODevice* device)
{
    if(!device || !device->isReadable() || device->isSequential()) {
        return false;
    }

    if(device->size() < static_cast<qint64>(SacdMasterTocLsn + 1) * SacdSectorSize) {
        return false;
    }

    QByteArray sector;
    if(!readSacdSectors(device, SacdMasterTocLsn, 1, sector)) {
        return false;
    }

    return sector.startsWith("SACDMTOC");
}

bool readSacdDisc(QIODevice* device, SacdDisc& disc, QString* error)
{
    auto fail = [&](const QString& message) {
        if(error) {
            *error = message;
        }
        return false;
    };

    QByteArray master;
    if(!readSacdSectors(device, SacdMasterTocLsn, SacdMasterTocSectors, master)) {
        return fail(u"Unable to read the SACD master TOC"_s);
    }

    const auto* m = reinterpret_cast<const uint8_t*>(master.constData());
    if(std::memcmp(m, "SACDMTOC", 8) != 0) {
        return fail(u"Not a Scarletbook (SACD) image"_s);
    }
    if(m[8] > 1) {
        return fail(u"Unsupported SACD version"_s);
    }

    disc = {};

    disc.albumSetSize         = be16(m + 16);
    disc.albumSequenceNumber  = be16(m + 18);
    disc.albumCatalog         = trimmed(m + 24, 16);
    disc.discCatalog          = trimmed(m + 88, 16);
    disc.year                 = be16(m + 120);
    disc.month                = m[122];
    disc.day                  = m[123];

    for(int i = 0; i < 4; ++i) {
        const int genre = m[40 + i * 4 + 3];
        if(genre >= 2 && genre < GenreCount) {
            const QString name = QString::fromLatin1(GenreNames[genre]);
            if(!disc.genres.contains(name)) {
                disc.genres.push_back(name);
            }
        }
    }

    const uint32_t area1Toc1 = be32(m + 64);
    const uint32_t area1Toc2 = be32(m + 68);
    const uint32_t area2Toc1 = be32(m + 72);
    const uint32_t area2Toc2 = be32(m + 76);
    const uint16_t area1Size = be16(m + 84);
    const uint16_t area2Size = be16(m + 86);

    // Master text (first locale only)
    {
        const int charset = m[136 + 2] & 0x07;
        const uint8_t* t  = m + SacdSectorSize;
        if(std::memcmp(t, "SACDText", 8) == 0) {
            const uint8_t* tEnd = t + SacdSectorSize;
            auto text           = [&](int index) -> QString {
                const uint16_t pos = be16(t + 16 + index * 2);
                if(pos == 0 || pos >= SacdSectorSize) {
                    return {};
                }
                return decodeText(t + pos, tEnd, charset);
            };

            disc.albumTitle     = text(0);
            disc.albumArtist    = text(1);
            disc.albumPublisher = text(2);
            disc.albumCopyright = text(3);
            disc.discTitle      = text(8);
            disc.discArtist     = text(9);
            disc.discPublisher  = text(10);
            disc.discCopyright  = text(11);
        }
    }

    SacdArea area;
    if(area1Toc1 > 0 && loadArea(device, area1Toc1, area1Toc2, area1Size, area)) {
        disc.areas.push_back(std::move(area));
    }
    area = {};
    if(area2Toc1 > 0 && loadArea(device, area2Toc1, area2Toc2, area2Size, area)) {
        disc.areas.push_back(std::move(area));
    }

    if(disc.areas.empty()) {
        return fail(u"No readable SACD audio areas"_s);
    }

    // Stereo area first, then multichannel
    std::stable_sort(disc.areas.begin(), disc.areas.end(),
                     [](const SacdArea& a, const SacdArea& b) { return !a.multichannel && b.multichannel; });

    return true;
}

SacdFrameReader::SacdFrameReader(QIODevice* device, int channelCount)
    : m_device{device}
    , m_channels{channelCount}
{ }

void SacdFrameReader::reset(uint32_t startLsn, uint32_t endLsn)
{
    m_nextLsn      = startLsn;
    m_endLsn       = endLsn;
    m_blockSectors = 0;
    m_blockIndex   = 0;
    m_started      = false;
    m_flushed      = false;
    m_cur.clear();
    m_ready.clear();
}

uint32_t SacdFrameReader::droppedFrames() const
{
    return m_dropped;
}

bool SacdFrameReader::fillBlock()
{
    if(m_nextLsn >= m_endLsn) {
        return false;
    }

    const uint32_t count = std::min<uint32_t>(BlockSectors, m_endLsn - m_nextLsn);
    if(!readSacdSectors(m_device, m_nextLsn, count, m_block)) {
        return false;
    }

    m_blockSectors = static_cast<int>(count);
    m_blockIndex   = 0;
    m_nextLsn += count;
    return true;
}

void SacdFrameReader::finishFrame()
{
    if(!m_started) {
        return;
    }

    bool valid{false};
    if(m_curDst) {
        valid = !m_cur.isEmpty();
    }
    else {
        valid = m_cur.size() == static_cast<qsizetype>(m_channels) * SacdBytesPerChannelDsd;
    }

    if(valid) {
        SacdFrame frame;
        frame.timecode = m_curTimecode;
        frame.dst      = m_curDst;
        frame.data     = std::move(m_cur);
        m_ready.push_back(std::move(frame));
    }
    else {
        ++m_dropped;
    }

    m_cur.clear();
    m_started = false;
}

void SacdFrameReader::processSector(const uint8_t* sector)
{
    const auto h = parseSector(sector);
    if(!h.valid) {
        m_started = false;
        ++m_dropped;
        return;
    }

    const uint8_t* p   = h.payload;
    const uint8_t* end = sector + SacdSectorSize;
    int frameIndex{0};

    for(int i = 0; i < h.packetCount; ++i) {
        const auto& packet = h.packets[i];

        if(packet.length > SacdMaxPacketSize || p + packet.length > end) {
            m_started = false;
            ++m_dropped;
            return;
        }

        if(packet.type == 2) { // audio
            if(packet.frameStart) {
                finishFrame();

                if(frameIndex < h.frameInfoCount) {
                    m_curTimecode = h.timecodes[frameIndex++];
                    m_curDst      = h.dst;
                    m_started     = true;
                    m_cur.clear();
                }
                else {
                    m_started = false;
                    ++m_dropped;
                }
            }

            if(m_started) {
                if(m_cur.size() + packet.length <= SacdMaxFrameSize) {
                    m_cur.append(reinterpret_cast<const char*>(p), packet.length);
                }
                else {
                    m_started = false;
                    m_cur.clear();
                    ++m_dropped;
                }
            }
        }

        p += packet.length;
    }
}

bool SacdFrameReader::next(SacdFrame& frame)
{
    while(true) {
        if(!m_ready.empty()) {
            frame = std::move(m_ready.front());
            m_ready.pop_front();
            return true;
        }

        if(m_flushed) {
            return false;
        }

        if(m_blockIndex >= m_blockSectors) {
            if(!fillBlock()) {
                finishFrame();
                m_flushed = true;
                continue;
            }
        }

        const auto* sector
            = reinterpret_cast<const uint8_t*>(m_block.constData()) + static_cast<size_t>(m_blockIndex) * SacdSectorSize;
        ++m_blockIndex;
        processSector(sector);
    }
}

bool SacdFrameReader::firstTimecode(QIODevice* device, uint32_t startLsn, uint32_t endLsn, uint32_t& timecode)
{
    QByteArray sector;
    const uint32_t limit = std::min<uint32_t>(endLsn, startLsn + 256);
    for(uint32_t lsn = startLsn; lsn < limit; ++lsn) {
        if(!readSacdSectors(device, lsn, 1, sector)) {
            return false;
        }
        if(sectorFirstTimecode(reinterpret_cast<const uint8_t*>(sector.constData()), timecode)) {
            return true;
        }
    }
    return false;
}

bool SacdFrameReader::lastTimecode(QIODevice* device, uint32_t startLsn, uint32_t endLsn, uint32_t& timecode)
{
    if(endLsn <= startLsn) {
        return false;
    }

    QByteArray sector;
    const uint32_t lowest = endLsn - std::min<uint32_t>(endLsn - startLsn, 256);
    for(uint32_t lsn = endLsn; lsn > lowest;) {
        --lsn;
        if(!readSacdSectors(device, lsn, 1, sector)) {
            return false;
        }
        if(sectorFirstTimecode(reinterpret_cast<const uint8_t*>(sector.constData()), timecode)) {
            return true;
        }
    }
    return false;
}

uint32_t SacdFrameReader::findLsn(QIODevice* device, uint32_t startLsn, uint32_t endLsn, uint32_t targetTimecode)
{
    if(endLsn <= startLsn) {
        return startLsn;
    }

    constexpr uint32_t MaxForwardScan = 64;

    QByteArray sector;
    int64_t lo = startLsn;
    int64_t hi = static_cast<int64_t>(endLsn) - 1;
    uint32_t best = startLsn;

    while(lo <= hi) {
        const int64_t mid = lo + (hi - lo) / 2;

        // Find the first sector at/after mid which begins a frame.
        int64_t found{-1};
        uint32_t tc{0};
        const int64_t limit = std::min<int64_t>(hi + 1, mid + MaxForwardScan);
        for(int64_t lsn = mid; lsn < limit; ++lsn) {
            if(!readSacdSectors(device, static_cast<uint32_t>(lsn), 1, sector)) {
                return best;
            }
            if(sectorFirstTimecode(reinterpret_cast<const uint8_t*>(sector.constData()), tc)) {
                found = lsn;
                break;
            }
        }

        if(found < 0) {
            hi = mid - 1;
            continue;
        }

        if(tc <= targetTimecode) {
            best = static_cast<uint32_t>(found);
            lo   = found + 1;
        }
        else {
            hi = mid - 1;
        }
    }

    return best;
}
} // namespace Fooyin::SacdIso
