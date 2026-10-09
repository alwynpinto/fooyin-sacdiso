#include "sacdinput.h"
#include "sacdisosettings.h"
#include <QCoreApplication>
#include <QFile>
#include <QDebug>
#include <cmath>
using namespace Fooyin;
using namespace Fooyin::SacdIso;

static double goertzel(const std::vector<float>& x, int ch, int channels, double freq, double rate, size_t skip) {
    double w = 2 * M_PI * freq / rate, c = 2 * std::cos(w), s1 = 0, s2 = 0;
    size_t n = x.size() / channels, cnt = 0;
    for (size_t i = skip; i < n; ++i) { double v = x[i * channels + ch]; double s0 = v + c * s1 - s2; s2 = s1; s1 = s0; ++cnt; }
    return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) * 2.0 / cnt;
}

struct Result { std::vector<float> pcm; uint64_t firstStart{0}; bool monotonic{true}; uint64_t lastEnd{0}; int channels{0}; int rate{0}; };

static Result decodeAll(QFile& f, int subsong, int seekMs = -1) {
    Result r;
    SacdDecoder dec;
    AudioSource src; src.filepath = f.fileName(); src.device = &f;
    Track t(f.fileName(), subsong);
    f.seek(0);
    auto fmt = dec.init(src, t, {});
    if (!fmt) { qWarning() << "init failed"; return r; }
    r.channels = fmt->channelCount(); r.rate = fmt->sampleRate();
    if (seekMs >= 0) dec.seek(seekMs);
    bool first = true; uint64_t prevEnd = 0;
    while (true) {
        AudioBuffer b = dec.readBuffer(16384);
        if (!b.isValid()) break;
        if (first) { r.firstStart = b.startTime(); first = false; }
        else if (b.startTime() + 1 < prevEnd || b.startTime() > prevEnd + 2) r.monotonic = false;
        prevEnd = b.endTime(); r.lastEnd = prevEnd;
        const float* p = reinterpret_cast<const float*>(b.data());
        r.pcm.insert(r.pcm.end(), p, p + b.sampleCount());
    }
    dec.stop();
    return r;
}

static void report(const char* name, const Result& r, std::initializer_list<double> freqs) {
    size_t frames = r.pcm.size() / std::max(r.channels, 1);
    qInfo().noquote() << name << "rate" << r.rate << "ch" << r.channels << "frames" << frames << "dur(ms)" << (frames * 1000.0 / std::max(r.rate, 1))
                      << "firstStart" << r.firstStart << "lastEnd" << r.lastEnd << "monotonic" << r.monotonic;
    for (int c = 0; c < r.channels; ++c) {
        QString line;
        for (double fq : freqs) line += QString(" %1Hz:%2").arg(fq).arg(goertzel(r.pcm, c, r.channels, fq, r.rate, 2000), 0, 'f', 3);
        qInfo().noquote() << "   ch" << c << line;
    }
}

int main(int argc, char** argv) {
    qputenv("XDG_CONFIG_HOME", "/tmp/fytest-config");
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("fooyin-test"); QCoreApplication::setApplicationName("fooyin-test");
    QFile f(argv[1]);
    if (!f.open(QIODevice::ReadOnly)) return 2;

    { // reader
        SacdReader rd; AudioSource src; src.filepath = f.fileName(); src.device = &f;
        qInfo() << "reader init" << rd.init(src) << "subsongs" << rd.subsongCount();
        for (int i = 0; i < rd.subsongCount(); ++i) {
            Track t(f.fileName(), i);
            bool ok = rd.readTrack(src, t);
            qInfo().noquote() << "  subsong" << i << ok << t.title() << "|" << t.artists() << "|" << t.album() << "| trk" << t.trackNumber() << "/" << t.trackTotal()
                              << "| dur" << t.duration() << "| ch" << t.channels() << "| rate" << t.sampleRate() << "|" << t.codec() << "| br" << t.bitrate()
                              << "| genres" << t.genres() << "| year" << t.year() << "| date" << t.date();
        }
    }

    auto r1 = decodeAll(f, 0);
    report("track1 stereo @176.4k", r1, {1000, 2000, 500, 3000});
    auto r2 = decodeAll(f, 1);
    report("track2 stereo @176.4k", r2, {1000, 2000, 500, 3000});
    auto r3 = decodeAll(f, 2);
    report("5ch @176.4k", r3, {500, 1000, 1500, 2000, 2500});

    auto s1 = decodeAll(f, 0, 300);
    report("track1 seek 300ms", s1, {1000, 2000});
    auto s2 = decodeAll(f, 0, 313);
    report("track1 seek 313ms", s2, {1000, 2000});

    FySettings settings;
    settings.setValue(QLatin1String{SettingSacdPcmRate}, 352800);
    settings.setValue(QLatin1String{SettingSacdGainDb}, 6.0);
    auto n1 = decodeAll(f, 0);
    report("track1 stereo @352.8k +6dB", n1, {1000, 2000});
    settings.setValue(QLatin1String{SettingSacdPcmRate}, 88200);
    settings.setValue(QLatin1String{SettingSacdGainDb}, 0.0);
    auto l1 = decodeAll(f, 0);
    report("track1 stereo @88.2k", l1, {1000, 2000});
    return 0;
}
