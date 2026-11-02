#pragma once
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QString>
#include <atomic>
#include "core/RenderPlan.h"

class QThread;
class QTemporaryDir;

// Renders an edit to an MP4 with ffmpeg.
//
// Two phases: one ffmpeg per shot into a temporary directory (so a long clip in
// the middle of the edit does not re-encode everything around it), then a single
// pass that either concatenates the shots or dissolves them together and muxes the
// song. Both phases run off the UI thread; progress and the outcome come back
// through the event loop.
class Exporter : public QObject {
    Q_OBJECT
public:
    enum class Quality { Draft, Balanced, High };
    struct Options {
        QSize size{1920, 1080};
        int fps = 30;
        Quality quality = Quality::Balanced;
    };

    explicit Exporter(QObject *parent = nullptr);
    ~Exporter() override;

    void start(const QList<RenderSeg> &segs, const QString &song, const QString &out, const Options &opt);
    void cancel();
    bool busy() const { return m_busy.load(); }

signals:
    // 0..1 over the whole render, with what it is doing in plain words.
    void progress(double fraction, const QString &what);
    void finished(bool ok, const QString &message);

private:
    void run(QList<RenderSeg> segs, QString song, QString out, Options opt);
    // Phase 1: every shot on its own, `workers` at a time.
    bool renderShots(const QList<RenderSeg> &segs, const QList<double> &padMs, const Options &opt,
                     const QString &dir, QString *error);
    // Phase 2: concat (no dissolves) or one xfade graph, then the song.
    bool joinShots(const QList<RenderSeg> &segs, const QList<double> &padMs, bool hasFade,
                   const QString &dir, const QString &song, const QString &out, const Options &opt,
                   QString *error);
    void finish(bool ok, const QString &message);

    std::atomic<bool> m_busy{false};
    std::atomic<bool> m_cancel{false};
    // The worker deletes itself when it finishes, so this is guarded: a plain
    // pointer would dangle between that deletion and the next export.
    QPointer<QThread> m_thread;
};
