#include "core/Tools.h"
#include "ui/ClipModel.h"
#include "core/Library.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QPointer>
#include <QStandardPaths>
#include <QThreadPool>
#include <QRunnable>
#include <QCoreApplication>
#include <cmath>
#ifdef Q_OS_UNIX
#  include <unistd.h>
#endif

// Thumbnail extraction runs on a pool of its own: one ffmpeg call per clip,
// cached in the user's cache dir keyed by clip id + size so a rescan is free.
//
// The pool is small and its children are deprioritised on purpose. A first scan
// already runs one ffprobe per clip and then a dozen ffmpeg calls per clip for the
// scores; adding a thumbnail encoder per visible row on top of that is what made
// the window stop answering on a small machine.
class ThumbJob : public QRunnable {
public:
    ThumbJob(const QString &src, double seek, const QString &dest, const QString &key, ThumbJobs *owner);
    void run() override;
private:
    QString m_src, m_dest, m_key;
    double m_seek = 0;
    // The pool outlives the model: a job that finishes while the app is closing
    // must not post into a deleted owner.
    QPointer<ThumbJobs> m_owner;
};

class ThumbJobs : public QObject {
    Q_OBJECT
public:
    ThumbJobs() {
        // Leave the machine something to draw with, and never more encoders than
        // the footage is worth.
        m_pool.setMaxThreadCount(std::max(1, std::min(2, QThread::idealThreadCount() / 2)));
    }
    QThreadPool *pool() { return &m_pool; }

    QString cacheDir() const {
        static const QString d = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/thumbs");
        static const bool once = QDir().mkpath(d);
        Q_UNUSED(once);
        return d;
    }
    void finished(const QString &key, const QString &path, bool ok) {
        emit done(key, path, ok);
    }
signals:
    void done(const QString &key, const QString &path, bool ok);
private:
    QThreadPool m_pool;
};

ThumbJob::ThumbJob(const QString &src, double seek, const QString &dest, const QString &key, ThumbJobs *owner)
    : m_src(src), m_dest(dest), m_key(key), m_seek(seek), m_owner(owner) {
    setAutoDelete(true);
}

void ThumbJob::run() {
    // Everything that touches the disk happens here, on a pool thread: asking the
    // UI thread to stat a thousand files on a network mount is a freeze, not a
    // thumbnail.
    bool ok = QFileInfo(m_dest).size() > 512;
    if (!ok) {
        QProcess p;
        // Below the window's priority: the user is looking at the roster, not at
        // the pictures, and the pool runs several of these at once. Unix only —
        // Qt cannot touch a child's scheduling before it starts anywhere else.
#ifdef Q_OS_UNIX
        p.setChildProcessModifier([] { ::nice(10); });
#endif
        p.start(tools::path(QStringLiteral("ffmpeg")),
                {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"),
                 QStringLiteral("-ss"), QString::number(m_seek, 'f', 2),
                 QStringLiteral("-i"), m_src,
                 QStringLiteral("-frames:v"), QStringLiteral("1"),
                 QStringLiteral("-vf"), QStringLiteral("scale=320:-2"),
                 QStringLiteral("-q:v"), QStringLiteral("4"),
                 m_dest});
        if (p.waitForStarted(15000) && p.waitForFinished(30000))
            ok = p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0 &&
                 QFileInfo(m_dest).size() > 512;
    }
    ThumbJobs *owner = m_owner.data();   // null when the model went away mid-job
    if (!owner) return;
    QMetaObject::invokeMethod(owner, [owner, k = m_key, d = m_dest, ok]() { owner->finished(k, d, ok); },
                              Qt::QueuedConnection);
}

ClipModel::ClipModel(QObject *parent) : QAbstractListModel(parent) {
    m_jobs = new ThumbJobs;
    m_jobs->moveToThread(QCoreApplication::instance()->thread());
    connect(m_jobs, &ThumbJobs::done, this, [this](const QString &key, const QString &path, bool ok) {
        m_pendingThumbs.remove(key);
        if (!ok) return;
        const int id = key.section(QLatin1Char('-'), 0, 0).toInt();
        m_thumbs.insert(id, path);
        for (int row = 0; row < m_ids.size(); row++) {
            if (m_ids[row] != id) continue;
            const QModelIndex ix = index(row, 0);
            emit dataChanged(ix, ix, {ThumbRole});
            break;
        }
    });
}

ClipModel::~ClipModel() { delete m_jobs; }

void ClipModel::setLibrary(Library *lib) {
    m_lib = lib;
    rebuild();
}

void ClipModel::setSearch(const QString &s) {
    if (m_search == s) return;
    m_search = s;
    rebuild();
    emit searchChanged();
}

void ClipModel::rebuild() {
    beginResetModel();
    m_ids.clear();
    m_passing.clear();
    if (m_lib) {
        for (const Clip &c : m_lib->clips())
            if (m_search.isEmpty() ||
                QFileInfo(c.path).fileName().contains(m_search, Qt::CaseInsensitive))
                m_ids.append(c.id);
    }
    endResetModel();
    emit countChanged();
    refresh();
}

int ClipModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_ids.size();
}

QHash<int, QByteArray> ClipModel::roleNames() const {
    return {
        {IdRole, "clipId"}, {NameRole, "name"}, {PathRole, "path"}, {DayRole, "day"},
        {TodRole, "tod"}, {ScoreRole, "score"}, {MvRole, "mv"},
        {AspectRole, "aspect"}, {DurationRole, "duration"}, {ProxyRole, "proxy"},
        {ThumbRole, "thumb"}, {IndexRole, "rowIndex"}, {FilteredRole, "inPool"},
    };
}

QVariant ClipModel::data(const QModelIndex &index, int role) const {
    if (!m_lib || index.row() < 0 || index.row() >= m_ids.size()) return {};
    const Clip *c = m_lib->clipById(m_ids[index.row()]);
    if (!c) return {};
    switch (role) {
    case IdRole: return c->id;
    case NameRole: return QFileInfo(c->path).fileName();
    case PathRole: return c->path;
    case DayRole: return c->day;
    case TodRole: return c->hasTod ? c->tod : -1.0;
    case ScoreRole: return c->score;
    case MvRole: return c->mv;
    case AspectRole: return c->ar();
    case DurationRole: return c->duration;
    case ProxyRole: return !c->lrf.isEmpty();
    case ThumbRole: return m_thumbs.value(c->id);
    case IndexRole: return index.row();
    case FilteredRole: return m_passing.contains(c->id);
    default: return {};
    }
}

QString ClipModel::thumbFor(int id) const { return m_thumbs.value(id); }

void ClipModel::setFilters(const Filters &f) {
    m_filters = f;
    refresh();
}

QList<int> ClipModel::passingIds() const {
    QList<int> out;
    if (!m_lib) return out;
    const QList<Clip> pool = m_lib->poolFor(m_filters);
    QSet<int> ok;
    for (const Clip &c : pool) ok.insert(c.id);
    for (int id : m_ids)
        if (ok.contains(id)) out.append(id);
    return out;
}

void ClipModel::refresh() {
    if (!m_lib) return;
    const QList<Clip> pool = m_lib->poolFor(m_filters);
    QSet<int> pass;
    for (const Clip &c : pool) pass.insert(c.id);
    if (pass != m_passing) {
        m_passing = pass;
        if (!m_ids.isEmpty())
            emit dataChanged(index(0, 0), index(m_ids.size() - 1, 0), {FilteredRole});
    }
    if (m_visible != pass.size()) {
        m_visible = pass.size();
        emit visibleCountChanged();
    }
    // Eager thumbnails for the first screenful; the rest ask for their own when
    // the view builds them (LibraryRail's delegate). A thousand-clip folder was
    // starting a thousand encoders the moment the scan landed.
    ensureThumbs(0, 39);
}

QVariantList ClipModel::visibleIds() const {
    QVariantList out;
    for (int id : passingIds()) out.append(id);
    return out;
}

void ClipModel::ensureThumbs(int first, int last) {
    if (!m_lib) return;
    const int lo = std::max(0, first), hi = std::min(int(m_ids.size()) - 1, last);
    for (int row = lo; row <= hi; row++) {
        const Clip *c = m_lib->clipById(m_ids[row]);
        if (!c) continue;
        if (m_thumbs.contains(c->id)) continue;
        // The key comes from the index, not from the file: this loop runs on the
        // UI thread for every clip, and a stat per clip on a slow share is exactly
        // the stall it used to be. The job stats the cache file and bails out when
        // it is already there.
        const QString key = QStringLiteral("%1-%2").arg(c->id).arg(c->size);
        // Mid-clip still: proxies seek fastest, and the picture matches playback.
        const QString src = !c->lrf.isEmpty() ? c->lrf : c->path;
        const double dur = !c->lrfDur ? c->duration : c->lrfDur;
        // Every refresh (each filter change, each rescan) walks the whole list: a
        // thumbnail still being extracted must not be started a second time, or a
        // slow share ends up running the same ffmpeg dozens of times.
        if (m_pendingThumbs.contains(key)) continue;
        m_pendingThumbs.insert(key);
        const QString dest = m_jobs->cacheDir() + QStringLiteral("/%1.jpg").arg(key);
        m_jobs->pool()->start(new ThumbJob(src, dur * 0.35, dest, key, m_jobs));
    }
}

void ClipModel::requestThumb(int row) { ensureThumbs(row, row); }

#include "ClipModel.moc"
