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

#include "sacdinput.h"

#include "sacdisosettings.h"

#include <QLoggingCategory>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>

Q_LOGGING_CATEGORY(SACD_DEC, "fy.sacdiso.decoder")

using namespace Qt::StringLiterals;

namespace Fooyin::SacdIso {
namespace {
void applyChannelLayout(AudioFormat& format, int channels)
{
    using Pos = AudioFormat::ChannelPosition;

    AudioFormat::ChannelLayout layout;
    switch(channels) {
        case 2:
            layout = {Pos::FrontLeft, Pos::FrontRight};
            break;
        case 5:
            layout = {Pos::FrontLeft, Pos::FrontRight, Pos::FrontCenter, Pos::SideLeft, Pos::SideRight};
            break;
        case 6:
            layout = {Pos::FrontLeft, Pos::FrontRight, Pos::FrontCenter, Pos::LFE, Pos::SideLeft, Pos::SideRight};
            break;
        default:
            break;
    }

    if(!layout.empty()) {
        format.setChannelLayoutAndCount(layout);
    }
}

QString areaName(const SacdArea& area)
{
    return area.multichannel ? u"Multichannel"_s : u"Stereo"_s;
}
} // namespace

bool resolveSacdSubsong(const SacdDisc& disc, int subsong, int& areaIndex, int& trackIndex)
{
    if(subsong < 0) {
        return false;
    }

    int remaining = subsong;
    for(size_t a = 0; a < disc.areas.size(); ++a) {
        const int count = static_cast<int>(disc.areas[a].tracks.size());
        if(remaining < count) {
            areaIndex  = static_cast<int>(a);
            trackIndex = remaining;
            return true;
        }
        remaining -= count;
    }

    return false;
}

QStringList SacdReader::extensions() const
{
    return {u"iso"_s};
}

bool SacdReader::canReadCover() const
{
    return false;
}

bool SacdReader::canWriteMetaData() const
{
    return false;
}

bool SacdReader::init(const AudioSource& source)
{
    if(!source.device || !isSacdImage(source.device)) {
        return false;
    }

    QString error;
    if(!readSacdDisc(source.device, m_disc, &error)) {
        qCDebug(SACD_DEC) << "Failed to read SACD image:" << error;
        return false;
    }

    return m_disc.totalTracks() > 0;
}

int SacdReader::subsongCount() const
{
    return m_disc.totalTracks();
}

bool SacdReader::readTrack(const AudioSource& source, Track& track)
{
    int areaIndex{0};
    int trackIndex{0};
    if(!resolveSacdSubsong(m_disc, track.subsong(), areaIndex, trackIndex)) {
        return false;
    }

    const auto& area = m_disc.areas[static_cast<size_t>(areaIndex)];
    const auto& info = area.tracks[static_cast<size_t>(trackIndex)];

    switch(areaSelection()) {
        case AreaSelection::StereoOnly:
            if(area.multichannel) {
                return false;
            }
            break;
        case AreaSelection::MultichannelOnly:
            if(!area.multichannel) {
                return false;
            }
            break;
        case AreaSelection::Both:
            break;
    }

    const QString album = !m_disc.albumTitle.isEmpty() ? m_disc.albumTitle : m_disc.discTitle;
    QString albumArtist = !m_disc.albumArtist.isEmpty() ? m_disc.albumArtist : m_disc.discArtist;

    track.setTitle(!info.title.isEmpty() ? info.title : u"Track %1"_s.arg(info.number));
    if(!info.performer.isEmpty()) {
        track.setArtists({info.performer});
    }
    else if(!albumArtist.isEmpty()) {
        track.setArtists({albumArtist});
    }
    if(!albumArtist.isEmpty()) {
        track.setAlbumArtists({albumArtist});
    }
    if(!album.isEmpty()) {
        track.setAlbum(album);
    }

    track.setTrackNumber(QString::number(info.number));
    track.setTrackTotal(QString::number(area.tracks.size()));
    if(m_disc.albumSetSize > 1) {
        track.setDiscNumber(QString::number(std::max(m_disc.albumSequenceNumber, 1)));
        track.setDiscTotal(QString::number(m_disc.albumSetSize));
    }

    if(!m_disc.genres.isEmpty()) {
        track.setGenres(m_disc.genres);
    }
    if(!info.composer.isEmpty()) {
        track.setComposers({info.composer});
    }
    if(!info.message.isEmpty()) {
        track.setComment(info.message);
    }
    if(m_disc.year > 0) {
        track.setYear(m_disc.year);
        track.setDate(m_disc.month > 0 && m_disc.day > 0
                          ? u"%1-%2-%3"_s.arg(m_disc.year, 4, 10, QLatin1Char{'0'})
                                .arg(m_disc.month, 2, 10, QLatin1Char{'0'})
                                .arg(m_disc.day, 2, 10, QLatin1Char{'0'})
                          : QString::number(m_disc.year));
    }

    track.addExtraTag(u"SACD_AREA"_s, areaName(area));
    if(!info.songwriter.isEmpty()) {
        track.addExtraTag(u"LYRICIST"_s, info.songwriter);
    }
    if(!info.arranger.isEmpty()) {
        track.addExtraTag(u"ARRANGER"_s, info.arranger);
    }
    if(!info.isrc.isEmpty()) {
        track.addExtraTag(u"ISRC"_s, info.isrc);
    }
    if(!m_disc.albumPublisher.isEmpty()) {
        track.addExtraTag(u"LABEL"_s, m_disc.albumPublisher);
    }
    else if(!m_disc.discPublisher.isEmpty()) {
        track.addExtraTag(u"LABEL"_s, m_disc.discPublisher);
    }
    if(!m_disc.albumCatalog.isEmpty()) {
        track.addExtraTag(u"CATALOGNUMBER"_s, m_disc.albumCatalog);
    }
    else if(!m_disc.discCatalog.isEmpty()) {
        track.addExtraTag(u"CATALOGNUMBER"_s, m_disc.discCatalog);
    }
    if(!m_disc.albumCopyright.isEmpty()) {
        track.addExtraTag(u"COPYRIGHT"_s, m_disc.albumCopyright);
    }

    // Duration
    uint64_t durationMs{info.durationMs()};
    if(!info.hasTimes && source.device) {
        uint32_t firstTc{0};
        if(SacdFrameReader::firstTimecode(source.device, info.rangeStartLsn, info.rangeEndLsn, firstTc)) {
            uint32_t endTc{0};
            bool haveEnd{false};

            if(static_cast<size_t>(trackIndex) + 1 < area.tracks.size()) {
                const auto& next = area.tracks[static_cast<size_t>(trackIndex) + 1];
                haveEnd          = SacdFrameReader::firstTimecode(source.device, next.startLsn, next.startLsn + 16, endTc);
            }
            else {
                haveEnd = SacdFrameReader::lastTimecode(source.device, info.rangeStartLsn, info.rangeEndLsn, endTc);
                ++endTc;
            }

            if(haveEnd && endTc > firstTc) {
                durationMs = (static_cast<uint64_t>(endTc - firstTc) * 1000) / SacdFrameRate;
            }
        }
    }
    track.setDuration(durationMs);

    track.setSampleRate(SacdDsdRate);
    track.setBitDepth(1);
    track.setChannels(area.channelCount);
    track.setCodec(area.isDst() ? u"DST"_s : u"DSD"_s);
    track.setEncoding(u"Lossless"_s);
    track.setTool(u"Super Audio CD"_s);

    if(durationMs > 0) {
        const uint64_t bytes = static_cast<uint64_t>(info.rangeEndLsn - info.rangeStartLsn) * SacdSectorSize;
        track.setBitrate(static_cast<int>((bytes * 8) / durationMs)); // bits per ms == kbit/s
    }

    return true;
}

SacdDecoder::SacdDecoder() = default;

QStringList SacdDecoder::extensions() const
{
    return {u"iso"_s};
}

bool SacdDecoder::isSeekable() const
{
    return true;
}

SacdDecoder::~SacdDecoder()
{
    stop();
}

bool SacdDecoder::openCodec(bool dst)
{
    Codec& codec = m_codecs[dst ? 1 : 0];
    if(codec.ctx) {
        return true;
    }

    const AVCodec* decoder = avcodec_find_decoder(dst ? AV_CODEC_ID_DST : AV_CODEC_ID_DSD_MSBF);
    if(!decoder) {
        qCWarning(SACD_DEC) << "FFmpeg is missing the" << (dst ? "DST" : "DSD") << "decoder";
        return false;
    }

    AVCodecContext* ctx = avcodec_alloc_context3(decoder);
    if(!ctx) {
        return false;
    }

    // libavcodec expects the PCM rate here (DSD rate / 8), as set by its own DSDIFF/DSF demuxers.
    ctx->sample_rate = SacdPcmRate;
    av_channel_layout_default(&ctx->ch_layout, m_channels);

    if(avcodec_open2(ctx, decoder, nullptr) < 0) {
        avcodec_free_context(&ctx);
        return false;
    }

    codec.ctx = ctx;
    return true;
}

void SacdDecoder::closeCodecs()
{
    for(auto& codec : m_codecs) {
        if(codec.ctx) {
            avcodec_free_context(&codec.ctx);
        }
    }
}

bool SacdDecoder::createResampler()
{
    destroyResampler();

    if(m_outRate == SacdPcmRate) {
        return true;
    }

    AVChannelLayout layout;
    av_channel_layout_default(&layout, m_channels);

    SwrContext* swr{nullptr};
    const int ret = swr_alloc_set_opts2(&swr, &layout, AV_SAMPLE_FMT_FLT, m_outRate, &layout, AV_SAMPLE_FMT_FLT,
                                        SacdPcmRate, 0, nullptr);
    av_channel_layout_uninit(&layout);

    if(ret < 0 || !swr) {
        return false;
    }

    av_opt_set_int(swr, "filter_size", 64, 0);
    av_opt_set_int(swr, "phase_shift", 10, 0);
    av_opt_set_double(swr, "cutoff", 0.97, 0);

    if(swr_init(swr) < 0) {
        swr_free(&swr);
        return false;
    }

    m_swr              = swr;
    m_flushedResampler = false;
    return true;
}

void SacdDecoder::destroyResampler()
{
    if(m_swr) {
        swr_free(&m_swr);
    }
    m_flushedResampler = false;
}

std::optional<AudioFormat> SacdDecoder::init(const AudioSource& source, const Track& track,
                                                    DecoderOptions /*options*/)
{
    stop();

    m_device = source.device;
    if(!m_device || !isSacdImage(m_device)) {
        return {};
    }

    QString error;
    if(!readSacdDisc(m_device, m_disc, &error)) {
        qCWarning(SACD_DEC) << "Failed to read SACD image:" << error;
        return {};
    }

    if(!resolveSacdSubsong(m_disc, track.subsong(), m_areaIndex, m_trackIndex)) {
        qCWarning(SACD_DEC) << "Invalid SACD track index" << track.subsong();
        return {};
    }

    const auto& area = m_disc.areas[static_cast<size_t>(m_areaIndex)];
    const auto& info = area.tracks[static_cast<size_t>(m_trackIndex)];

    m_channels = area.channelCount;
    m_outRate  = sacdPcmRate();
    m_gain     = static_cast<float>(std::pow(10.0, sacdGainDb() / 20.0));

    const auto deviceSectors = static_cast<uint64_t>(m_device->size() / SacdSectorSize);
    m_trackStartLsn          = info.rangeStartLsn;
    m_trackEndLsn            = static_cast<uint32_t>(std::min<uint64_t>(deviceSectors, info.rangeEndLsn));
    if(m_trackEndLsn <= m_trackStartLsn) {
        return {};
    }

    if(info.hasTimes) {
        m_startTimecode = info.startFrame;
        m_endTimecode   = info.startFrame + info.durationFrames;
    }
    else {
        uint32_t tc{0};
        if(!SacdFrameReader::firstTimecode(m_device, m_trackStartLsn, m_trackEndLsn, tc)) {
            return {};
        }
        m_startTimecode = tc;
        m_endTimecode   = 0xFFFFFFFFU;

        // Without track times, stop where the following track begins.
        if(static_cast<size_t>(m_trackIndex) + 1 < area.tracks.size()) {
            const auto& next = area.tracks[static_cast<size_t>(m_trackIndex) + 1];
            uint32_t nextTc{0};
            if(SacdFrameReader::firstTimecode(m_device, next.startLsn, next.startLsn + 16, nextTc) && nextTc > tc) {
                m_endTimecode = nextTc;
            }
        }
    }

    m_format = AudioFormat{SampleFormat::F32, m_outRate, m_channels};
    applyChannelLayout(m_format, m_channels);
    if(!m_format.isValid()) {
        return {};
    }

    m_packet = av_packet_alloc();
    m_frame  = av_frame_alloc();
    if(!m_packet || !m_frame) {
        stop();
        return {};
    }

    if(!openCodec(area.isDst()) || !createResampler()) {
        stop();
        return {};
    }

    m_reader = std::make_unique<SacdFrameReader>(m_device, m_channels);

    m_baseMs          = 0;
    m_deliveredFrames = 0;
    m_bytesRead       = 0;
    m_framesRead      = 0;
    m_decodeErrors    = 0;
    m_eof             = false;
    m_pending.clear();
    m_pendingPos = 0;

    m_targetTimecode = m_startTimecode;
    m_skipOutFrames  = 0;
    m_reader->reset(m_trackStartLsn, m_trackEndLsn);

    return m_format;
}

void SacdDecoder::stop()
{
    m_reader.reset();
    destroyResampler();
    closeCodecs();

    if(m_packet) {
        av_packet_free(&m_packet);
    }
    if(m_frame) {
        av_frame_free(&m_frame);
    }

    m_pending.clear();
    m_pendingPos = 0;
    m_device     = nullptr;
    m_eof        = true;
}

void SacdDecoder::restartAt(uint32_t targetTimecode, double frameFraction, uint64_t posMs)
{
    const uint32_t lsn = SacdFrameReader::findLsn(m_device, m_trackStartLsn, m_trackEndLsn, targetTimecode);

    m_reader->reset(lsn, m_trackEndLsn);
    m_pending.clear();
    m_pendingPos = 0;
    m_eof        = false;

    for(auto& codec : m_codecs) {
        if(codec.ctx) {
            avcodec_flush_buffers(codec.ctx);
        }
    }
    createResampler();

    m_targetTimecode = targetTimecode;

    const double nativeSkip = std::clamp(frameFraction, 0.0, 1.0) * SacdSamplesPerFrame;
    m_skipOutFrames
        = static_cast<uint64_t>(std::llround(nativeSkip * static_cast<double>(m_outRate) / SacdPcmRate));

    m_baseMs          = posMs;
    m_deliveredFrames = 0;
}

void SacdDecoder::seek(uint64_t posMs)
{
    if(!m_reader) {
        return;
    }

    const double frames        = static_cast<double>(posMs) * SacdFrameRate / 1000.0;
    const auto wholeFrames     = static_cast<uint32_t>(std::floor(frames));
    const double frameFraction = frames - static_cast<double>(wholeFrames);

    restartAt(m_startTimecode + wholeFrames, frameFraction, posMs);
}

bool SacdDecoder::decodeFrame(const SacdFrame& frame, std::vector<float>& out)
{
    const auto* payload = reinterpret_cast<const uint8_t*>(frame.data.constData());
    auto payloadSize    = static_cast<int>(frame.data.size());
    bool useDstDecoder  = frame.dst;

    // A DST stream may contain frames which were stored uncompressed (the DstCoded flag in the top bit of
    // the first byte is clear). The header byte is followed by plain, byte interleaved DSD. libavcodec's DST
    // decoder does not handle these correctly, so they are decoded with the plain DSD decoder instead.
    if(frame.dst) {
        if(payloadSize < 2) {
            return false;
        }
        if((payload[0] & 0x80) == 0) {
            ++payload;
            --payloadSize;
            useDstDecoder = false;

            const int expected = m_channels * SacdBytesPerChannelDsd;
            if(payloadSize < expected) {
                return false;
            }
            payloadSize = expected;
        }
    }

    if(!openCodec(useDstDecoder)) {
        return false;
    }

    AVCodecContext* ctx = m_codecs[useDstDecoder ? 1 : 0].ctx;

    av_packet_unref(m_packet);
    if(av_new_packet(m_packet, payloadSize) < 0) {
        return false;
    }
    std::memcpy(m_packet->data, payload, static_cast<size_t>(payloadSize));

    if(avcodec_send_packet(ctx, m_packet) < 0) {
        return false;
    }

    av_frame_unref(m_frame);
    if(avcodec_receive_frame(ctx, m_frame) < 0) {
        return false;
    }

    const int samples = m_frame->nb_samples;
    if(samples <= 0 || m_frame->ch_layout.nb_channels != m_channels) {
        return false;
    }

    out.resize(static_cast<size_t>(samples) * static_cast<size_t>(m_channels));

    if(m_frame->format == AV_SAMPLE_FMT_FLTP) {
        for(int ch = 0; ch < m_channels; ++ch) {
            const auto* src = reinterpret_cast<const float*>(m_frame->extended_data[ch]);
            for(int i = 0; i < samples; ++i) {
                out[static_cast<size_t>(i) * static_cast<size_t>(m_channels) + static_cast<size_t>(ch)] = src[i];
            }
        }
    }
    else if(m_frame->format == AV_SAMPLE_FMT_FLT) {
        std::memcpy(out.data(), m_frame->data[0], out.size() * sizeof(float));
    }
    else {
        qCWarning(SACD_DEC) << "Unexpected decoder sample format" << m_frame->format;
        return false;
    }

    return true;
}

bool SacdDecoder::produce()
{
    if(m_eof) {
        return false;
    }

    auto flushResampler = [this]() {
        if(!m_swr || m_flushedResampler) {
            return;
        }
        m_flushedResampler = true;

        const int capacity = swr_get_out_samples(m_swr, 0) + 4096;
        std::vector<float> tail(static_cast<size_t>(capacity) * static_cast<size_t>(m_channels));
        auto* outPtr = reinterpret_cast<uint8_t*>(tail.data());
        const int n  = swr_convert(m_swr, &outPtr, capacity, nullptr, 0);
        if(n > 0) {
            m_pending.insert(m_pending.end(), tail.begin(),
                             tail.begin() + static_cast<std::ptrdiff_t>(n) * m_channels);
        }
    };

    SacdFrame frame;
    std::vector<float> native;
    std::vector<float> converted;

    while(true) {
        if(!m_reader->next(frame)) {
            flushResampler();
            m_eof = true;
            return m_pendingPos < m_pending.size();
        }

        if(frame.timecode < m_targetTimecode) {
            continue; // before the requested start (pre-gap or seek pre-roll)
        }

        if(frame.timecode >= m_endTimecode) {
            flushResampler();
            m_eof = true;
            return m_pendingPos < m_pending.size();
        }

        ++m_framesRead;
        m_bytesRead += static_cast<uint64_t>(frame.data.size());

        if(!decodeFrame(frame, native)) {
            if(++m_decodeErrors <= 5) {
                qCWarning(SACD_DEC) << "Failed to decode frame at timecode" << frame.timecode;
            }
            native.assign(static_cast<size_t>(SacdSamplesPerFrame) * static_cast<size_t>(m_channels), 0.0F);
        }

        const std::vector<float>* samples = &native;
        int sampleFrames                  = static_cast<int>(native.size() / static_cast<size_t>(m_channels));

        if(m_swr) {
            const int capacity = swr_get_out_samples(m_swr, sampleFrames) + 64;
            converted.resize(static_cast<size_t>(capacity) * static_cast<size_t>(m_channels));
            auto* outPtr      = reinterpret_cast<uint8_t*>(converted.data());
            const auto* inPtr = reinterpret_cast<const uint8_t*>(native.data());
            const int n       = swr_convert(m_swr, &outPtr, capacity, &inPtr, sampleFrames);
            if(n < 0) {
                m_eof = true;
                return false;
            }
            converted.resize(static_cast<size_t>(n) * static_cast<size_t>(m_channels));
            samples      = &converted;
            sampleFrames = n;
        }

        size_t first = 0;
        if(m_skipOutFrames > 0) {
            const auto skip = std::min<uint64_t>(m_skipOutFrames, static_cast<uint64_t>(sampleFrames));
            first           = static_cast<size_t>(skip) * static_cast<size_t>(m_channels);
            m_skipOutFrames -= skip;
        }

        if(first >= samples->size()) {
            continue;
        }

        const size_t oldSize = m_pending.size();
        m_pending.insert(m_pending.end(), samples->begin() + static_cast<std::ptrdiff_t>(first), samples->end());

        if(m_gain != 1.0F) {
            for(size_t i = oldSize; i < m_pending.size(); ++i) {
                m_pending[i] *= m_gain;
            }
        }

        return true;
    }
}

AudioBuffer SacdDecoder::readBuffer(size_t bytes)
{
    if(!m_reader) {
        return {};
    }

    const auto channels       = static_cast<size_t>(m_channels);
    const size_t wantedFrames = std::max<size_t>(1, bytes / (channels * sizeof(float)));

    auto availableFrames = [&]() { return (m_pending.size() - m_pendingPos) / channels; };

    while(availableFrames() < wantedFrames && !m_eof) {
        produce();
    }

    const size_t frames = std::min(wantedFrames, availableFrames());
    if(frames == 0) {
        return {};
    }

    const uint64_t startMs = m_baseMs + (m_deliveredFrames * 1000) / static_cast<uint64_t>(m_outRate);

    AudioBuffer buffer{m_format, startMs};
    buffer.resize(frames * channels * sizeof(float));
    std::memcpy(buffer.data(), m_pending.data() + m_pendingPos, frames * channels * sizeof(float));

    m_pendingPos += frames * channels;
    m_deliveredFrames += frames;

    if(m_pendingPos > (1U << 20) && m_pendingPos * 2 > m_pending.size()) {
        m_pending.erase(m_pending.begin(), m_pending.begin() + static_cast<std::ptrdiff_t>(m_pendingPos));
        m_pendingPos = 0;
    }

    return buffer;
}

int SacdDecoder::bitrate() const
{
    if(m_framesRead == 0 || m_areaIndex < 0 || static_cast<size_t>(m_areaIndex) >= m_disc.areas.size()) {
        return 0;
    }

    if(!m_disc.areas[static_cast<size_t>(m_areaIndex)].isDst()) {
        return 0;
    }

    // bits per second over the frames decoded so far, as kbit/s
    return static_cast<int>((m_bytesRead * 8 * SacdFrameRate) / (m_framesRead * 1000));
}
} // namespace Fooyin::SacdIso
