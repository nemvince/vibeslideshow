#include "core/Library.h"

#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QThread>
#include <cmath>
#include <algorithm>

namespace {

const QStringList &videoExts() {
    static const QStringList e = {QStringLiteral("mp4"), QStringLiteral("webm"), QStringLiteral("mov"),
                                  QStringLiteral("mkv"), QStringLiteral("m4v")};
    return e;
}

QString extOf(const QString &p) {
    const int dot = p.lastIndexOf(QLatin1Char('.'));
    return dot < 0 ? QString() : p.mid(dot + 1).toLower();
}

QString stemOf(const QString &p) {
    const int dot = p.lastIndexOf(QLatin1Char('.'));
    return (dot < 0 ? p : p.left(dot)).toLower();
}

// Run a command and return its trimmed stdout; empty on failure.
QString runOut(const QString &prog, const QStringList &args, bool *ok = nullptr, int timeoutMs = 20000) {
    QProcess p;
    p.setProcessChannelMode(QProcess::SeparateChannels);
    p.start(prog, args);
    if (!p.waitForStarted(timeoutMs)) {
        if (ok) *ok = false;
        return {};
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(2000);
        if (ok) *ok = false;
        return {};
    }
    const bool good = p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    if (ok) *ok = good;
    return good ? QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed() : QString();
}

// Capture stamp: a 14-digit YYYYMMDDHHMMSS token in the filename (DJI writes
// UTC). Returns false when absent or implausible.
bool stampOf(const QString &path, QString *day, double *tod) {
    static const QRegularExpression re(QStringLiteral("(\\d{14})"));
    const auto m = re.match(QFileInfo(path).fileName());
    if (!m.hasMatch()) return false;
    const QString s = m.captured(1);
    const int h = s.mid(8, 2).toInt();
    const int mi = s.mid(10, 2).toInt();
    if (h > 23 || mi > 59) return false;
    const int mo = s.mid(4, 2).toInt();
    const int d = s.mid(6, 2).toInt();
    const int y = s.mid(0, 4).toInt();
    if (y <= 0 || mo < 1 || mo > 12 || d < 1 || d > 31) return false;
    *day = QStringLiteral("%1-%2-%3").arg(s.mid(0, 4), s.mid(4, 2), s.mid(6, 2));
    *tod = h + mi / 60.0;
    return true;
}

// The cache prefers the footage directory (so it travels with the clips) and
// falls back to the user cache dir when the footage is read-only.
QString localCacheFile(const QString &dir) {
    return dir + QStringLiteral("/.vibeslideshow-score-cache.json");
}

QString fallbackCacheFile(const QString &dir) {
    const QString slug = QString(dir).replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9.]+")), QStringLiteral("_"));
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
           + QStringLiteral("/cache-%1.json").arg(slug);
}

void loadCaches(const QStringList &dirs, QHash<QString, QVariantMap> *cache, QHash<QString, QString> *paths) {
    for (const QString &dir : dirs) {
        for (const QString &p : {localCacheFile(dir), fallbackCacheFile(dir)}) {
            QFile f(p);
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
            for (auto it = o.begin(); it != o.end(); ++it) cache->insert(it.key(), it.value().toObject().toVariantMap());
            paths->insert(dir, p);
            break;
        }
    }
}

void writeCaches(const QStringList &dirs, const QHash<QString, QVariantMap> &cache,
                 const QHash<QString, QString> &paths) {
    for (const QString &dir : dirs) {
        QJsonObject own;
        for (auto it = cache.begin(); it != cache.end(); ++it) {
            if (!it.key().startsWith(dir)) continue;
            own.insert(it.key(), QJsonObject::fromVariantMap(it.value()));
        }
        if (own.isEmpty()) continue;
        const QByteArray json = QJsonDocument(own).toJson(QJsonDocument::Compact);
        const QString target = paths.contains(dir) ? paths.value(dir) : localCacheFile(dir);
        const bool wroteLocal = [&]() {
            QFile f(target);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
            return f.write(json) == json.size();
        }();
        if (wroteLocal) continue;
        const QString fallback = fallbackCacheFile(dir);
        QDir().mkpath(QFileInfo(fallback).absolutePath());
        QFile f(fallback);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(json);
    }
}

// Walks every configured folder and probes each video. Pure: returns the list,
// touches no members, so it is safe to run on a worker thread.
QList<Clip> scanDirs(const QStringList &dirs) {
    QStringList paths;
    QHash<QString, QString> lrfFor;   // stem -> proxy path
    for (const QString &dir : dirs) {
        QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const QString e = extOf(path);
            if (e == QLatin1String("lrf")) lrfFor.insert(stemOf(path), path);
            else if (videoExts().contains(e)) paths.append(path);
        }
    }
    std::sort(paths.begin(), paths.end());

    QList<Clip> out;
    for (const QString &p : paths) {
        bool ok = false;
        const double d = runOut(QStringLiteral("ffprobe"),
                                {QStringLiteral("-v"), QStringLiteral("error"),
                                 QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
                                 QStringLiteral("-of"), QStringLiteral("csv=p=0"), p}, &ok).toDouble();
        if (!ok || !std::isfinite(d) || d <= 0) continue;

        Clip c;
        c.path = p;
        c.ext = extOf(p);
        c.duration = d;
        c.size = QFileInfo(p).size();
        QString day;
        double tod = 0;
        if (stampOf(p, &day, &tod)) { c.day = day; c.tod = tod; c.hasTod = true; }

        // Dimensions feed the aspect-ratio filter; absent when unreadable.
        const QString dims = runOut(QStringLiteral("ffprobe"),
                                    {QStringLiteral("-v"), QStringLiteral("error"),
                                     QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                                     QStringLiteral("-show_entries"), QStringLiteral("stream=width,height"),
                                     QStringLiteral("-of"), QStringLiteral("csv=p=0"), p});
        if (!dims.isEmpty()) {
            static const QRegularExpression dre(QStringLiteral("(\\d+)\\D+(\\d+)"));
            const auto m = dre.match(dims);
            if (m.hasMatch()) { c.w = m.captured(1).toInt(); c.h = m.captured(2).toInt(); }
        }

        const QString lrf = lrfFor.value(stemOf(p));
        if (!lrf.isEmpty()) {
            bool lok = false;
            const double ld = runOut(QStringLiteral("ffprobe"),
                                     {QStringLiteral("-v"), QStringLiteral("error"),
                                      QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
                                      QStringLiteral("-of"), QStringLiteral("csv=p=0"), lrf}, &lok).toDouble();
            if (lok && std::isfinite(ld) && ld > 0) {
                c.lrf = lrf;
                c.lrfDur = ld;
                c.lrfSize = QFileInfo(lrf).size();
            }
        }
        out.append(c);
    }

    std::sort(out.begin(), out.end(), [](const Clip &a, const Clip &b) { return a.path < b.path; });
    for (int i = 0; i < out.size(); i++) out[i].id = i;
    return out;
}

// Up to 12 evenly spaced 32x18 gray frames, scored for motion, colour, exposure
// and variety. Pure and thread-safe: shells out to ffmpeg, touches no members.
bool scoreClip(const Clip &c, int *score, int *mv, double *dk) {
    const double dur = (c.lrfDur > 0) ? c.lrfDur : c.duration;
    const QString path = (c.lrfDur > 0) ? c.lrf : c.path;
    const int want = std::min(12, std::max(3, int(std::ceil(dur / 4.0))));
    QList<QByteArray> samples;
    const int px = 32 * 18;
    for (int s = 0; s < want; s++) {
        const double t = std::round(((dur / (want + 1)) * (s + 1)) * 10.0) / 10.0;
        QProcess p;
        p.start(QStringLiteral("ffmpeg"),
                {QStringLiteral("-v"), QStringLiteral("error"),
                 QStringLiteral("-ss"), QString::number(t, 'f', 3),
                 QStringLiteral("-i"), path,
                 QStringLiteral("-frames:v"), QStringLiteral("1"),
                 QStringLiteral("-vf"), QStringLiteral("scale=32:18"),
                 QStringLiteral("-f"), QStringLiteral("rawvideo"),
                 QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
                 QStringLiteral("pipe:1")});
        if (!p.waitForStarted(15000)) continue;
        p.waitForFinished(30000);
        const QByteArray buf = p.readAllStandardOutput();
        if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) continue;
        if (buf.size() < px * 3) continue;
        samples.append(buf.left(px * 3));
    }
    if (samples.size() < std::min(3, want)) return false;

    double expSum = 0, colSum = 0, motSum = 0;
    QList<double> means;
    for (int q = 0; q < samples.size(); q++) {
        const uchar *f = reinterpret_cast<const uchar *>(samples[q].constData());
        double lumSum = 0, rgSum = 0, rgSq = 0, ybSum = 0, ybSq = 0;
        for (int j = 0; j < px; j++) {
            const double r = f[j * 3], g = f[j * 3 + 1], b = f[j * 3 + 2];
            const double lum = (r + g + b) / 3.0;
            lumSum += lum;
            const double rg = r - g, yb = (r + g) / 2.0 - b;
            rgSum += rg;
            rgSq += rg * rg;
            ybSum += yb;
            ybSq += yb * yb;
        }
        const double lum = lumSum / px;
        means.append(lum);
        const double mean = lum / 255.0;
        // Dead dark (tunnel), blown white, or a narrow tonal range reads as boring.
        const double exp = (mean <= 0.06 || mean >= 0.92)
                               ? 0.0
                               : std::max(0.0, 1.0 - std::max(0.0, mean < 0.2 ? 0.2 - mean : mean - 0.65) * 6.0);
        // Colourfulness (Hasler-Süsstrunk style): sky and landscape beat tunnel grey.
        const double rgM = rgSum / px, ybM = ybSum / px;
        const double col = std::sqrt(std::max(0.0, rgSq / px - rgM * rgM) + std::max(0.0, ybSq / px - ybM * ybM)) +
                           0.3 * std::sqrt(rgM * rgM + ybM * ybM);
        colSum += std::min(1.0, col / 50.0);
        expSum += exp;
        if (q > 0) {
            const uchar *g0 = reinterpret_cast<const uchar *>(samples[q - 1].constData());
            double dm = 0;
            for (int j = 0; j < px; j++)
                dm += std::abs(int(f[j * 3]) - int(g0[j * 3])) +
                      std::abs(int(f[j * 3 + 1]) - int(g0[j * 3 + 1])) +
                      std::abs(int(f[j * 3 + 2]) - int(g0[j * 3 + 2]));
            motSum += std::min(1.0, dm / (px * 3) / 255.0 / 0.07);
        }
    }
    const int n = samples.size();
    // Variety along the trip: parked or train-stuck footage repeats its frames.
    double vm = 0;
    for (double v : means) vm += v;
    vm /= n;
    double vsd = 0;
    for (double v : means) vsd += (v - vm) * (v - vm);
    vsd = std::sqrt(vsd / n);
    const double variety = std::min(1.0, vsd / 25.0);
    const double motion = motSum / std::max(1, n - 1);

    *score = int(std::round(100.0 * (0.35 * motion + 0.25 * (colSum / n) + 0.25 * (expSum / n) + 0.15 * variety)));
    *mv = int(std::round(100.0 * motion));
    int dark = 0;
    for (double m : means)
        if (m < 10) dark++;
    *dk = dark / double(n);
    return true;
}

} // namespace

Library::Library(QObject *parent) : QObject(parent) {}

const Clip *Library::clipById(int id) const {
    auto it = m_indexById.constFind(id);
    if (it == m_indexById.constEnd()) return nullptr;
    if (it.value() < 0 || it.value() >= m_clips.size()) return nullptr;
    return &m_clips[it.value()];
}

void Library::startIndex() {
    if (m_busy) {
        m_restart = true;
        return;
    }
    m_restart = false;
    m_busy = true;
    m_scoreDone = 0;
    m_scoreTotal = 0;
    const quint64 gen = ++m_gen;
    const QStringList dirs = m_dirs;

    // The index pass shells out to ffprobe per file: keep it off the UI thread.
    QThread *t = QThread::create([this, gen, dirs]() {
        const QList<Clip> found = scanDirs(dirs);
        QMetaObject::invokeMethod(this, [this, gen, found]() { applyIndex(gen, found); }, Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void Library::reset() {
    // Supersede any pass still in flight: its results are now stale.
    ++m_gen;
    m_busy = false;
    m_restart = false;
    m_scoreDone = 0;
    m_scoreTotal = 0;
    m_clips.clear();
    m_indexById.clear();
    emit indexed();
    emit scoringProgress(0, 0);
}

void Library::applyIndex(quint64 gen, const QList<Clip> &clips) {
    if (gen != m_gen) return;
    m_clips = clips;
    m_indexById.clear();
    for (int i = 0; i < m_clips.size(); i++) m_indexById.insert(m_clips[i].id, i);

    // The folders changed while this scan ran: the result is already outdated.
    if (m_restart) {
        restartIfPending();
        return;
    }

    if (m_clips.isEmpty()) {
        m_busy = false;
        emit failed(QStringLiteral("No playable videos found in %1").arg(m_dirs.join(QStringLiteral(", "))));
        emit indexed();
        return;
    }
    emit indexed();
    scoreClips(gen);
}

void Library::restartIfPending() {
    if (!m_restart) return;
    m_restart = false;
    m_busy = false;
    startIndex();
}

void Library::scoreClips(quint64 gen) {
    m_scoreDone = 0;
    m_scoreTotal = m_clips.size();
    emit scoringProgress(0, m_scoreTotal);

    // Snapshot everything the worker needs; the live list stays on this thread.
    const QList<Clip> snapshot = m_clips;
    const QStringList dirs = m_dirs;

    QThread *t = QThread::create([this, gen, snapshot, dirs]() {
        QHash<QString, QVariantMap> cache;
        QHash<QString, QString> cachePaths;
        loadCaches(dirs, &cache, &cachePaths);

        QList<Score> scores;
        scores.resize(snapshot.size());

        auto postProgress = [this, gen](int done, int total) {
            QMetaObject::invokeMethod(this, [this, gen, done, total]() {
                if (gen != m_gen) return;
                m_scoreDone = done;
                emit scoringProgress(done, total);
            }, Qt::QueuedConnection);
        };

        for (int i = 0; i < snapshot.size(); i++) {
            const Clip &c = snapshot[i];
            const QFileInfo fi(c.path);
            const qint64 sizeNow = fi.size();
            const qint64 mtimeNow = qint64(fi.lastModified().toMSecsSinceEpoch());
            const QVariantMap hit = cache.value(c.path);
            const bool cacheOk = hit.value("ver").toInt() == 2 && hit.contains("mv") && hit.contains("dk") &&
                                 hit.value("s").toLongLong() == sizeNow && hit.value("m").toLongLong() == mtimeNow &&
                                 (c.lrf.isEmpty() || hit.value("ls").toLongLong() == c.lrfSize);

            Score s;
            if (cacheOk) {
                s.score = hit.value("v").toInt();
                s.mv = hit.value("mv").toInt();
                s.dk = hit.value("dk").toDouble();
                s.ok = true;
            } else {
                s.ok = scoreClip(c, &s.score, &s.mv, &s.dk);
                if (!s.ok) { s.score = 0; s.mv = 0; s.dk = 1; }
                QVariantMap entry;
                entry["s"] = sizeNow;
                entry["m"] = mtimeNow;
                if (!c.lrf.isEmpty()) entry["ls"] = c.lrfSize;
                entry["v"] = s.score;
                entry["mv"] = s.mv;
                entry["dk"] = s.dk;
                entry["ver"] = 2;
                cache.insert(c.path, entry);
            }
            scores[i] = s;
            const int done = i + 1;
            if (done % 4 == 0 || done == snapshot.size()) postProgress(done, snapshot.size());
        }

        writeCaches(dirs, cache, cachePaths);
        QMetaObject::invokeMethod(this, [this, gen, scores]() { applyScores(gen, scores); }, Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void Library::applyScores(quint64 gen, const QList<Score> &scores) {
    if (gen != m_gen) return;
    const int n = std::min(int(scores.size()), int(m_clips.size()));
    for (int i = 0; i < n; i++) {
        m_clips[i].score = scores[i].score;
        m_clips[i].mv = scores[i].mv;
        m_clips[i].dk = scores[i].dk;
        m_clips[i].scored = scores[i].ok;
    }
    m_busy = false;
    m_scoreDone = m_scoreTotal;
    emit scoringProgress(m_scoreTotal, m_scoreTotal);
    emit scoringFinished();
    restartIfPending();
}

QList<Clip> Library::poolFor(const Filters &f) const {
    const QList<SkipWindow> wins = parseWindows(f.skipWindows);
    const bool anyExclusion = f.junk || (f.dayOn && !f.day.isEmpty()) || f.scoreOn || f.mvOn || f.arOn ||
                              (f.todOn && !f.tod.isEmpty()) || !wins.isEmpty();
    // Fast path: nothing enabled at all.
    if (!anyExclusion) return m_clips;

    QList<Clip> out;
    out.reserve(m_clips.size());
    for (const Clip &c : m_clips) {
        if (f.junk && c.dk >= 0.5) continue;              // mostly-dark junk: black flash source
        if (inSkipWindows(c.day, wins)) continue;
        // "only this day" is exclusive: a clip with no readable stamp is not from it.
        if (f.dayOn && !f.day.isEmpty() && c.day != f.day) continue;
        if (f.scoreOn && !(c.score >= f.scoreMin)) continue;
        if (f.mvOn && !(c.mv >= f.mvMin)) continue;
        // The ratio filter never matches clips without readable dimensions.
        if (f.arOn && f.ar > 0 && !(c.ar() > 0 && std::abs(c.ar() - f.ar) / f.ar < 0.06)) continue;
        if (!c.hasTod) { out.append(c); continue; }        // unstamped clips always pass
        if (!f.todOn || f.tod.isEmpty()) { out.append(c); continue; }
        bool inAny = false;
        for (const auto &r : f.tod) {
            if (r.wrap ? (c.tod >= r.a || c.tod < r.b) : (c.tod >= r.a && c.tod < r.b)) { inAny = true; break; }
        }
        if (inAny) out.append(c);
    }
    return out;
}

int Library::scoreCutoff(double pct) const {
    QList<int> s;
    s.reserve(m_clips.size());
    for (const Clip &c : m_clips) s.append(c.score);
    if (s.isEmpty()) return 0;
    std::sort(s.begin(), s.end(), std::greater<int>());
    const double p = std::max(5.0, std::min(100.0, pct));
    const int idx = std::max(0, int(std::ceil(double(s.size()) * p / 100.0)) - 1);
    return s[std::min(idx, int(s.size()) - 1)];
}

int Library::mvCutoff(double pct) const {
    QList<int> s;
    s.reserve(m_clips.size());
    for (const Clip &c : m_clips) s.append(c.mv);
    if (s.isEmpty()) return 0;
    std::sort(s.begin(), s.end(), std::greater<int>());
    const double p = std::max(10.0, std::min(100.0, pct));
    const int idx = std::max(0, int(std::ceil(double(s.size()) * p / 100.0)) - 1);
    return s[std::min(idx, int(s.size()) - 1)];
}

QStringList Library::days() const {
    QStringList out;
    for (const Clip &c : m_clips)
        if (!c.day.isEmpty() && !out.contains(c.day)) out.append(c.day);
    std::sort(out.begin(), out.end(), std::greater<QString>());
    return out;
}
