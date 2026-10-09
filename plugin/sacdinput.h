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

#pragma once

#include "sacdimage.h"

#include <core/engine/audioinput.h>

#include <memory>
#include <vector>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwrContext;

namespace Fooyin::SacdIso {
//! Maps a flat subsong index (stereo area first) to an area/track pair.
bool resolveSacdSubsong(const SacdDisc& disc, int subsong, int& areaIndex, int& trackIndex);

class SacdReader : public AudioReader
{
public:
    [[nodiscard]] QStringList extensions() const override;
    [[nodiscard]] bool canReadCover() const override;
    [[nodiscard]] bool canWriteMetaData() const override;
    [[nodiscard]] int subsongCount() const override;

    bool init(const AudioSource& source) override;
    bool readTrack(const AudioSource& source, Track& track) override;

private:
    SacdDisc m_disc;
};

class SacdDecoder : public AudioDecoder
{
public:
    SacdDecoder();
    ~SacdDecoder() override;

    [[nodiscard]] QStringList extensions() const override;
    [[nodiscard]] bool isSeekable() const override;
    [[nodiscard]] int bitrate() const override;

    std::optional<AudioFormat> init(const AudioSource& source, const Track& track, DecoderOptions options) override;
    void stop() override;
    void seek(uint64_t pos) override;
    AudioBuffer readBuffer(size_t bytes) override;

private:
    struct Codec
    {
        AVCodecContext* ctx{nullptr};
    };

    bool openCodec(bool dst);
    void closeCodecs();
    bool createResampler();
    void destroyResampler();

    //! Decodes one disc frame into native-rate interleaved float samples.
    bool decodeFrame(const SacdFrame& frame, std::vector<float>& out);
    //! Pulls the next frame from the disc and appends converted PCM to the pending buffer.
    bool produce();
    void restartAt(uint32_t targetTimecode, double frameFraction, uint64_t posMs);

    QIODevice* m_device{nullptr};
    SacdDisc m_disc;
    int m_areaIndex{0};
    int m_trackIndex{0};

    int m_channels{2};
    int m_outRate{176400};
    float m_gain{1.0F};
    AudioFormat m_format;

    uint32_t m_trackStartLsn{0};
    uint32_t m_trackEndLsn{0};
    uint32_t m_startTimecode{0};
    uint32_t m_endTimecode{0xFFFFFFFFU};

    std::unique_ptr<SacdFrameReader> m_reader;
    uint32_t m_targetTimecode{0};
    uint64_t m_skipOutFrames{0};

    Codec m_codecs[2];
    AVPacket* m_packet{nullptr};
    AVFrame* m_frame{nullptr};
    SwrContext* m_swr{nullptr};

    std::vector<float> m_pending;
    size_t m_pendingPos{0};
    bool m_eof{false};
    bool m_flushedResampler{false};

    uint64_t m_baseMs{0};
    uint64_t m_deliveredFrames{0};
    uint64_t m_bytesRead{0};
    uint64_t m_framesRead{0};
    int m_decodeErrors{0};
};
} // namespace Fooyin::SacdIso
