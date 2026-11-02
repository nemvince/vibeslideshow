#pragma once
#include "core/Types.h"
#include "core/Params.h"
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QHash>
#include <QSet>

// Scans the project's sources (folders and loose files), indexes each video with
// ffprobe, pairs DJI .LRF proxies, and computes the interest score (motion /
// colour / exposure / variety) in the background.
//
// Threading: every member here is touched on the main thread only. The worker
// threads run free functions over a snapshot of the file list and hand their
// results back with a queued call, so the UI can never read a half-written
// list. Each pass carries a generation number: results from a pass that a
// newer scan superseded are dropped instead of applied.
class Library : public QObject {
    Q_OBJECT
public:
    explicit Library(QObject *parent = nullptr);

    void setSources(const QList<MediaSource> &sources) { m_sources = sources; }
    const QList<MediaSource> &sources() const { return m_sources; }
    // Clips the user removed from the project: skipped by every scan, so they
    // cannot come back on a rescan.
    void setHidden(const QSet<QString> &paths) { m_hidden = paths; }
    const QList<Clip> &clips() const { return m_clips; }

    void startIndex();
    // Drop everything indexed (no sources configured any more).
    void reset();
    // Remove already-indexed clips by path without renumbering the survivors:
    // the timeline still holds segments that point at the surviving ids.
    void dropClips(const QSet<QString> &paths);
    bool busy() const { return m_busy; }
    int scoreDone() const { return m_scoreDone; }
    int scoreTotal() const { return m_scoreTotal; }
    // 0..1 of the scoring work done, counted in sampled frames rather than clips:
    // one long clip otherwise leaves the bar parked at zero for minutes.
    double scoreFraction() const { return m_scoreFrac; }
    int scanDone() const { return m_scanDone; }
    int scanTotal() const { return m_scanTotal; }

    // The pool after the user's filters, in scan order.
    QList<Clip> poolFor(const Filters &f) const;

    // Rank cutoffs for the top-% filters (score at the percentile boundary).
    int scoreCutoff(double pct) const;
    int mvCutoff(double pct) const;

    // Distinct capture days present, newest first.
    QStringList days() const;

    const Clip *clipById(int id) const;

    // One scanned file, before it becomes a Clip.
    struct ScanFile {
        QString path;
        qint64 size = 0;
    };

    // What a scan pass produced.
    struct ScanOutcome {
        QList<Clip> clips;
        int files = 0;     // videos found after duplicate removal
        int skipped = 0;   // found but unreadable (network hiccup, unsupported codec)
    };

signals:
    void indexed();
    void scoringProgress(int done, int total);
    void scoringFinished();
    // Files probed so far, out of the videos found. A scan over a network mount
    // takes minutes; without this the window looks dead.
    void scanProgress(int done, int total);
    // True from the start of a scan until scoring settles.
    void busyChanged();
    void failed(const QString &message);

private:
    // One scored clip, as computed off the UI thread.
    struct Score {
        int score = 0;
        int mv = 0;
        double dk = 1;
        bool ok = false;
    };

    void applyIndex(quint64 gen, const ScanOutcome &outcome);
    void scoreClips(quint64 gen);
    void applyScores(quint64 gen, const QList<Score> &scores);
    void restartIfPending();
    void setBusyInternal(bool b);
    // The human-readable source list used in failure messages.
    QString sourcesLabel() const;

    QList<MediaSource> m_sources;
    QSet<QString> m_hidden;
    QList<Clip> m_clips;
    QHash<int, int> m_indexById;
    bool m_busy = false;
    // setSources() + startIndex() while a pass is running: re-scan as soon as the
    // current pass is out of the way instead of dropping the new folders.
    bool m_restart = false;
    int m_scoreDone = 0;
    int m_scoreTotal = 0;
    double m_scoreFrac = 0;
    void setScoreFrac(double f) { m_scoreFrac = f; }
    int m_scanDone = 0;
    int m_scanTotal = 0;
    quint64 m_gen = 0;
};
