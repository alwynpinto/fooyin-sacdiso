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
 *
 * The Scarletbook (SACD) structures handled here follow the layout documented
 * by the sacd-ripper project (https://github.com/sacd-ripper/sacd-ripper).
 */

#pragma once

#include <QByteArray>
#include <QIODevice>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <deque>
#include <vector>

namespace Fooyin::SacdIso {
constexpr int SacdSectorSize         = 2048;
constexpr int SacdFrameRate          = 75;
constexpr int SacdBytesPerChannelDsd = 4704; // 588 samples * 64 / 8
constexpr int SacdDsdRate            = 2822400;
constexpr int SacdPcmRate            = SacdDsdRate / 8; // rate produced by DSD -> PCM conversion in libavcodec
constexpr int SacdSamplesPerFrame    = SacdBytesPerChannelDsd; // PCM samples per channel per SACD frame (at 352.8kHz)
constexpr uint32_t SacdMasterTocLsn  = 510;
constexpr int SacdMasterTocSectors   = 10;
constexpr int SacdMaxPacketSize      = 2045;
constexpr int SacdMaxFrameSize       = 64 * 1024;

enum class SacdFrameFormat : uint8_t
{
    Dst        = 0,
    Dsd3In14   = 2,
    Dsd3In16   = 3,
    Unknown    = 0xFF,
};

struct SacdTrack
{
    int number{0}; // 1-based number within its area
    uint32_t startLsn{0};  // as stored in the track list (SACDTRL1)
    uint32_t lengthLsn{0}; // as stored in the track list (SACDTRL1)
    //! Sector range [rangeStartLsn, rangeEndLsn) holding every frame of the track, including the sector
    //! shared with the following track which completes the final frame.
    uint32_t rangeStartLsn{0};
    uint32_t rangeEndLsn{0};
    bool hasTimes{false};
    uint32_t startFrame{0};    // start time in 1/75s units
    uint32_t durationFrames{0}; // duration in 1/75s units

    QString title;
    QString performer;
    QString songwriter;
    QString composer;
    QString arranger;
    QString message;
    QString extraMessage;
    QString isrc;

    [[nodiscard]] uint64_t durationMs() const
    {
        return (static_cast<uint64_t>(durationFrames) * 1000) / SacdFrameRate;
    }
};

struct SacdArea
{
    bool multichannel{false};
    int channelCount{2};
    int loudspeakerConfig{0};
    SacdFrameFormat frameFormat{SacdFrameFormat::Unknown};
    uint32_t areaStartLsn{0};
    uint32_t areaEndLsn{0};
    QString description;
    QString copyright;
    std::vector<SacdTrack> tracks;

    [[nodiscard]] bool isDst() const
    {
        return frameFormat == SacdFrameFormat::Dst;
    }
};

struct SacdDisc
{
    QString albumTitle;
    QString albumArtist;
    QString albumPublisher;
    QString albumCopyright;
    QString discTitle;
    QString discArtist;
    QString discPublisher;
    QString discCopyright;
    QString albumCatalog;
    QString discCatalog;
    QStringList genres;
    int year{0};
    int month{0};
    int day{0};
    int albumSetSize{0};
    int albumSequenceNumber{0};
    std::vector<SacdArea> areas;

    [[nodiscard]] int totalTracks() const;
};

/*!
 * One audio frame (1/75s) as stored on the disc, either raw interleaved DSD (MSB first)
 * or a DST compressed frame.
 */
struct SacdFrame
{
    uint32_t timecode{0}; // 1/75s units, absolute within the audio area
    bool dst{false};
    QByteArray data;
};

//! Reads a block of whole sectors. Returns false on short reads.
bool readSacdSectors(QIODevice* device, uint32_t lsn, uint32_t count, QByteArray& out);

//! Quick check for the Scarletbook master TOC signature.
bool isSacdImage(QIODevice* device);

//! Parses the master TOC, area TOCs, track lists and text.
bool readSacdDisc(QIODevice* device, SacdDisc& disc, QString* error = nullptr);

/*!
 * Reassembles audio frames from the sector stream of a track.
 *
 * Usage: `reset(startLsn, endLsn)` then call `next()` until it returns false.
 */
class SacdFrameReader
{
public:
    SacdFrameReader(QIODevice* device, int channelCount);

    void reset(uint32_t startLsn, uint32_t endLsn);
    //! Returns the next complete frame, or false when the sector range is exhausted.
    bool next(SacdFrame& frame);

    [[nodiscard]] uint32_t droppedFrames() const;

    /*!
     * Finds the LSN of the last sector, inside [startLsn, endLsn), which begins a frame whose
     * timecode is <= targetTimecode. Falls back to startLsn.
     */
    static uint32_t findLsn(QIODevice* device, uint32_t startLsn, uint32_t endLsn, uint32_t targetTimecode);
    /*! Timecode (1/75s units) of the first frame starting at or after startLsn. */
    static bool firstTimecode(QIODevice* device, uint32_t startLsn, uint32_t endLsn, uint32_t& timecode);
    /*! Timecode (1/75s units) of the last frame starting before endLsn. */
    static bool lastTimecode(QIODevice* device, uint32_t startLsn, uint32_t endLsn, uint32_t& timecode);

private:
    bool fillBlock();
    void processSector(const uint8_t* sector);
    void finishFrame();

    QIODevice* m_device;
    int m_channels;

    uint32_t m_nextLsn{0};
    uint32_t m_endLsn{0};

    QByteArray m_block;
    int m_blockSectors{0};
    int m_blockIndex{0};

    bool m_started{false};
    bool m_curDst{false};
    uint32_t m_curTimecode{0};
    QByteArray m_cur;

    std::deque<SacdFrame> m_ready;
    bool m_flushed{false};
    uint32_t m_dropped{0};
};
} // namespace Fooyin::SacdIso
