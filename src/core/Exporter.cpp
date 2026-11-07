#include "core/Exporter.h"
#include "core/Tools.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>

namespace {

// ffmpeg parses its numbers with the C locale; format them explicitly rather than
// relying on whatever locale this worker thread inherited.
QString num(double v, int prec = 6) {
    return QString::number(v, 'f', prec);
}

QString ffmpegBin() { return tools::path(QStringLiteral("ffmpeg")); }

// The last lines of a process's error output: ffmpeg says what went wrong on its
// final lines, and the first ones are the stream banners.
QString tail(const QString &err, int lines = 12) {
    const QStringList all = err.trimmed().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    return all.mid(std::max(0, int(all.size()) - lines)).join(QLatin1Char('\n')).trimmed();
}

int evenAtLeast2(int v) { return std::max(2, v - (v % 2)); }

int workerCount(int jobs) {
    const int cap = std::max(1, std::min(4, QThread::idealThreadCount() - 1));
    return std::max(1, std::min(cap, jobs));
}

// What a single ffmpeg invocation was stopped by.
enum RunResult { RunOk = 0, RunFailed = 1, RunCancelled = 2 };

// Waits for `p` in short slices so Cancel is honoured within a fraction of a
// second: one waitForFinished() on a four-minute shot would ignore it entirely.
RunResult waitWatched(QProcess &p, std::atomic<bool> &cancel, QString *err) {
    for (;;) {
        if (p.waitForFinished(100)) break;
        if (cancel.load()) {
            p.kill();
            p.waitForFinished(3000);
            return RunCancelled;
        }
        if (p.state() == QProcess::NotRunning) break;
    }
    if (err) *err += QString::fromLocal8Bit(p.readAllStandardError());
    if (p.exitStatus() != QProcess::NormalExit) return RunFailed;
    return p.exitCode() == 0 ? RunOk : RunFailed;
}

// One shot: seek, cap the length, add the move, letterbox it into the frame and
// freeze its last frame for the dissolve into the next shot.
RunResult renderShot(const RenderSeg &s, int index, double padMs, const Exporter::Options &opt,
                     int outW, int outH, const QString &dir, std::atomic<bool> &cancel,
                     QString *err) {
    // The picture is scaled to fit the frame (never cropped), so a 4:3 clip is
    // pillarboxed exactly the way the preview letterboxes it.
    const double k = std::min(double(outW) / double(s.w), double(outH) / double(s.h));
    const int sw = evenAtLeast2(int(std::lround(s.w * k)));
    const int sh = evenAtLeast2(int(std::lround(s.h * k)));
    const int frames = std::max(1, int(std::lround(s.len * opt.fps)));

    // Letterbox offsets, in pixels: the pad filter would take an expression, but
    // the numbers are already known here and an even offset is mandatory for
    // 4:2:0 chroma.
    const int padX = ((outW - sw) / 2) & ~1;
    const int padY = ((outH - sh) / 2) & ~1;
    QString chain = QStringLiteral("fps=%1").arg(QString::number(opt.fps));
    // Exposure is applied while the frame is still at the source's bit depth:
    // the filter works in float, so a 10-bit clip is lifted before anything is
    // quantised to the 8-bit output. At 0 EV the filter is left out entirely.
    if (std::fabs(opt.exposure) > 0.001)
        chain += QStringLiteral(",exposure=exposure=%1").arg(num(opt.exposure, 3));
    chain += QStringLiteral(",scale=%1:%2:flags=bicubic").arg(QString::number(sw), QString::number(sh));
    // A still shot has nothing to interpolate: the zoompan is left out entirely,
    // which is what "move the shots" being off costs (nothing) and saves (a filter
    // over every frame).
    if (!s.move.still()) {
        const QString zoom = QStringLiteral("z='%1+(%2-%1)*on/%3'")
                                 .arg(num(s.move.z0, 4), num(s.move.z1, 4)).arg(frames);
        const QString px = QStringLiteral("x='(iw-iw/zoom)*(%1+(%2-%1)*on/%3)'")
                               .arg(num(s.move.cx0, 4), num(s.move.cx1, 4)).arg(frames);
        const QString py = QStringLiteral("y='(ih-ih/zoom)*(%1+(%2-%1)*on/%3)'")
                               .arg(num(s.move.cy0, 4), num(s.move.cy1, 4)).arg(frames);
        chain += QStringLiteral(",zoompan=%1:%2:%3:d=1:s=%4x%5:fps=%6")
                     .arg(zoom, px, py, QString::number(sw), QString::number(sh),
                          QString::number(opt.fps));
    }
    chain += QStringLiteral(",pad=%1:%2:%3:%4,setsar=1")
                 .arg(QString::number(outW), QString::number(outH), QString::number(padX),
                      QString::number(padY));
    if (padMs > 0)
        chain += QStringLiteral(",tpad=stop_mode=clone:stop_duration=%1").arg(num(padMs / 1000.0, 3));

    const QString out = QStringLiteral("%1/shot_%2.mp4").arg(dir).arg(index, 4, 10, QLatin1Char('0'));
    QProcess p;
    p.start(ffmpegBin(),
            {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"),
             QStringLiteral("-y"), QStringLiteral("-ss"), num(s.in, 3),
             QStringLiteral("-t"), num(s.len, 3), QStringLiteral("-i"), s.path,
             QStringLiteral("-vf"), chain, QStringLiteral("-an"),
             QStringLiteral("-c:v"), QStringLiteral("libx264"),
             QStringLiteral("-preset"), QStringLiteral("veryfast"),
             QStringLiteral("-crf"), QStringLiteral("12"),
             QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), out});
    if (!p.waitForStarted(10000)) {
        if (err) *err = QStringLiteral("could not start ffmpeg");
        return RunFailed;
    }
    return waitWatched(p, cancel, err);
}

QString concatListLine(const QString &path) {
    // ffmpeg's concat demuxer quotes with single quotes, and a quote inside the
    // path is written by closing, escaping and reopening the string.
    QString escaped = path;
    escaped.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QStringLiteral("file '%1'").arg(escaped);
}

} // namespace

Exporter::Exporter(QObject *parent) : QObject(parent) {}

Exporter::~Exporter() {
    // A worker holding `this` must be gone before the object goes: cancel it and
    // give the shot it is rendering time to die.
    m_cancel = true;
    if (m_thread && m_thread->isRunning()) m_thread->wait();
}

void Exporter::start(const QList<RenderSeg> &segs, const QString &song, const QString &out,
                     const Options &opt) {
    if (m_busy.load()) {
        QMetaObject::invokeMethod(
            this, [this]() { emit finished(false, QStringLiteral("An export is already running.")); },
            Qt::QueuedConnection);
        return;
    }
    // Refuse the impossible cases here, with no process started: a caller that
    // asked for nothing must get an answer, not silence.
    if (segs.isEmpty() || song.isEmpty() || out.isEmpty()) {
        const QString why = segs.isEmpty() ? QStringLiteral("This project has no edit to render.")
                                           : QStringLiteral("Nothing to export.");
        QMetaObject::invokeMethod(this, [this, why]() { emit finished(false, why); },
                                  Qt::QueuedConnection);
        return;
    }
    m_busy = true;
    m_cancel = false;
    m_thread = QThread::create([this, segs, song, out, opt]() { run(segs, song, out, opt); });
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->start();
}

void Exporter::cancel() {
    if (!m_busy.load()) return;
    m_cancel = true;
}

void Exporter::finish(bool ok, const QString &message) {
    m_busy = false;
    QMetaObject::invokeMethod(this, [this, ok, message]() { emit finished(ok, message); },
                              Qt::QueuedConnection);
}

bool Exporter::renderShots(const QList<RenderSeg> &segs, const QList<double> &padMs,
                           const Options &opt, const QString &dir, QString *error) {
    const int n = segs.size();
    const int outW = std::max(2, opt.size.width() - (opt.size.width() % 2));
    const int outH = std::max(2, opt.size.height() - (opt.size.height() % 2));
    std::atomic<int> next{0};
    std::atomic<int> done{0};
    std::atomic<bool> failed{false};
    std::mutex failMtx;
    QString failMsg;

    auto work = [&]() {
        for (;;) {
            const int i = next.fetch_add(1);
            if (i >= n || failed.load() || m_cancel.load()) return;
            QString err;
            const RunResult r = renderShot(segs[i], i, padMs.value(i, 0), opt, outW, outH, dir,
                                           m_cancel, &err);
            if (r == RunCancelled) return;
            if (r != RunOk) {
                std::lock_guard<std::mutex> lock(failMtx);
                if (failMsg.isEmpty())
                    failMsg = QStringLiteral("Rendering shot %1 failed: %2").arg(i + 1).arg(tail(err));
                failed = true;
                return;
            }
            const int d = done.fetch_add(1) + 1;
            QMetaObject::invokeMethod(
                this,
                [this, d, n]() {
                    emit progress(0.75 * d / double(n),
                                  QStringLiteral("Rendering shot %1 of %2").arg(d).arg(n));
                },
                Qt::QueuedConnection);
        }
    };

    std::vector<std::thread> pool;
    const int workers = workerCount(n);
    pool.reserve(workers);
    for (int i = 0; i < workers; i++) pool.emplace_back(work);
    for (std::thread &t : pool) t.join();

    if (failed.load()) {
        if (error) *error = failMsg;
        return false;
    }
    return !m_cancel.load();
}

bool Exporter::joinShots(const QList<RenderSeg> &segs, const QList<double> &padMs, bool hasFade,
                         const QString &dir, const QString &song, const QString &out,
                         const Options &opt, QString *error) {
    const int n = segs.size();
    double total = 0;
    for (const RenderSeg &s : segs) total += s.len;

    QStringList args{QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"),
                     QStringLiteral("-y"), QStringLiteral("-progress"), QStringLiteral("pipe:1")};
    QProcess p;

    if (!hasFade) {
        // No dissolve anywhere: the shots just follow one another, so the video
        // stream is copied rather than encoded a second time.
        const QString list = dir + QStringLiteral("/list.txt");
        QFile f(list);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error) *error = QStringLiteral("Joining the shots failed: could not write the shot list.");
            return false;
        }
        for (int i = 0; i < n; i++)
            f.write(concatListLine(QStringLiteral("%1/shot_%2.mp4").arg(dir).arg(i, 4, 10, QLatin1Char('0')))
                        .toUtf8() + '\n');
        f.close();
        args << QStringLiteral("-f") << QStringLiteral("concat") << QStringLiteral("-safe")
             << QStringLiteral("0") << QStringLiteral("-i") << list;
    } else {
        for (int i = 0; i < n; i++)
            args << QStringLiteral("-i")
                 << QStringLiteral("%1/shot_%2.mp4").arg(dir).arg(i, 4, 10, QLatin1Char('0'));
    }
    args << QStringLiteral("-i") << song;

    if (!hasFade) {
        args << QStringLiteral("-map") << QStringLiteral("0:v") << QStringLiteral("-map")
             << QStringLiteral("1:a") << QStringLiteral("-c:v") << QStringLiteral("copy");
    } else {
        // Each boundary is the dissolve the previous shot was padded for, at the
        // point where that shot's real content ends. Chained, so the graph is one
        // filter per join.
        QStringList graph;
        QString prev = QStringLiteral("[0:v]");
        double at = 0;
        for (int i = 1; i < n; i++) {
            // The dissolve starts where the previous shot's content ends: the sum of
            // the shot lengths before this one. Its frozen tail supplies the rest.
            at += segs[i - 1].len;
            const QString label = QStringLiteral("[v%1]").arg(i);
            graph << QStringLiteral("%1[%2:v]xfade=transition=fade:duration=%3:offset=%4%5")
                         .arg(prev, QString::number(i), num(padMs.value(i - 1, 0) / 1000.0, 3),
                              num(at, 3), label);
            prev = label;
        }
        const int crf = opt.quality == Quality::Draft ? 23
                      : opt.quality == Quality::High ? 17
                                                     : 20;
        const QString preset = opt.quality == Quality::Draft ? QStringLiteral("veryfast")
                             : opt.quality == Quality::High ? QStringLiteral("slow")
                                                            : QStringLiteral("medium");
        args << QStringLiteral("-filter_complex") << graph.join(QLatin1Char(';'))
             << QStringLiteral("-map") << prev << QStringLiteral("-map")
             << QString::number(n) + QStringLiteral(":a") << QStringLiteral("-c:v")
             << QStringLiteral("libx264") << QStringLiteral("-preset") << preset
             << QStringLiteral("-crf") << QString::number(crf) << QStringLiteral("-pix_fmt")
             << QStringLiteral("yuv420p");
    }
    args << QStringLiteral("-c:a") << QStringLiteral("aac") << QStringLiteral("-b:a")
         << QStringLiteral("192k") << QStringLiteral("-t") << num(total, 3)
         << QStringLiteral("-movflags") << QStringLiteral("+faststart") << out;

    p.start(ffmpegBin(), args);
    if (!p.waitForStarted(10000)) {
        if (error) *error = QStringLiteral("Joining the shots failed: could not start ffmpeg.");
        return false;
    }
    // Phase 2 reports its own progress on stdout, so this loop reads both pipes
    // instead of blocking in waitForFinished().
    QByteArray stdoutBuf;
    QString errText;
    for (;;) {
        if (m_cancel.load()) {
            p.kill();
            p.waitForFinished(3000);
            return false;
        }
        if (p.state() == QProcess::NotRunning && !p.bytesAvailable()) break;
        p.waitForReadyRead(100);
        stdoutBuf += p.readAllStandardOutput();
        errText += QString::fromLocal8Bit(p.readAllStandardError());
        int nl;
        while ((nl = stdoutBuf.indexOf('\n')) >= 0) {
            const QByteArray line = stdoutBuf.left(nl).trimmed();
            stdoutBuf.remove(0, nl + 1);
            if (!line.startsWith("out_time_us=")) continue;
            const qint64 us = line.mid(12).toLongLong();
            const double frac = total > 0 ? std::clamp(double(us) / (total * 1e6), 0.0, 1.0) : 0.0;
            QMetaObject::invokeMethod(
                this,
                [this, frac]() {
                    emit progress(0.75 + 0.25 * frac, QStringLiteral("Joining the shots"));
                },
                Qt::QueuedConnection);
        }
    }
    errText += QString::fromLocal8Bit(p.readAllStandardError());
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (error) *error = QStringLiteral("Joining the shots failed: %1").arg(tail(errText));
        return false;
    }
    return true;
}

void Exporter::run(QList<RenderSeg> segs, QString song, QString out, Options opt) {
    QTemporaryDir tmp(QDir::tempPath() + QStringLiteral("/vibes-render-XXXXXX"));
    if (!tmp.isValid()) {
        finish(false, QStringLiteral("Could not create a temporary directory."));
        return;
    }
    const int outW = std::max(2, opt.size.width() - (opt.size.width() % 2));
    const int outH = std::max(2, opt.size.height() - (opt.size.height() % 2));

    // Source sizes: known from the index when the edit is live, probed when the
    // plan came from a document.
    for (RenderSeg &s : segs) {
        if (s.w > 0 && s.h > 0) continue;
        int pw = 0, ph = 0;
        if (probeVideoSize(s.path, &pw, &ph)) {
            s.w = pw;
            s.h = ph;
        } else {
            s.w = outW;   // unreadable: let ffmpeg report the real reason
            s.h = outH;
        }
    }

    bool hasFade = false;
    for (int i = 1; i < segs.size(); i++)
        if (segs[i].fadeMs > 0) hasFade = true;

    // The frozen tail each shot needs for the dissolve that starts the next one.
    // With no dissolve anywhere the concat path holds the length exactly, so no
    // tails are made; with dissolves each tail is exactly the dissolve that
    // consumes it, which keeps the total equal to the sum of the shot lengths.
    const double frameMs = 1000.0 / std::max(1, opt.fps);
    QList<double> padMs(segs.size(), 0.0);
    if (hasFade)
        for (int i = 0; i + 1 < segs.size(); i++)
            padMs[i] = std::max(frameMs, segs[i + 1].fadeMs);

    QString err;
    if (!renderShots(segs, padMs, opt, tmp.path(), &err)) {
        finish(false, m_cancel.load() ? QStringLiteral("Export cancelled") : err);
        return;
    }
    if (!joinShots(segs, padMs, hasFade, tmp.path(), song, out, opt, &err)) {
        finish(false, m_cancel.load() ? QStringLiteral("Export cancelled") : err);
        return;
    }
    finish(true, out);
}
