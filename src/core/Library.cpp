#include "core/Tools.h"
#include "core/Library.h"

#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QThread>
#include <atomic>
#ifdef Q_OS_UNIX
#  include <unistd.h>
#endif
#include <cmath>
#include <algorithm>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

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

// The scan and the scoring run dozens of these at a time. At normal priority they
// take every core the machine has, and the window — which has to keep drawing the
// progress it is reporting — comes last. Hand the children a lower priority so the
// UI thread wins the races it needs to.
void deprioritise(QProcess &p) {
#ifdef Q_OS_UNIX
    p.setChildProcessModifier([] { ::nice(10); });
#else
    // Qt can only modify a child's environment before it starts on Unix, and
    // Windows has no creation-time priority class. The worker count below is what
    // keeps those platforms responsive instead.
    Q_UNUSED(p)
#endif
}

// How many probe/score workers to run: every core but one, capped. The point is
// the same as deprioritise() — the interface stays answerable while the footage
// is being read.
int workerCount(int cap) {
    const int cores = QThread::idealThreadCount();
    return std::max(1, std::min(cap, cores - 1));
}

// Run a command and return its trimmed stdout; empty on failure.
QString runOut(const QString &prog, const QStringList &args, bool *ok = nullptr, int timeoutMs = 20000) {
    QProcess p;
    p.setProcessChannelMode(QProcess::SeparateChannels);
    deprioritise(p);
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

// Where probe results are cached: `key` is the directory a clip path must start with
// to belong to this bucket. A folder source caches beside the footage, a loose file
// caches in the app's cache directory (never litter a user folder it merely came from).
struct CacheBucket {
    QString key;
    QString localPath;     // empty: never write here
    QString fallbackPath;
};

QList<CacheBucket> cacheBucketsFor(const QList<MediaSource> &sources) {
    QList<CacheBucket> buckets;
    QSet<QString> seen;
    auto norm = [](QString p) {
        while (p.size() > 1 && p.endsWith(QLatin1Char('/'))) p.chop(1);
        return p;
    };
    auto add = [&](CacheBucket b) {
        b.key = norm(b.key);
        if (seen.contains(b.key)) return;
        seen.insert(b.key);
        buckets.append(b);
    };
    for (const MediaSource &s : sources) {
        if (s.isDir) {
            add({s.path, localCacheFile(norm(s.path)), fallbackCacheFile(norm(s.path))});
        } else {
            const QString dir = norm(QFileInfo(s.path).absolutePath());
            add({dir, QString(), fallbackCacheFile(dir)});
        }
    }
    return buckets;
}

// A clip belongs to a bucket when its path sits *inside* that key directory.
bool inBucket(const QString &path, const QString &key) {
    const QString prefix = key.endsWith(QLatin1Char('/')) ? key : key + QLatin1Char('/');
    return path.startsWith(prefix);
}

void loadCaches(const QList<CacheBucket> &buckets, QHash<QString, QVariantMap> *cache,
                QHash<int, QString> *targets) {
    for (int i = 0; i < buckets.size(); i++) {
        const CacheBucket &b = buckets[i];
        for (const QString &p : {b.localPath, b.fallbackPath}) {
            if (p.isEmpty()) continue;
            QFile f(p);
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
            for (auto it = o.begin(); it != o.end(); ++it) cache->insert(it.key(), it.value().toObject().toVariantMap());
            targets->insert(i, p);
            break;
        }
    }
}

void writeCaches(const QList<CacheBucket> &buckets, const QHash<QString, QVariantMap> &cache,
                 const QHash<int, QString> &targets) {
    for (int i = 0; i < buckets.size(); i++) {
        const CacheBucket &b = buckets[i];
        QJsonObject own;
        for (auto it = cache.begin(); it != cache.end(); ++it) {
            if (!inBucket(it.key(), b.key)) continue;
            own.insert(it.key(), QJsonObject::fromVariantMap(it.value()));
        }
        if (own.isEmpty()) continue;
        const QByteArray json = QJsonDocument(own).toJson(QJsonDocument::Compact);
        const QString target = targets.contains(i) ? targets.value(i)
                               : (!b.localPath.isEmpty() ? b.localPath : b.fallbackPath);
        const bool wroteLocal = [&]() {
            QFile f(target);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
            return f.write(json) == json.size();
        }();
        if (wroteLocal) continue;
        QDir().mkpath(QFileInfo(b.fallbackPath).absolutePath());
        QFile f(b.fallbackPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(json);
    }
}

// One video file, probed. Returns false when it cannot be read (network hiccup,
// unsupported container), so the caller can count it instead of dropping it
// silently.
// Capture stamp from the file name, shared by the probe and the cache path.
void applyStamp(Clip *c, const QString &path) {
    QString day;
    double tod = 0;
    if (stampOf(path, &day, &tod)) { c->day = day; c->tod = tod; c->hasTod = true; }
}

// A cached probe is only trusted while it describes the same file: same byte size
// and mtime, and the same proxy (added, removed or replaced invalidates it).
bool probeCached(const QVariantMap &e, qint64 size, qint64 mtime, const QString &lrf, qint64 lrfSize) {
    if (e.isEmpty() || e.value(QStringLiteral("pv")).toInt() < 1) return false;
    if (e.value(QStringLiteral("s")).toLongLong() != size) return false;
    if (e.value(QStringLiteral("m")).toLongLong() != mtime) return false;
    if (!(e.value(QStringLiteral("dur")).toDouble() > 0)) return false;
    if (lrf.isEmpty()) return !e.contains(QStringLiteral("ls"));
    return e.contains(QStringLiteral("ld")) &&
           e.value(QStringLiteral("ls")).toLongLong() == lrfSize;
}

Clip clipFromCache(const QString &path, qint64 size, const QVariantMap &e,
                   const QString &lrf, qint64 lrfSize) {
    Clip c;
    c.path = path;
    c.ext = extOf(path);
    c.duration = e.value(QStringLiteral("dur")).toDouble();
    c.size = size;
    c.w = e.value(QStringLiteral("w")).toInt();
    c.h = e.value(QStringLiteral("h")).toInt();
    if (!lrf.isEmpty()) {
        c.lrf = lrf;
        c.lrfDur = e.value(QStringLiteral("ld")).toDouble();
        c.lrfSize = lrfSize;
    }
    applyStamp(&c, path);
    return c;
}

// Duration and dimensions in ONE ffprobe run. Asking twice meant opening the
// same file twice per clip, which on a network mount is two round trips and two
// chances to time out; the aspect-ratio filter needs the second answer, so the
// cheap fix is to stop asking separately.
bool probeFile(const QString &p, double *duration, int *w, int *h) {
    for (int attempt = 0; attempt < 2; attempt++) {
        bool ok = false;
        // A network mount answers slowly on the first touch: retry once with a
        // longer deadline before calling the file unreadable.
        const QString out = runOut(tools::path(QStringLiteral("ffprobe")),
                                   {QStringLiteral("-v"), QStringLiteral("error"),
                                    QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                                    QStringLiteral("-show_entries"), QStringLiteral("format=duration:stream=width,height"),
                                    QStringLiteral("-of"), QStringLiteral("json"), p},
                                   &ok, attempt == 0 ? 30000 : 60000);
        if (!ok) continue;
        const QJsonObject o = QJsonDocument::fromJson(out.toUtf8()).object();
        // ffprobe prints every value as a string in its JSON output.
        const double d = o.value(QStringLiteral("format")).toObject()
                          .value(QStringLiteral("duration")).toString().toDouble();
        if (!std::isfinite(d) || d <= 0) return false;
        const QJsonObject stream = o.value(QStringLiteral("streams")).toArray()
                                    .first().toObject();
        *duration = d;
        if (w) *w = stream.value(QStringLiteral("width")).toInt();
        if (h) *h = stream.value(QStringLiteral("height")).toInt();
        return true;
    }
    return false;
}

bool probeClip(const QString &path, qint64 size, const QHash<QString, QString> &lrfFor, Clip *out) {
    double d = 0;
    int w = 0, h = 0;
    if (!probeFile(path, &d, &w, &h)) return false;

    Clip c;
    c.path = path;
    c.ext = extOf(path);
    c.duration = d;
    c.size = size;
    c.w = w;
    c.h = h;
    applyStamp(&c, path);

    const QString lrf = lrfFor.value(stemOf(path));
    if (!lrf.isEmpty()) {
        double ld = 0;
        if (probeFile(lrf, &ld, nullptr, nullptr)) {
            c.lrf = lrf;
            c.lrfDur = ld;
            c.lrfSize = QFileInfo(lrf).size();
        }
    }
    *out = c;
    return true;
}

// Walks every configured source and probes each video. Pure: builds its own
// list, touches no members, so it is safe to run on a worker thread.
//
// Two things this has to get right on a real footage drive:
//  - `.LRF` files are DJI proxies, not clips: they are paired by stem with their
//    `.MP4` twin and never indexed on their own.
//  - The same clip is often copied into several date folders. A duplicate is the
//    same file name at the same byte size; it is dropped before probing, so the
//    edit cannot show the same shot twice by accident.
Library::ScanOutcome scanSources(const QList<MediaSource> &sources, const QSet<QString> &hidden,
                                 const QList<CacheBucket> &buckets,
                                 const std::function<void(int, int)> &progress) {
    QHash<QString, QString> lrfFor;   // stem -> proxy path
    QStringList candidates;
    for (const MediaSource &s : sources) {
        if (s.isDir) {
            QDirIterator it(s.path, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString path = it.next();
                const QString e = extOf(path);
                if (e == QLatin1String("lrf")) lrfFor.insert(stemOf(path), path);
                else if (videoExts().contains(e) && !hidden.contains(path)) candidates.append(path);
            }
        } else {
            if (hidden.contains(s.path)) continue;
            if (!videoExts().contains(extOf(s.path))) continue;
            candidates.append(s.path);
            // A loose file gets the same .LRF pairing its folder walk would have given it.
            const QDir parent = QFileInfo(s.path).absoluteDir();
            const QString stem = stemOf(s.path);
            for (const QString &sibling : parent.entryList({QStringLiteral("*.[lL][rR][fF]")}, QDir::Files))
                if (stemOf(sibling) == stemOf(QFileInfo(s.path).fileName()))
                    lrfFor.insert(stem, parent.absoluteFilePath(sibling));
        }
    }
    // Sort first, dedupe second: the copy that wins must be the same one every
    // run, and it must be the earliest path — a later copy may have no .LRF twin
    // next to it, and then the pairing is lost for no reason.
    std::sort(candidates.begin(), candidates.end());

    QList<Library::ScanFile> files;
    QSet<QString> seen;               // dedupe key: file name + size
    for (const QString &path : candidates) {
        const QFileInfo fi(path);
        const QString key = fi.fileName().toLower() + QLatin1Char('|') + QString::number(fi.size());
        if (seen.contains(key)) continue;
        seen.insert(key);
        files.append({path, fi.size()});
    }

    // Probe results are cached beside the scores, so a folder that has not changed
    // since the last scan opens no file at all: not one ffprobe per clip.
    QHash<QString, QVariantMap> cache;
    QHash<int, QString> cachePaths;
    loadCaches(buckets, &cache, &cachePaths);

    // Probe in parallel: every ffprobe over SMB/NFS is a round trip, and running
    // them one by one turns a few hundred clips into a coffee break.
    QList<Clip> probed;
    probed.resize(files.size());
    QList<bool> ok;
    ok.resize(files.size());
    std::atomic<int> next{0};
    std::atomic<int> done{0};
    std::mutex cacheLock;
    std::atomic<bool> flushing{false};

    // A scan of a network share can run for minutes; the cache is flushed as it
    // goes so closing the window early does not throw the probes away.
    auto flush = [&]() {
        if (flushing.exchange(true)) return;
        QHash<QString, QVariantMap> copy;
        {
            std::lock_guard<std::mutex> lock(cacheLock);
            copy = cache;
        }
        writeCaches(buckets, copy, cachePaths);
        flushing = false;
    };

    auto work = [&]() {
        for (;;) {
            const int i = next.fetch_add(1);
            if (i >= int(files.size())) break;
            const QString path = files[i].path;
            const qint64 size = files[i].size;
            const qint64 mtime = qint64(QFileInfo(path).lastModified().toMSecsSinceEpoch());
            const QString lrf = lrfFor.value(stemOf(path));
            const qint64 lrfSize = lrf.isEmpty() ? 0 : QFileInfo(lrf).size();

            QVariantMap hit;
            {
                std::lock_guard<std::mutex> lock(cacheLock);
                hit = cache.value(path);
            }

            Clip c;
            bool readable = false;
            if (probeCached(hit, size, mtime, lrf, lrfSize)) {
                c = clipFromCache(path, size, hit, lrf, lrfSize);
                readable = true;
            } else {
                readable = probeClip(path, size, lrfFor, &c);
            }
            if (readable) {
                // Merge: the entry may already hold this clip's score.
                QVariantMap entry = hit;
                entry[QStringLiteral("pv")] = 1;
                entry[QStringLiteral("s")] = size;
                entry[QStringLiteral("m")] = mtime;
                entry[QStringLiteral("dur")] = c.duration;
                entry[QStringLiteral("w")] = c.w;
                entry[QStringLiteral("h")] = c.h;
                if (!c.lrf.isEmpty()) {
                    entry[QStringLiteral("ls")] = c.lrfSize;
                    entry[QStringLiteral("ld")] = c.lrfDur;
                } else {
                    entry.remove(QStringLiteral("ls"));
                    entry.remove(QStringLiteral("ld"));
                }
                std::lock_guard<std::mutex> lock(cacheLock);
                cache.insert(path, entry);
            }
            probed[i] = c;
            ok[i] = readable;
            const int d = ++done;
            // Report early and often: on a network share the first files take a
            // while, and a bar that only moves every fourth file looks dead.
            const int totalFiles = int(files.size());
            if (d <= 8 || d % 4 == 0 || d == totalFiles) progress(d, totalFiles);
            if (d % 64 == 0) flush();
        }
    };

    const int threads = workerCount(6);
    if (threads <= 1 || files.size() <= 2) {
        work();
    } else {
        std::vector<std::thread> pool;
        pool.reserve(size_t(threads));
        for (int t = 0; t < threads; t++) pool.emplace_back(work);
        for (auto &t : pool) t.join();
    }
    flush();

    Library::ScanOutcome outcome;
    outcome.files = files.size();
    for (int i = 0; i < probed.size(); i++) {
        if (!ok[i]) { outcome.skipped++; continue; }
        outcome.clips.append(probed[i]);
    }
    std::sort(outcome.clips.begin(), outcome.clips.end(),
              [](const Clip &a, const Clip &b) { return a.path < b.path; });
    for (int i = 0; i < outcome.clips.size(); i++) outcome.clips[i].id = i;
    return outcome;
}


bool isRemotePath(const QString &path) {
    return path.contains(QLatin1String("/gvfs/")) || path.startsWith(QLatin1String("//")) ||
           path.startsWith(QLatin1String("/mnt/")) || path.startsWith(QLatin1String("/media/"));
}

// How many frames a clip will be sampled at. Used for both the work and the exact
// total, so the progress bar measures frames rather than whole clips.
int sampleCount(const Clip &c) {
    const double dur = (c.lrfDur > 0) ? c.lrfDur : c.duration;
    const QString path = (c.lrfDur > 0) ? c.lrf : c.path;
    // Every sample is a separate ffmpeg invocation; on a network mount each one
    // costs a round trip, so take fewer of them. The score moves by a few points,
    // the wait moves by minutes.
    const int maxSamples = isRemotePath(path) ? 5 : 12;
    return std::min(maxSamples, std::max(3, int(std::ceil(dur / 4.0))));
}

// Up to 12 evenly spaced 32x18 gray frames, scored for motion, colour, exposure
// and variety. Pure and thread-safe: shells out to ffmpeg, touches no members.
// `sampleDone` runs after each frame lands, so progress moves inside a clip too.
bool scoreClip(const Clip &c, int *score, int *mv, double *dk, const std::function<void()> &sampleDone = {}) {
    const double dur = (c.lrfDur > 0) ? c.lrfDur : c.duration;
    const QString path = (c.lrfDur > 0) ? c.lrf : c.path;
    const int want = sampleCount(c);
    QList<QByteArray> samples;
    const int px = 32 * 18;
    for (int s = 0; s < want; s++) {
        const double t = std::round(((dur / (want + 1)) * (s + 1)) * 10.0) / 10.0;
        QProcess p;
        deprioritise(p);
        p.start(tools::path(QStringLiteral("ffmpeg")),
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
        if (sampleDone) sampleDone();
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

void Library::setBusyInternal(bool b) {
    if (m_busy == b) return;
    m_busy = b;
    emit busyChanged();
}

QString Library::sourcesLabel() const {
    QStringList names;
    for (const MediaSource &s : m_sources) {
        const QString name = QFileInfo(s.path).fileName();
        names.append(name.isEmpty() ? s.path : name);
    }
    if (names.size() > 3) {
        const int more = names.size() - 3;
        names = names.mid(0, 3);
        return names.join(QStringLiteral(", ")) + QStringLiteral(" and %1 more").arg(more);
    }
    return names.join(QStringLiteral(", "));
}

void Library::dropClips(const QSet<QString> &paths) {
    if (paths.isEmpty()) return;
    QList<Clip> keep;
    keep.reserve(m_clips.size());
    for (const Clip &c : m_clips)
        if (!paths.contains(c.path)) keep.append(c);
    if (keep.size() == m_clips.size()) return;
    m_clips = keep;
    m_indexById.clear();
    for (int i = 0; i < m_clips.size(); i++) m_indexById.insert(m_clips[i].id, i);
    emit indexed();
}

void Library::startIndex() {
    if (m_busy) {
        m_restart = true;
        return;
    }
    m_restart = false;
    setBusyInternal(true);
    m_scoreDone = 0;
    m_scoreTotal = 0;
    m_scanDone = 0;
    m_scanTotal = 0;
    const quint64 gen = ++m_gen;
    const QList<MediaSource> sources = m_sources;
    const QSet<QString> hidden = m_hidden;
    const QList<CacheBucket> buckets = cacheBucketsFor(sources);

    // The index pass shells out to ffprobe per file: keep it off the UI thread.
    QThread *t = QThread::create([this, gen, sources, hidden, buckets]() {
        auto report = [this](int done, int total) {
            QMetaObject::invokeMethod(this, [this, done, total]() {
                m_scanDone = done;
                m_scanTotal = total;
                emit scanProgress(done, total);
            }, Qt::QueuedConnection);
        };
        const ScanOutcome outcome = scanSources(sources, hidden, buckets, report);
        QMetaObject::invokeMethod(this, [this, gen, outcome]() { applyIndex(gen, outcome); }, Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void Library::reset() {
    // Supersede any pass still in flight: its results are now stale.
    ++m_gen;
    setBusyInternal(false);
    m_restart = false;
    m_scoreDone = 0;
    m_scoreTotal = 0;
    m_clips.clear();
    m_indexById.clear();
    emit indexed();
    emit scoringProgress(0, 0);
}

void Library::applyIndex(quint64 gen, const ScanOutcome &outcome) {
    if (gen != m_gen) return;
    m_clips = outcome.clips;
    m_indexById.clear();
    for (int i = 0; i < m_clips.size(); i++) m_indexById.insert(m_clips[i].id, i);

    // The folders changed while this scan ran: the result is already outdated.
    if (m_restart) {
        restartIfPending();
        return;
    }

    if (m_clips.isEmpty()) {
        setBusyInternal(false);
        // Distinguish "nothing there" from "nothing readable": over a network
        // mount those are very different problems.
        if (outcome.skipped > 0)
            emit failed(QStringLiteral("Could not read %1 of %2 videos in %3")
                            .arg(outcome.skipped).arg(outcome.files).arg(sourcesLabel()));
        else
            emit failed(QStringLiteral("No playable videos found in %1").arg(sourcesLabel()));
        emit indexed();
        return;
    }
    emit indexed();
    scoreClips(gen);
}

void Library::restartIfPending() {
    if (!m_restart) return;
    m_restart = false;
    setBusyInternal(false);
    startIndex();
}

void Library::scoreClips(quint64 gen) {
    m_scoreDone = 0;
    m_scoreTotal = m_clips.size();
    m_scoreFrac = 0;
    emit scoringProgress(0, m_scoreTotal);

    // Snapshot everything the worker needs; the live list stays on this thread.
    const QList<Clip> snapshot = m_clips;
    const QList<CacheBucket> buckets = cacheBucketsFor(m_sources);

    QThread *t = QThread::create([this, gen, snapshot, buckets]() {
        QHash<QString, QVariantMap> cache;
        QHash<int, QString> cachePaths;
        loadCaches(buckets, &cache, &cachePaths);

        // Scoring is 12 ffmpeg spawns per clip; over SMB that is the slow part of
        // the app by an order of magnitude, so the clips are spread over threads
        // and the cache is flushed as we go (a scan of a network share can take
        // long enough that the user closes the window first).
        std::mutex cacheLock;
        std::atomic<int> next{0};
        std::atomic<int> done{0};
        std::atomic<bool> flushing{false};

        // Progress is measured in sampled frames, not clips: a single long clip on
        // a slow share would otherwise leave the bar parked at zero for minutes.
        int unitsTotal = 0;
        for (const Clip &c : snapshot) unitsTotal += sampleCount(c);
        std::atomic<int> unitsDone{0};
        std::atomic<qint64> lastFractionPost{0};

        std::vector<Score> results;
        results.resize(size_t(snapshot.size()));

        auto postFraction = [this, gen, &unitsDone, unitsTotal, &lastFractionPost]() {
            if (unitsTotal <= 0) return;
            // Throttle: a queued call per sampled frame would flood the GUI thread.
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            const qint64 prev = lastFractionPost.load();
            if (now - prev < 120) return;
            lastFractionPost.store(now);
            const double frac = double(unitsDone.load()) / double(unitsTotal);
            QMetaObject::invokeMethod(this, [this, gen, frac]() {
                if (gen != m_gen) return;
                setScoreFrac(frac);
                emit scoringProgress(m_scoreDone, m_scoreTotal);
            }, Qt::QueuedConnection);
        };

        auto postProgress = [this, gen](int d, int total) {
            QMetaObject::invokeMethod(this, [this, gen, d, total]() {
                if (gen != m_gen) return;
                m_scoreDone = d;
                emit scoringProgress(d, total);
            }, Qt::QueuedConnection);
        };

        auto flush = [&]() {
            if (flushing.exchange(true)) return;
            QHash<QString, QVariantMap> copy;
            {
                std::lock_guard<std::mutex> lock(cacheLock);
                copy = cache;
            }
            writeCaches(buckets, copy, cachePaths);
            flushing = false;
        };

        const int total = snapshot.size();
        auto work = [&]() {
            for (;;) {
                const int i = next.fetch_add(1);
                if (i >= total) break;
                const Clip &c = snapshot[i];
                const QFileInfo fi(c.path);
                const qint64 sizeNow = fi.size();
                const qint64 mtimeNow = qint64(fi.lastModified().toMSecsSinceEpoch());

                QVariantMap hit;
                {
                    std::lock_guard<std::mutex> lock(cacheLock);
                    hit = cache.value(c.path);
                }
                const bool cacheOk = hit.value("ver").toInt() == 2 && hit.contains("mv") && hit.contains("dk") &&
                                     hit.value("s").toLongLong() == sizeNow && hit.value("m").toLongLong() == mtimeNow &&
                                     (c.lrf.isEmpty() || hit.value("ls").toLongLong() == c.lrfSize);

                Score s;
                if (cacheOk) {
                    s.score = hit.value("v").toInt();
                    s.mv = hit.value("mv").toInt();
                    s.dk = hit.value("dk").toDouble();
                    s.ok = true;
                    unitsDone += sampleCount(c);
                    postFraction();
                } else {
                    int counted = 0;
                    s.ok = scoreClip(c, &s.score, &s.mv, &s.dk, [&]() {
                        ++counted;
                        unitsDone += 1;
                        postFraction();
                    });
                    // Frames that could not be read are still work accounted for,
                    // otherwise a failed clip would leave the bar short of 100 %.
                    unitsDone += std::max(0, sampleCount(c) - counted);
                    postFraction();
                    // Unreadable is not "dark": scoring 0 with dk=1 would let the
                    // junk filter silently drop a clip the network failed on.
                    if (!s.ok) { s.score = 0; s.mv = 0; s.dk = 0.25; }
                    // Merge into the entry the probe pass left behind: the commit
                    // must not throw away the duration and dimensions it cached.
                    QVariantMap entry = hit;
                    entry["s"] = sizeNow;
                    entry["m"] = mtimeNow;
                    if (!c.lrf.isEmpty()) entry["ls"] = c.lrfSize;
                    entry["v"] = s.score;
                    entry["mv"] = s.mv;
                    entry["dk"] = s.dk;
                    entry["ver"] = 2;
                    std::lock_guard<std::mutex> lock(cacheLock);
                    cache.insert(c.path, entry);
                }
                results[size_t(i)] = s;

                const int d = ++done;
                if (d % 2 == 0 || d == total) postProgress(d, total);
                if (!cacheOk && d % 16 == 0) flush();
            }
        };

        const int threads = workerCount(6);
        if (threads <= 1 || total <= 1) {
            work();
        } else {
            std::vector<std::thread> pool;
            pool.reserve(size_t(threads));
            for (int k = 0; k < threads; k++) pool.emplace_back(work);
            for (auto &th : pool) th.join();
        }

        flush();
        QList<Score> scores;
        scores.reserve(total);
        for (int i = 0; i < total; i++) scores.append(results[size_t(i)]);
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
    setBusyInternal(false);
    m_scoreDone = m_scoreTotal;
    emit scoringProgress(m_scoreTotal, m_scoreTotal);
    emit scoringFinished();
    restartIfPending();
}

QList<Clip> Library::poolFor(const Filters &f) const {
    const QList<SkipWindow> wins = f.skipWindowsOn ? parseWindows(f.skipWindows) : QList<SkipWindow>();
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
