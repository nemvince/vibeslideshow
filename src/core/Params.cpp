#include "core/Params.h"
#include <QDateTime>
#include <QTimeZone>
#include <cstdlib>

QVariantMap Params::toMap() const {
    QVariantMap m;
    m["holdMax"] = holdMax;
    m["energyHi"] = energyHi;
    m["energyLo"] = energyLo;
    m["aggro"] = aggro;
    m["split"] = split;
    m["stutter"] = stutter;
    m["stutterMaxBpm"] = stutterMaxBpm;
    m["stutterMax"] = stutterMax;
    m["stutterGap"] = stutterGap;
    m["stutterMinHold"] = stutterMinHold;
    m["stutterRamp"] = stutterRamp;
    m["stutterAccent"] = stutterAccent;
    m["fadeMs"] = fadeMs;
    m["useLrf"] = useLrf;
    m["mood"] = mood;
    m["shuffle"] = shuffle;
    m["skipJunk"] = skipJunk;
    m["replicate"] = replicate;
    m["aggrThreshold"] = aggrThreshold;
    m["aggrHalfRun"] = aggrHalfRun;
    m["aggrQuarterRun"] = aggrQuarterRun;
    m["aggrFloor"] = aggrFloor;
    m["insMax"] = insMax;
    m["gridSec"] = gridSec;
    // Shortest shot was applied from a map but never written to one: it did not
    // survive a restart and no saved look ever carried it.
    m["minShotSec"] = minShotSec;
    m["seed"] = seed;
    // The picture.
    m["moveOn"] = moveOn;
    m["moveAmt"] = moveAmt;
    m["cutStep"] = cutStep;
    m["cutMax"] = cutMax;
    // The editor.
    m["exportQuality"] = exportQuality;
    m["exportW"] = exportW;
    m["exportH"] = exportH;
    m["exportFps"] = exportFps;
    m["restoreProject"] = restoreProject;
    m["undoDepth"] = undoDepth;
    return m;
}

QVariantMap Params::lookMap() const {
    // A look is the cut and the picture. Where the film is exported to, how much
    // undo the editor keeps and whether the last project reopens are not part of
    // what a look looks like.
    QVariantMap m = toMap();
    for (const char *k : {"exportQuality", "exportW", "exportH", "exportFps", "restoreProject",
                          "undoDepth"})
        m.remove(QLatin1String(k));
    return m;
}

void Params::keepPictureFrom(const Params &o) {
    moveOn = o.moveOn;
    moveAmt = o.moveAmt;
    cutStep = o.cutStep;
    cutMax = o.cutMax;
}

void Params::keepEditorFrom(const Params &o) {
    exportQuality = o.exportQuality;
    exportW = o.exportW;
    exportH = o.exportH;
    exportFps = o.exportFps;
    restoreProject = o.restoreProject;
    undoDepth = o.undoDepth;
}

void Params::applyMap(const QVariantMap &m) {
    auto num = [&](const char *k, auto &dst) {
        auto it = m.constFind(k);
        if (it != m.constEnd()) dst = it->toDouble();
    };
    auto flag = [&](const char *k, bool &dst) {
        auto it = m.constFind(k);
        if (it != m.constEnd()) dst = it->toBool();
    };
    num("holdMax", holdMax);
    num("energyHi", energyHi);
    num("energyLo", energyLo);
    num("aggro", aggro);
    num("split", split);
    flag("stutter", stutter);
    num("stutterMaxBpm", stutterMaxBpm);
    num("stutterMax", stutterMax);
    num("stutterGap", stutterGap);
    num("stutterMinHold", stutterMinHold);
    num("stutterRamp", stutterRamp);
    num("stutterAccent", stutterAccent);
    num("fadeMs", fadeMs);
    flag("useLrf", useLrf);
    flag("mood", mood);
    flag("shuffle", shuffle);
    flag("skipJunk", skipJunk);
    flag("replicate", replicate);
    num("aggrThreshold", aggrThreshold);
    num("aggrHalfRun", aggrHalfRun);
    num("aggrQuarterRun", aggrQuarterRun);
    num("aggrFloor", aggrFloor);
    num("insMax", insMax);
    num("gridSec", gridSec);
    num("minShotSec", minShotSec);
    num("seed", seed);
    flag("moveOn", moveOn);
    num("moveAmt", moveAmt);
    num("cutStep", cutStep);
    num("cutMax", cutMax);
    num("exportQuality", exportQuality);
    num("exportW", exportW);
    num("exportH", exportH);
    num("exportFps", exportFps);
    flag("restoreProject", restoreProject);
    num("undoDepth", undoDepth);
}

const QList<Preset> &builtinPresets() {
    static const QList<Preset> p = {
        {QStringLiteral("Anything"),
         QStringLiteral("Follows the song wherever it goes."),
         QStringLiteral("Balanced holds — long on quiet passages, one clip per beat once it lifts."),
         {}},
        {QStringLiteral("Slow melancholic"),
         QStringLiteral("Long, quiet shots with soft dissolves."),
         QStringLiteral("Up to six beats per clip, no stutters, 200 ms fades."),
         {{"aggro", 0}, {"holdMax", 6}, {"energyHi", 0.85}, {"energyLo", 0.15}, {"stutter", false}, {"gridSec", 1}, {"fadeMs", 200}, {"minShotSec", 2.4}}},
        {QStringLiteral("Ambient drift"),
         QStringLiteral("Barely cutting at all; the picture just breathes."),
         QStringLiteral("Six-beat holds, 300 ms dissolves, two-piece splits only if it ever stutters."),
         {{"aggro", 0}, {"holdMax", 6}, {"energyHi", 0.95}, {"energyLo", 0.05}, {"stutter", false}, {"split", 2}, {"gridSec", 2}, {"fadeMs", 300}, {"minShotSec", 3.5}}},
        {QStringLiteral("Cinematic montage"),
         QStringLiteral("Trailer pacing — steady, confident, hard on the hits."),
         QStringLiteral("Four-beat holds, 150 ms fades, a handful of stutters at the peak."),
         {{"aggro", 0.5}, {"holdMax", 4}, {"energyHi", 0.75}, {"energyLo", 0.25}, {"stutterMax", 6}, {"fadeMs", 150}, {"gridSec", 0.5}, {"minShotSec", 1.2}}},
        {QStringLiteral("Club driver"),
         QStringLiteral("Tight, driving, cut on the groove."),
         QStringLiteral("Two-beat holds at most, 80 ms fades, half-beat cuts through loud runs."),
         {{"aggro", 1.5}, {"holdMax", 2}, {"energyHi", 0.45}, {"energyLo", 0.35}, {"aggrFloor", 0.4}, {"aggrThreshold", 0.5}, {"insMax", 150}, {"fadeMs", 80}, {"gridSec", 0.5}, {"minShotSec", 0.7}}},
        {QStringLiteral("Techno 185"),
         QStringLiteral("Relentless. Every beat, every quarter."),
         QStringLiteral("One-beat holds, hard cuts, quarter-beat machine-gunning once a run builds."),
         {{"aggro", 2}, {"holdMax", 1}, {"energyHi", 0.35}, {"energyLo", 0.3}, {"aggrFloor", 0.3}, {"aggrThreshold", 0.35}, {"aggrHalfRun", 2}, {"aggrQuarterRun", 4}, {"insMax", 400}, {"split", 4}, {"stutterRamp", 0.25}, {"stutterAccent", 0.7}, {"fadeMs", 0}, {"gridSec", 0.3}, {"minShotSec", 0.4}}},
        {QStringLiteral("Bull on cocaine"),
         QStringLiteral("Chaos. Flash frames, no breathing room."),
         QStringLiteral("Five-piece splits, no crossfades, up to 2000 inserted micro-cuts."),
         {{"aggro", 2}, {"holdMax", 1}, {"energyHi", 0.2}, {"energyLo", 0.05}, {"aggrFloor", 0.12}, {"aggrThreshold", 0.1}, {"aggrHalfRun", 2}, {"aggrQuarterRun", 4}, {"split", 5}, {"stutterMax", 99}, {"stutterGap", 0}, {"stutterMinHold", 0.2}, {"stutterRamp", 0.05}, {"stutterAccent", 0.5}, {"insMax", 2000}, {"fadeMs", 0}, {"gridSec", 0.2}}},
    };
    return p;
}

QList<SkipWindow> parseWindows(const QString &spec) {
    QList<SkipWindow> out;
    const auto parts = spec.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString w : parts) {
        w = w.trimmed();
        if (w.isEmpty()) continue;
        // '±' here is U+00B1; the spec allows a plain +/- spelling too.
        int sep = w.indexOf(QChar(0x00B1));
        QString base = w, radTxt;
        int radius = 0;
        if (sep >= 0) {
            base = w.left(sep).trimmed();
            radTxt = w.mid(sep + 1).trimmed();
            bool ok = false;
            radius = radTxt.toInt(&ok);
            if (!ok) radius = 0;
        }
        if (base.size() != 10 || base[4] != QLatin1Char('-') || base[7] != QLatin1Char('-')) continue;
        const QDate d = QDate::fromString(base, QStringLiteral("yyyy-MM-dd"));
        if (!d.isValid()) continue;
        const qint64 t = QDateTime(d, QTime(0, 0), QTimeZone::utc()).toMSecsSinceEpoch();
        out.append({t, qint64(86400000LL) * radius});
    }
    return out;
}

bool inSkipWindows(const QString &day, const QList<SkipWindow> &wins) {
    if (day.isEmpty() || wins.isEmpty()) return false;
    const QDate d = QDate::fromString(day, QStringLiteral("yyyy-MM-dd"));
    if (!d.isValid()) return false;
    const qint64 t = QDateTime(d, QTime(0, 0), QTimeZone::utc()).toMSecsSinceEpoch();
    for (const auto &w : wins)
        if (std::llabs(t - w.t) <= w.r) return true;
    return false;
}

TodRange todRange(double a, double b) { return {a, b, a >= b}; }
