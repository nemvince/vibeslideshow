#include "core/Tools.h"
#include "ui/Studio.h"
#include "core/Library.h"
#include "core/Scheduler.h"
#include "core/Analyzer.h"
#include "player/Show.h"
#include "ui/ClipModel.h"
#include "ui/TimelineModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QProcess>
#include <QThread>
#include <QDateTime>
#include <algorithm>
#include <cmath>

namespace {

// The pool filter reaches the UI as a flat map so controls can bind to it.
QVariantMap filtersToMap(const Filters &f) {
    QVariantMap m;
    m["junk"] = f.junk;
    m["skipWindowsOn"] = f.skipWindowsOn;
    m["skipWindows"] = f.skipWindows;
    m["dayOn"] = f.dayOn;
    m["day"] = f.day;
    m["scoreOn"] = f.scoreOn;
    m["scoreMin"] = f.scoreMin;
    m["scorePct"] = f.scorePct;
    m["mvOn"] = f.mvOn;
    m["mvMin"] = f.mvMin;
    m["mvPct"] = f.mvPct;
    m["arOn"] = f.arOn;
    m["ar"] = f.ar;
    m["todOn"] = f.todOn;
    QVariantList tod;
    for (const auto &r : f.tod) {
        QVariantMap o;
        o["a"] = r.a;
        o["b"] = r.b;
        o["wrap"] = r.wrap;
        tod.append(o);
    }
    m["tod"] = tod;
    return m;
}

// A location mounted through Nautilus/Dolphin (SMB, SFTP, NFS, WebDAV) comes
// back from the file picker as a URL, not a local path, and the only thing this
// app can scan is the FUSE mount GVFS creates for it under $XDG_RUNTIME_DIR.
// Map the URL onto that mount: "smb://nas/media/trip" becomes
// "/run/user/1000/gvfs/smb-share:server=nas,share=media/trip".
QString resolveGvfs(const QUrl &url) {
    static const QStringList remoteSchemes = {
        QStringLiteral("smb"), QStringLiteral("sftp"), QStringLiteral("ftp"),
        QStringLiteral("nfs"), QStringLiteral("dav"), QStringLiteral("davs"),
        QStringLiteral("afp"), QStringLiteral("ssh")};
    const QString scheme = url.scheme().toLower();
    if (!remoteSchemes.contains(scheme)) return {};

    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty()) return {};
    const QString gvfs = runtime + QStringLiteral("/gvfs");
    const QDir mounts(gvfs);
    if (!mounts.exists()) return {};

    const QString host = url.host().toLower();
    if (host.isEmpty()) return {};
    const QStringList segs = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);

    // Mount directory names look like "smb-share:server=nas,share=media" or
    // "sftp:host=box,user=me". Match the host, and prefer the share named by the
    // first path segment.
    QString best;
    for (const QString &name : mounts.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString lower = name.toLower();
        if (!lower.startsWith(scheme)) continue;
        if (!lower.contains(QStringLiteral("server=") + host) &&
            !lower.contains(QStringLiteral("host=") + host)) continue;
        if (!segs.isEmpty() && lower.contains(QStringLiteral("share=") + segs.first().toLower())) {
            best = name;
            break;
        }
        if (best.isEmpty()) best = name;
    }
    if (best.isEmpty()) return {};

    QString path = gvfs + QLatin1Char('/') + best;
    // The share is part of the mount name, not of the path inside it.
    int first = 0;
    if (!segs.isEmpty() && best.toLower().contains(QStringLiteral("share=") + segs.first().toLower())) first = 1;
    for (int i = first; i < segs.size(); i++) path += QLatin1Char('/') + segs[i];
    return path;
}

// The one place a picker URL becomes something the library can walk.
QString localPathFor(const QUrl &url) {
    if (url.isLocalFile()) return url.toLocalFile();
    const QString mounted = resolveGvfs(url);
    if (!mounted.isEmpty()) return mounted;
    // Not a URL at all (a plain path handed over by a script or a saved session).
    if (url.scheme().isEmpty()) return url.toString();
    return {};
}

// Inverse of filtersToMap: one place that knows the shape, so a filter saved in
// params.json, a saved look and the live UI can never drift apart.
Filters filtersFromMap(const QVariantMap &m) {
    Filters f;
    if (m.isEmpty()) return f;
    f.junk = m.value("junk", true).toBool();
    f.skipWindowsOn = m.value("skipWindowsOn", !f.skipWindows.isEmpty()).toBool();
    f.skipWindows = m.value("skipWindows", f.skipWindows).toString();
    f.dayOn = m.value("dayOn", false).toBool();
    f.day = m.value("day").toString();
    f.scoreOn = m.value("scoreOn", false).toBool();
    f.scoreMin = m.value("scoreMin", 0).toInt();
    f.scorePct = m.value("scorePct", f.scorePct).toInt();
    f.mvOn = m.value("mvOn", false).toBool();
    f.mvMin = m.value("mvMin", 0).toInt();
    f.mvPct = m.value("mvPct", f.mvPct).toInt();
    f.arOn = m.value("arOn", false).toBool();
    f.ar = m.value("ar").toDouble();
    f.todOn = m.value("todOn", false).toBool();
    const QVariantList tod = m.value("tod").toList();
    for (const auto &v : tod) {
        const QVariantMap r = v.toMap();
        f.tod.append(todRange(r.value("a").toDouble(), r.value("b").toDouble()));
    }
    return f;
}

} // namespace

Studio::Studio(QObject *parent) : QObject(parent) {
    m_lib = new Library(this);
    m_sched = std::make_unique<Scheduler>();
    m_clips = new ClipModel(this);
    m_timeline = new TimelineModel(this);
    m_show = new Show(this);

    m_timeline->setLibrary(m_lib);
    m_timeline->setScheduler(m_sched.get());
    m_timeline->setClipModel(m_clips);
    m_show->setLibrary(m_lib);
    m_show->setScheduler(m_sched.get());

    connect(m_show, &Show::volumeChanged, this, &Studio::volumeChanged);
    // The progress label lives and dies with busy; refresh it when either flips.
    connect(this, &Studio::busyChanged, this, &Studio::progressChanged);

    connect(m_lib, &Library::indexed, this, [this]() {
        reloadLibraryModel();
        emit libraryChanged();
        emit filtersChanged();
        persist();
        // The index was just rebuilt, and every clip id with it. An edit cut from
        // the old index no longer names the clips it was built from, so recut it
        // from the new one rather than leave segments pointing at other files.
        if (m_timeline->hasTimeline() && !m_timeline->segments().isEmpty()) generate();
    });
    connect(m_lib, &Library::scanProgress, this, [this](int done, int total) {
        // A progress event can still be queued when the pass finishes; do not let
        // it overwrite the "N clips ready" message with a stale "Scanning…".
        if (busy()) setStatus(QStringLiteral("Scanning footage… %1/%2").arg(done).arg(total));
        noteProgress(done, total);
    });
    connect(m_lib, &Library::scoringProgress, this, [this](int done, int total) { noteProgress(done, total); });
    connect(m_lib, &Library::busyChanged, this, [this]() {
        emit busyChanged();
        emit progressChanged();
    });
    connect(m_lib, &Library::scoringFinished, this, [this]() {
        setStatus(QStringLiteral("%1 clips ready").arg(m_lib->clips().size()));
        reloadLibraryModel();
        emit libraryChanged();
        emit filtersChanged();
        syncScheduler();
        if (m_pendingRegenerate) {
            m_pendingRegenerate = false;
            generate();
            return;
        }
        // The visible edit was cut from unscored clips; now that the real scores
        // are in, re-cut it — unless the user has already been editing by hand.
        if (m_editUnscored && m_timeline->hasTimeline() && m_timeline->edits() == 0) {
            m_editUnscored = false;
            generate();
        }
    });
    connect(m_lib, &Library::failed, this, [this](const QString &m) { setStatus(m); });

    connect(m_timeline, &TimelineModel::changed, this, [this]() {
        syncScheduler();
        emit editChanged();
        // Always forward it, empty included: when the last footage folder is
        // removed the player has to let go of the edit it is holding.
        m_show->setEdit(m_timeline->segments(), m_timeline->cuts(), m_timeline->fades());
    });
    connect(m_timeline, &TimelineModel::countChanged, this, [this]() { emit editChanged(); });

    syncScheduler();
    refreshProfiles();
    restoreSession();
}

Studio::~Studio() = default;

QString Studio::configDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/vibeslideshow");
}

double Studio::volume() const { return m_show ? m_show->volume() : 0; }

void Studio::setVolume(double v) {
    if (m_show) m_show->setVolume(v);
}

// ---- session persistence -------------------------------------------------

void Studio::restoreSession() {
    // Params and folders are the document: restoring them is what makes the app
    // reopen where the user left off.
    QFile pf(configDir() + QStringLiteral("/params.json"));
    if (pf.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(pf.readAll()).object();
        m_params.applyMap(o.value("params").toObject().toVariantMap());
        m_activePreset = o.value("preset").toString();
        const QJsonArray dirs = o.value("dirs").toArray();
        for (const auto &d : dirs) m_dirs.append(d.toString());
        const QString song = o.value("song").toString();
        if (!song.isEmpty() && QFileInfo::exists(song)) m_song = song;
        // Filters ride along so a reopened app shows the same pool.
        const QJsonObject fo = o.value("filters").toObject();
        if (!fo.isEmpty()) applyFilters(fo.toVariantMap());
        m_clips->setFilters(m_filters);
        emit paramsChanged();
        emit activePresetChanged();
        emit filtersChanged();
    }
    if (!m_dirs.isEmpty()) {
        m_lib->setDirs(m_dirs);
        m_lib->startIndex();
    }
    if (!m_song.isEmpty()) loadSongFile(m_song);
}

void Studio::persist() {
    QDir().mkpath(configDir());
    QJsonObject o;
    o["params"] = QJsonObject::fromVariantMap(m_params.toMap());
    o["preset"] = m_activePreset;
    QJsonArray dirs;
    for (const QString &d : m_dirs) dirs.append(d);
    o["dirs"] = dirs;
    o["song"] = m_song;
    o["filters"] = QJsonObject::fromVariantMap(filtersToMap(m_filters));
    QFile f(configDir() + QStringLiteral("/params.json"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
}

void Studio::saveNow() { persist(); }

void Studio::refreshProfiles() {
    QFile f(configDir() + QStringLiteral("/profiles.json"));
    if (f.open(QIODevice::ReadOnly)) m_profiles = QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
    emit profilesChanged();
    emit looksChanged();
}

void Studio::writeProfiles() {
    QDir().mkpath(configDir());
    QFile f(configDir() + QStringLiteral("/profiles.json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(QJsonObject::fromVariantMap(m_profiles)).toJson(QJsonDocument::Indented));
}

QVariantList Studio::looks() const {
    QVariantList out;
    for (const Preset &p : builtinPresets()) {
        QVariantMap row;
        row["name"] = p.name;
        row["blurb"] = p.blurb;
        row["saved"] = false;
        row["active"] = m_activeProfile.isEmpty() && p.name == m_activePreset;
        out.append(row);
    }
    QStringList names = m_profiles.keys();
    std::sort(names.begin(), names.end());
    for (const QString &n : names) {
        QVariantMap row;
        row["name"] = n;
        row["blurb"] = QStringLiteral("Saved look");
        row["saved"] = true;
        row["active"] = n == m_activeProfile;
        out.append(row);
    }
    return out;
}

QVariantList Studio::profiles() const {
    QVariantList out;
    QStringList names = m_profiles.keys();
    std::sort(names.begin(), names.end());
    for (const QString &n : names) out.append(n);
    return out;
}

void Studio::saveProfile(const QString &name) {
    if (name.trimmed().isEmpty()) return;
    QVariantMap entry;
    entry["params"] = m_params.toMap();
    entry["preset"] = m_activePreset;
    entry["filters"] = filtersToMap(m_filters);
    m_profiles.insert(name.trimmed(), entry);
    writeProfiles();
    setStatus(QStringLiteral("Saved look “%1”").arg(name.trimmed()));
    refreshProfiles();
    emit looksChanged();
}

void Studio::loadProfile(const QString &name) {
    if (!m_profiles.contains(name)) return;
    const QVariantMap entry = m_profiles.value(name).toMap();
    m_params.applyMap(entry.value("params").toMap());
    m_activePreset = entry.value("preset").toString();
    m_activeProfile = name;
    if (!entry.value("filters").toMap().isEmpty()) applyFilters(entry.value("filters").toMap());
    syncScheduler();
    m_clips->setFilters(m_filters);
    emit paramsChanged();
    emit activePresetChanged();
    emit filtersChanged();
    persist();
    setStatus(QStringLiteral("Loaded look “%1”").arg(name));
}

void Studio::deleteProfile(const QString &name) {
    if (!m_profiles.remove(name)) return;
    // Deleting the look in use leaves its parameters alone, but nothing is
    // "active" any more: the picker must not keep pointing at a name that no
    // longer exists.
    if (m_activeProfile == name) {
        m_activeProfile.clear();
        emit activePresetChanged();
    }
    writeProfiles();
    refreshProfiles();
    emit looksChanged();
}

// ---- library -------------------------------------------------------------

void Studio::setStatus(const QString &s) {
    if (m_status == s) return;
    m_status = s;
    emit statusChanged();
}

void Studio::setSongBusy(bool b) {
    if (m_songBusy == b) return;
    m_songBusy = b;
    emit busyChanged();
}

bool Studio::busy() const {
    return m_songBusy || (m_lib && m_lib->busy());
}

int Studio::scoreDone() const { return m_lib ? m_lib->scoreDone() : 0; }
int Studio::scoreTotal() const { return m_lib ? m_lib->scoreTotal() : 0; }

double Studio::progress() const {
    // Negative means "working, no total yet": the line sweeps instead of sitting
    // at zero, which is indistinguishable from a stuck app.
    if (!busy() || !m_lib) return -1;
    // A phase that has counted nothing yet is "unknown", not "0%": the bar sweeps
    // instead of drawing a sliver, which is what made it look absent.
    if (m_lib->scoreTotal() > 0) {
        // Sampled-frame fraction: moves during the very first clip.
        const double f = m_lib->scoreFraction();
        if (f > 0.0005) return qBound(0.0, f, 1.0);
        return -1;
    }
    if (m_lib->scanTotal() > 0 && m_lib->scanDone() > 0)
        return qBound(0.0, double(m_lib->scanDone()) / double(m_lib->scanTotal()), 1.0);
    return -1;
}

QString Studio::progressLabel() const {
    // Self-contained: the top bar shows this instead of the last status message
    // while a pass runs, so it has to say what the number refers to.
    if (m_songBusy) return QStringLiteral("Listening to the song…");
    if (!m_lib || !m_lib->busy()) return {};
    if (m_lib->scoreTotal() > 0) {
        // Whole clips finish slowly on a network share; until the first one lands
        // the percentage is the only honest number.
        if (m_lib->scoreDone() > 0)
            return QStringLiteral("Scoring %1 of %2%3")
                .arg(m_lib->scoreDone()).arg(m_lib->scoreTotal())
                .arg(etaSuffix(m_lib->scoreDone(), m_lib->scoreTotal()));
        const int pct = int(std::lround(m_lib->scoreFraction() * 100));
        return QStringLiteral("Scoring clips · %1%2")
            .arg(pct)
            .arg(etaFraction(m_lib->scoreFraction()));
    }
    if (m_lib->scanTotal() > 0)
        return QStringLiteral("Found %1 videos · probed %2%3")
            .arg(m_lib->scanTotal()).arg(m_lib->scanDone())
            .arg(etaSuffix(m_lib->scanDone(), m_lib->scanTotal()));
    return QStringLiteral("Looking through the folders…");
}

QString Studio::phase() const {
    if (m_songBusy) return QStringLiteral("Listening to the song");
    if (!m_lib || !m_lib->busy()) return {};
    if (m_lib->scoreTotal() > 0) return QStringLiteral("Scoring clips");
    return QStringLiteral("Scanning footage");
}

bool Studio::workspaceEnabled() const {
    // A rescan renumbers clip ids while the open edit still holds the old ones,
    // so nothing may play or be edited until the index has settled. Scoring only
    // fills in numbers the ids do not depend on: the show stays usable.
    if (m_songBusy) return false;
    return !(m_lib && m_lib->busy() && m_lib->scoreTotal() == 0);
}

QString Studio::etaFraction(double frac) const {
    const double elapsed = m_phaseClock.isValid() ? m_phaseClock.elapsed() / 1000.0 : 0.0;
    if (elapsed < 2.0 || frac <= 0.001) return QStringLiteral(" · estimating…");
    const double rate = frac / elapsed;
    if (rate <= 0) return {};
    const int left = int(std::ceil((1.0 - frac) / rate));
    if (left <= 0) return QStringLiteral(" · almost done");
    if (left < 60) return QStringLiteral(" · ~%1s left").arg(left);
    return QStringLiteral(" · ~%1 min left").arg(int(std::ceil(left / 60.0)));
}

QString Studio::etaSuffix(int done, int total) const {
    if (total <= 0 || done <= 0) return {};
    const double elapsed = m_phaseClock.isValid() ? m_phaseClock.elapsed() / 1000.0 : 0.0;
    // Below a couple of seconds the rate is noise, and it jumps around once the
    // first slow clip lands; better to say we are still measuring.
    if (elapsed < 2.0 || done < 2) return QStringLiteral(" · estimating…");
    const double rate = done / elapsed;
    if (rate <= 0) return {};
    const int left = int(std::ceil((total - done) / rate));
    if (left <= 0) return QStringLiteral(" · almost done");
    if (left < 60) return QStringLiteral(" · ~%1s left").arg(left);
    return QStringLiteral(" · ~%1 min left").arg(int(std::ceil(left / 60.0)));
}

void Studio::noteProgress(int done, int total) {
    // A different total means a new pass started (new scan, new library).
    // Restarting on every zero-progress tick would keep the ETA at "estimating".
    if (total != m_phaseTotal) {
        m_phaseTotal = total;
        m_phaseClock.restart();
    }
    emit progressChanged();
}

void Studio::startIndexing(const QStringList &dirs) {
    // Keep the list the caller gave us. Validating here and discarding what does
    // not stat *yet* is how a network mount (slow FUSE, GVFS) disappeared from
    // the UI the moment it was added: the row was emitted, then removed.
    QStringList clean;
    for (const QString &d : dirs) {
        if (d.isEmpty()) continue;
        const QString abs = QFileInfo(d).absoluteFilePath();
        if (!clean.contains(abs)) clean.append(abs);
    }
    m_dirs = clean;
    emit videoDirsChanged();

    if (m_dirs.isEmpty()) {
        m_lib->setDirs(QStringList());
        m_lib->reset();
        setStatus(QStringLiteral("Add a footage folder to begin"));
        persist();
        return;
    }
    // Every clip id is renumbered by the scan that follows: a show playing against
    // the old index would cut to the wrong files. Stop it and recut once indexed.
    if (m_show->playing()) m_show->stop();
    m_lib->setDirs(m_dirs);
    m_lib->startIndex();
    persist();
}

void Studio::addFolder(const QUrl &url) {
    const QString dir = localPathFor(url);
    if (dir.isEmpty()) {
        setStatus(QStringLiteral("That location has no path this app can read: %1").arg(url.toString()));
        return;
    }
    const QFileInfo fi(dir);
    if (!fi.exists() || !fi.isDir()) {
        // A remote mount that GVFS has not materialised yet, or a stale saved
        // location: say so instead of doing nothing visible.
        setStatus(QStringLiteral("Cannot open %1 — is the network drive mounted?").arg(dir));
        return;
    }
    const QString abs = fi.absoluteFilePath();
    if (m_dirs.contains(abs)) {
        // Already known: honour the click as "scan it again" instead of nothing.
        setStatus(QStringLiteral("Rescanning %1").arg(fi.fileName()));
        startIndexing(m_dirs);
        return;
    }
    m_dirs.append(abs);
    emit videoDirsChanged();
    startIndexing(m_dirs);
}

void Studio::removeFolder(const QString &dir) {
    const QString abs = QFileInfo(dir).absoluteFilePath();
    if (!m_dirs.removeAll(abs) && !m_dirs.removeAll(dir)) return;
    emit videoDirsChanged();
    if (m_dirs.isEmpty()) {
        // No footage at all: drop the index instead of scanning nothing.
        m_lib->setDirs(QStringList());
        m_lib->reset();
        m_pendingRegenerate = false;
        m_timeline->regenerate(QList<int>());   // nothing left to cut
        setStatus(QStringLiteral("Add a footage folder to begin"));
        emit libraryChanged();
        emit filtersChanged();
        persist();
        return;
    }
    startIndexing(m_dirs);
}

void Studio::openFolderDialog() { emit folderDialogRequested(); }

void Studio::openSongDialog() { emit songDialogRequested(); }

void Studio::reloadLibraryModel() {
    m_clips->setLibrary(m_lib);
    m_clips->refresh();
}

void Studio::syncScheduler() {
    m_sched->setParams(m_params);
    m_sched->setAnalysis(m_analysis);
    m_sched->reseed();
    // The pool is the filtered set: the walk must never pick an excluded clip.
    // Walk order (chronology, or a seeded shuffle) belongs to the scheduler.
    m_sched->setPool(m_lib ? m_lib->poolFor(m_filters) : QList<Clip>());
}

// ---- song ---------------------------------------------------------------

QString Studio::songName() const {
    return m_song.isEmpty() ? QString() : QFileInfo(m_song).completeBaseName();
}

QString Studio::songSummary() const {
    if (!m_analysis.valid()) return {};
    const QString mood = m_analysis.mood;
    const int bpm = int(std::lround(m_analysis.bpm));
    int drops = 0;
    for (const auto &s : m_analysis.sections)
        if (s.kind == QLatin1String("drop")) drops++;
    QString line = QStringLiteral("%1 bpm").arg(bpm);
    if (!m_analysis.keyName.isEmpty()) line += QStringLiteral(" · %1").arg(m_analysis.keyName);
    line += QStringLiteral(" · %1").arg(mood);
    if (drops > 0) line += QStringLiteral(" · %1 drop%2").arg(drops).arg(drops == 1 ? QString() : QStringLiteral("s"));
    if (!m_analysis.patAt.isEmpty()) line += QStringLiteral(" · has its own stutter");
    return line;
}

void Studio::loadSong(const QUrl &url) {
    const QString path = localPathFor(url);
    if (path.isEmpty()) {
        setStatus(QStringLiteral("That location has no path this app can read: %1").arg(url.toString()));
        return;
    }
    loadSongFile(path);
}

void Studio::loadSongFile(const QString &path) {
    if (path.isEmpty()) return;
    if (!QFileInfo::exists(path)) {
        setStatus(QStringLiteral("Could not open %1").arg(path));
        return;
    }
    m_song = path;
    m_analysis.clear();
    const int gen = ++m_songGen;
    emit songChanged();
    emit analysisChanged();
    setSongBusy(true);
    // No status message for the pass itself: the progress strip is the single
    // place that says what is happening. This slot is for outcomes.

    // Decoding, beat detection and the duration probe all shell out; none of it
    // may run on the UI thread or the window freezes for the length of the song.
    QThread *t = QThread::create([this, path, gen]() {
        QList<float> pcm;
        QString err;
        const bool ok = Analyzer::decodeAudio(path, pcm, &err);
        Analysis a = ok ? Analyzer::analyze(pcm, Analyzer::SampleRate) : Analysis();
        double dur = 0;
        if (ok) {
            QProcess probe;
            probe.start(tools::path(QStringLiteral("ffprobe")),
                        {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-show_entries"),
                         QStringLiteral("format=duration"), QStringLiteral("-of"), QStringLiteral("csv=p=0"), path});
            if (probe.waitForFinished(10000))
                dur = QString::fromLocal8Bit(probe.readAllStandardOutput()).trimmed().toDouble();
        }
        QMetaObject::invokeMethod(this, [this, a, ok, err, path, dur, gen]() {
            if (gen != m_songGen) return;   // a newer song already won
            setSongBusy(false);
            if (!ok) {
                setStatus(QStringLiteral("Could not read %1").arg(err));
                return;
            }
            m_analysis = a;
            emit analysisChanged();
            m_show->setSong(path);
            m_timeline->setSongDuration(dur);
            syncScheduler();
            setStatus(songSummary());
            emit editChanged();
            persist();
            generate();
        }, Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

// ---- params / presets ---------------------------------------------------

QString Studio::lookSummary() const {
    // Plain prose, no numbers unless they are meaningful to a human.
    QString pace;
    if (m_params.aggro <= 0.15) pace = QStringLiteral("long, unhurried shots");
    else if (m_params.aggro < 0.6) pace = QStringLiteral("steady shots every few beats");
    else if (m_params.aggro < 1.15) pace = QStringLiteral("cuts that follow the beat");
    else if (m_params.aggro < 1.75) pace = QStringLiteral("tight, driving cuts");
    else pace = QStringLiteral("cuts on nearly every beat");
    QString fade = m_params.fadeMs <= 0 ? QStringLiteral("hard cuts")
                 : m_params.fadeMs <= 120 ? QStringLiteral("quick dissolves")
                                          : QStringLiteral("soft dissolves");
    return QStringLiteral("%1, %2").arg(pace, fade);
}

void Studio::applyPreset(const QString &name) {
    for (const Preset &p : builtinPresets()) {
        if (p.name != name) continue;
        Params base;   // presets are sparse: they layer on the defaults
        base.applyMap(p.overrides);
        m_params = base;
        m_activePreset = name;
        m_activeProfile.clear();
        syncScheduler();
        emit activePresetChanged();
        emit paramsChanged();
        persist();
        setStatus(QStringLiteral("Look: %1").arg(p.blurb));
        return;
    }
    // A saved profile behaves like a preset in the same rail.
    if (m_profiles.contains(name)) loadProfile(name);
}

void Studio::setParam(const QString &key, const QVariant &value) {
    QVariantMap m = m_params.toMap();
    m[key] = value;
    m_params.applyMap(m);
    // Any hand edit detaches from the named look.
    if (!m_activePreset.isEmpty() || !m_activeProfile.isEmpty()) {
        m_activePreset.clear();
        m_activeProfile.clear();
        emit activePresetChanged();
        emit looksChanged();
    }
    syncScheduler();
    emit paramsChanged();
    persist();
}

void Studio::restoreDefaults() {
    m_params = Params();
    m_activePreset = QStringLiteral("Anything");
    m_activeProfile.clear();
    syncScheduler();
    emit activePresetChanged();
    emit paramsChanged();
    persist();
}

// ---- filters ------------------------------------------------------------

QVariantMap Studio::filters() const { return filtersToMap(m_filters); }
QStringList Studio::days() const { return m_lib ? m_lib->days() : QStringList(); }
int Studio::poolCount() const { return m_lib ? m_lib->poolFor(m_filters).size() : 0; }
int Studio::libraryCount() const { return m_lib ? m_lib->clips().size() : 0; }
bool Studio::hasEdit() const { return m_timeline->hasTimeline(); }

void Studio::applyFilters(const QVariantMap &m) {
    m_filters = filtersFromMap(m);
}

void Studio::setFilter(const QString &key, const QVariant &value) {
    const double d = value.toDouble();
    if (key == QLatin1String("junk")) m_filters.junk = value.toBool();
    else if (key == QLatin1String("skipWindowsOn")) m_filters.skipWindowsOn = value.toBool();
    else if (key == QLatin1String("skipWindows")) m_filters.skipWindows = value.toString();
    else if (key == QLatin1String("dayOn")) {
        m_filters.dayOn = value.toBool();
        // Picking "only one day" with nothing chosen would filter everything out.
        if (m_filters.dayOn && m_filters.day.isEmpty()) {
            const QStringList all = days();
            if (!all.isEmpty()) m_filters.day = all.first();
        }
    } else if (key == QLatin1String("day")) m_filters.day = value.toString();
    else if (key == QLatin1String("scoreOn")) {
        m_filters.scoreOn = value.toBool();
        // Top-% means the score at the rank boundary, computed over the library.
        if (m_filters.scoreOn && m_lib) m_filters.scoreMin = m_lib->scoreCutoff(m_filters.scorePct);
    } else if (key == QLatin1String("scorePct")) {
        m_filters.scorePct = qBound(5, int(std::lround(d)), 100);
        if (m_lib) m_filters.scoreMin = m_lib->scoreCutoff(m_filters.scorePct);
    } else if (key == QLatin1String("mvOn")) {
        m_filters.mvOn = value.toBool();
        if (m_filters.mvOn && m_lib) m_filters.mvMin = m_lib->mvCutoff(m_filters.mvPct);
    } else if (key == QLatin1String("mvPct")) {
        m_filters.mvPct = qBound(5, int(std::lround(d)), 100);
        if (m_lib) m_filters.mvMin = m_lib->mvCutoff(m_filters.mvPct);
    } else if (key == QLatin1String("arOn")) {
        m_filters.arOn = value.toBool();
        if (m_filters.arOn && m_filters.ar == 0) m_filters.ar = 16.0 / 9.0;
    } else if (key == QLatin1String("ar")) m_filters.ar = d;
    else if (key == QLatin1String("todOn")) m_filters.todOn = value.toBool();
    else if (key == QLatin1String("todFrom") || key == QLatin1String("todTo")) {
        // One daylight window is what a human needs; preset windows live in Advanced.
        double from = 8, to = 19;
        if (!m_filters.tod.isEmpty()) { from = m_filters.tod.first().a; to = m_filters.tod.first().b; }
        if (key == QLatin1String("todFrom")) from = d; else to = d;
        m_filters.tod = {todRange(from, to)};
    }
    m_clips->setFilters(m_filters);
    syncScheduler();
    emit filtersChanged();
    persist();
}

// ---- edit ---------------------------------------------------------------

void Studio::generate() {
    if (m_song.isEmpty() || !m_analysis.valid()) {
        setStatus(m_song.isEmpty() ? QStringLiteral("Choose a song first") : QStringLiteral("Still listening…"));
        return;
    }
    // Building the edit needs clip ids and durations, not scores: it can run while
    // the library is still being scored. Only a scan that has not found anything
    // yet holds it back. Waiting for scoring over a network share looked exactly
    // like the app doing nothing.
    if (!m_lib || m_lib->clips().isEmpty()) {
        m_pendingRegenerate = busy();
        setStatus(m_pendingRegenerate ? QStringLiteral("Scanning footage…") : QStringLiteral("Add a footage folder first"));
        return;
    }

    syncScheduler();
    m_clips->refresh();
    const QList<int> pool = m_clips->passingIds();
    if (pool.isEmpty()) {
        setStatus(QStringLiteral("No clips pass these filters"));
        return;
    }
    m_sched->reseed();
    m_timeline->regenerate(pool);
    if (!m_timeline->hasTimeline()) {
        setStatus(QStringLiteral("Nothing to cut to"));
        return;
    }
    m_editUnscored = m_lib && m_lib->scoreDone() < m_lib->scoreTotal();
    m_show->setEdit(m_timeline->segments(), m_timeline->cuts(), m_timeline->fades());
    emit editChanged();
    setStatus(QStringLiteral("%1 cuts from %2 clips").arg(m_timeline->rowCount()).arg(pool.size()));
}

void Studio::playPause() {
    if (!m_timeline->hasTimeline()) generate();
    m_show->togglePlay();
}

void Studio::stopShow() { m_show->stop(); }
void Studio::seek(double t) { m_show->seek(t); }
void Studio::cueSegment(int index) { m_show->cueSegment(index); }

void Studio::newInPointAt(int clipId, double songTime) {
    if (clipId < 0 || !m_lib) return;
    // Clicking a clip is "put this shot here". With no edit yet there is nowhere
    // to put it, so cut one first and drop the clip into that — otherwise the
    // click is silently swallowed.
    if (!m_timeline->hasTimeline()) {
        generate();
        if (!m_timeline->hasTimeline()) return;
    }
    const Clip *c = m_lib->clipById(clipId);
    if (!c) return;
    // Drop the clip in at the musical position the user picked: the covering
    // segment keeps its length, the new clip starts at a fresh in-point.
    const QList<TlSeg> &segs = m_timeline->segments();
    int idx = 0;
    double at = 0;
    for (int i = 0; i < segs.size(); i++) {
        if (at + segs[i].len > songTime) { idx = i; break; }
        at += segs[i].len;
        idx = i;
    }
    const double len = segs[idx].len;
    const double in = m_sched->dur2seek(*c, len, m_sched->rnd());
    m_timeline->replaceSegment(idx, c->id, in);
    setStatus(QStringLiteral("Clip dropped at %1").arg(formatTime(songTime)));
}

QString Studio::formatTime(double t) const {
    if (!std::isfinite(t) || t < 0) t = 0;
    const int total = int(t);
    return QStringLiteral("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QLatin1Char('0'));
}
