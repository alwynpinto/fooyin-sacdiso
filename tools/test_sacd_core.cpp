#include "sacdimage.h"
#include <QCoreApplication>
#include <QFile>
#include <QDebug>
#include <cmath>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
}
using namespace Fooyin::SacdIso;

static double goertzel(const std::vector<float>& x, int ch, int channels, double freq, double rate, size_t skip) {
    double w = 2 * M_PI * freq / rate, c = 2 * std::cos(w), s1 = 0, s2 = 0;
    size_t n = x.size() / channels;
    size_t cnt = 0;
    for (size_t i = skip; i < n; ++i) { double v = x[i * channels + ch]; double s0 = v + c * s1 - s2; s2 = s1; s1 = s0; ++cnt; }
    double p = s1 * s1 + s2 * s2 - c * s1 * s2;
    return std::sqrt(p) * 2.0 / cnt;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QFile f(argv[1]);
    if (!f.open(QIODevice::ReadOnly)) { qWarning() << "open fail"; return 1; }
    qInfo() << "isSacd" << isSacdImage(&f);
    SacdDisc disc; QString err;
    if (!readSacdDisc(&f, disc, &err)) { qWarning() << "read fail" << err; return 1; }
    qInfo() << "album" << disc.albumTitle << disc.albumArtist << disc.year << disc.month << disc.day << disc.genres << disc.albumCatalog;
    for (auto& a : disc.areas) {
        qInfo() << "area multich" << a.multichannel << "ch" << a.channelCount << "fmt" << int(a.frameFormat) << "start" << a.areaStartLsn << "end" << a.areaEndLsn << a.description << "tracks" << a.tracks.size();
        for (auto& t : a.tracks) qInfo() << "  track" << t.number << t.title << t.performer << "lsn" << t.startLsn << t.lengthLsn << "tc" << t.startFrame << t.durationFrames << "ms" << t.durationMs();
    }
    int errors = 0;
    for (auto& a : disc.areas) {
        for (auto& t : a.tracks) {
            SacdFrameReader rd(&f, a.channelCount);
            rd.reset(t.startLsn, t.startLsn + t.lengthLsn);
            SacdFrame fr; int n = 0; uint32_t first = 0, last = 0;
            const AVCodec* dec = avcodec_find_decoder(AV_CODEC_ID_DSD_MSBF);
            AVCodecContext* ctx = avcodec_alloc_context3(dec);
            ctx->sample_rate = 352800; av_channel_layout_default(&ctx->ch_layout, a.channelCount);
            avcodec_open2(ctx, dec, nullptr);
            AVPacket* pkt = av_packet_alloc(); AVFrame* av = av_frame_alloc();
            std::vector<float> pcm;
            while (rd.next(fr)) {
                if (fr.timecode < (t.hasTimes ? t.startFrame : 0)) continue;
                if (t.hasTimes && fr.timecode >= t.startFrame + t.durationFrames) break;
                if (n == 0) first = fr.timecode; last = fr.timecode; ++n;
                av_new_packet(pkt, fr.data.size()); memcpy(pkt->data, fr.data.constData(), fr.data.size());
                avcodec_send_packet(ctx, pkt); av_frame_unref(av);
                if (avcodec_receive_frame(ctx, av) < 0) { qWarning() << "decode fail"; continue; }
                for (int i = 0; i < av->nb_samples; ++i) for (int c = 0; c < a.channelCount; ++c) pcm.push_back(((float*)av->extended_data[c])[i]);
                if (n == 1) qInfo() << "   first frame: nb_samples" << av->nb_samples << "fmt" << av->format << "datasize" << fr.data.size();
            }
            qInfo() << " track" << t.number << "frames" << n << "tc" << first << "-" << last << "dropped" << rd.droppedFrames() << "pcm samples/ch" << pcm.size() / a.channelCount;
            size_t skip = 4000;
            for (int c = 0; c < a.channelCount; ++c) {
                QString line;
                for (double fq : {500.0, 1000.0, 1500.0, 2000.0, 2500.0, 3000.0}) line += QString(" %1Hz:%2").arg(fq).arg(goertzel(pcm, c, a.channelCount, fq, 352800, skip), 0, 'f', 3);
                qInfo().noquote() << "   ch" << c << line;
            }
            // seek test
            uint32_t target = t.hasTimes ? t.startFrame + 20 : 20;
            uint32_t lsn = SacdFrameReader::findLsn(&f, t.startLsn, t.startLsn + t.lengthLsn, target);
            SacdFrameReader rd2(&f, a.channelCount); rd2.reset(lsn, t.startLsn + t.lengthLsn);
            SacdFrame f2; bool ok = rd2.next(f2);
            qInfo() << "   seek target tc" << target << "-> lsn" << lsn << "first frame tc" << (ok ? f2.timecode : 0u);
            if (!ok || f2.timecode > target || target - f2.timecode > 2) ++errors;
            uint32_t ftc = 0, ltc = 0;
            SacdFrameReader::firstTimecode(&f, t.startLsn, t.startLsn + t.lengthLsn, ftc);
            SacdFrameReader::lastTimecode(&f, t.startLsn, t.startLsn + t.lengthLsn, ltc);
            qInfo() << "   first/last tc" << ftc << ltc;
            av_packet_free(&pkt); av_frame_free(&av); avcodec_free_context(&ctx);
        }
    }
    return errors;
}
