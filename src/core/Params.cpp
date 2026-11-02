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
    m["darkMean"] = darkMean;
    m["darkBrightPct"] = darkBrightPct;
    m["darkRetries"] = darkRetries;
    m["useLrf"] = useLrf;
    m["precache"] = precache;
    m["mood"] = mood;
    m["fancy"] = fancy;
    m["shuffle"] = shuffle;
    m["skipJunk"] = skipJunk;
    m["replicate"] = replicate;
    m["aggrThreshold"] = aggrThreshold;
    m["aggrHalfRun"] = aggrHalfRun;
    m["aggrQuarterRun"] = aggrQuarterRun;
    m["aggrFloor"] = aggrFloor;
    m["insMax"] = insMax;
    m["reactive"] = reactive;
    m["reactiveMs"] = reactiveMs;
    m["reactiveCool"] = reactiveCool;
    m["gridSec"] = gridSec;
    m["seed"] = seed;
    m["skipWindows"] = skipWindows;
    return m;
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
    num("darkMean", darkMean);
    num("darkBrightPct", darkBrightPct);
    num("darkRetries", darkRetries);
    flag("useLrf", useLrf);
    flag("precache", precache);
    flag("mood", mood);
    flag("fancy", fancy);
    flag("shuffle", shuffle);
    flag("skipJunk", skipJunk);
    flag("replicate", replicate);
    num("aggrThreshold", aggrThreshold);
    num("aggrHalfRun", aggrHalfRun);
    num("aggrQuarterRun", aggrQuarterRun);
    num("aggrFloor", aggrFloor);
    num("insMax", insMax);
    flag("reactive", reactive);
    num("reactiveMs", reactiveMs);
    num("reactiveCool", reactiveCool);
    num("gridSec", gridSec);
    num("seed", seed);
    auto it = m.constFind("skipWindows");
    if (it != m.constEnd()) skipWindows = it->toString();
}

const QList<ParamField> &paramSchema() {
    static const QList<ParamField> s = {
        {"holdMax", QStringLiteral("Longest hold"), QStringLiteral("How many beats a single clip may stay on screen before it cuts."), ParamField::Number, 1, 6, 1},
        {"aggro", QStringLiteral("Aggressivity"), QStringLiteral("0 sits on shots for ages, 2 cuts on nearly every beat."), ParamField::Number, 0, 2, 0.25},
        {"energyHi", QStringLiteral("Cut on every beat when energy reaches"), QStringLiteral("Below this the clip is held for several beats."), ParamField::Number, 0.5, 0.95, 0.05},
        {"energyLo", QStringLiteral("Longest hold when energy drops to"), QStringLiteral("The quietest passages hold the longest."), ParamField::Number, 0.05, 0.45, 0.05},
        {"split", QStringLiteral("Stutter split"), QStringLiteral("How many pieces a stuttered drop is chopped into."), ParamField::Number, 2, 5, 1},
        {"stutter", QStringLiteral("Drop stutters"), QStringLiteral("Chop the drop into a quick burst of shots."), ParamField::Check},
        {"stutterMaxBpm", QStringLiteral("Stutters only below"), QStringLiteral("Fast songs already feel busy; stutters are skipped above this tempo."), ParamField::Number, 60, 180, 5, QStringLiteral("bpm")},
        {"stutterMax", QStringLiteral("Stutter cap per song"), {}, ParamField::Number, 0, 30, 1},
        {"stutterGap", QStringLiteral("Minimum gap between stutters"), {}, ParamField::Number, 0, 10, 0.5, QStringLiteral("s")},
        {"stutterMinHold", QStringLiteral("Shortest segment worth splitting"), {}, ParamField::Number, 0.3, 2, 0.1, QStringLiteral("s")},
        {"stutterRamp", QStringLiteral("Drop ramp threshold"), {}, ParamField::Number, 0.05, 0.9, 0.05},
        {"stutterAccent", QStringLiteral("Accent energy threshold"), {}, ParamField::Number, 0.5, 1, 0.05},
        {"fadeMs", QStringLiteral("Crossfade"), QStringLiteral("Soft dissolve between clips. 0 is a hard cut."), ParamField::Number, 0, 500, 10, QStringLiteral("ms")},
        {"darkMean", QStringLiteral("Dark chunk brightness under"), {}, ParamField::Number, 0, 40, 1},
        {"darkBrightPct", QStringLiteral("Dark chunk: most bright pixels under"), {}, ParamField::Number, 0, 10, 0.5, QStringLiteral("%")},
        {"darkRetries", QStringLiteral("Dark chunk re-rolls"), QStringLiteral("How many times to look for a better in-point before giving up."), ParamField::Number, 0, 8, 1},
        {"useLrf", QStringLiteral("Play low-res proxies"), QStringLiteral("DJI cameras write a light .LRF twin next to each clip; it cuts faster."), ParamField::Check},
        {"precache", QStringLiteral("Precache clips in memory"), {}, ParamField::Check},
        {"skipJunk", QStringLiteral("Skip mostly-dark clips"), QStringLiteral("Drops tunnels and lens-cap footage out of the pool entirely."), ParamField::Check},
        {"mood", QStringLiteral("Follow the music"), QStringLiteral("Holds and crossfades rise and fall with the song's sections."), ParamField::Check},
        {"fancy", QStringLiteral("Indie transitions"), {}, ParamField::Check},
        {"shuffle", QStringLiteral("Shuffle clip order"), QStringLiteral("Breaks the day/chronology walk. A fixed seed keeps the shuffle repeatable."), ParamField::Check},
        {"replicate", QStringLiteral("Mimic the song's own stutters"), QStringLiteral("Cuts land on off-beat hits the track already has."), ParamField::Check},
        {"aggrThreshold", QStringLiteral("Aggressive section energy"), {}, ParamField::Number, 0.6, 1.05, 0.05},
        {"aggrHalfRun", QStringLiteral("Half-beat cuts after N strong beats"), {}, ParamField::Number, 2, 8, 1},
        {"aggrQuarterRun", QStringLiteral("Quarter-beat cuts after N strong beats"), {}, ParamField::Number, 4, 16, 1},
        {"aggrFloor", QStringLiteral("Aggressive section floor"), {}, ParamField::Number, 0, 1, 0.05},
        {"insMax", QStringLiteral("Inserted micro-cut cap"), {}, ParamField::Number, 12, 2000, 4},
        {"reactive", QStringLiteral("Cut away from black frames"), {}, ParamField::Check},
        {"reactiveMs", QStringLiteral("Black time before an early cut"), {}, ParamField::Number, 100, 2000, 100, QStringLiteral("ms")},
        {"reactiveCool", QStringLiteral("Minimum gap between early cuts"), {}, ParamField::Number, 0, 5, 0.5, QStringLiteral("s")},
        {"gridSec", QStringLiteral("Fallback cut interval"), QStringLiteral("Used when a song has no detectable beat."), ParamField::Number, 0.2, 2, 0.1, QStringLiteral("s")},
        {"seed", QStringLiteral("Edit seed"), QStringLiteral("0 reshuffles every run; any other number reproduces the exact same edit."), ParamField::Number, 0, 99999, 1},
        {"skipWindows", QStringLiteral("Skip date windows"), QStringLiteral("Comma separated, e.g. 2026-08-14±1, 2026-09-01."), ParamField::Text},
    };
    return s;
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
         {{"aggro", 0}, {"holdMax", 6}, {"energyHi", 0.85}, {"energyLo", 0.15}, {"stutter", false}, {"gridSec", 1}, {"fadeMs", 200}}},
        {QStringLiteral("Ambient drift"),
         QStringLiteral("Barely cutting at all; the picture just breathes."),
         QStringLiteral("Six-beat holds, 300 ms dissolves, two-piece splits only if it ever stutters."),
         {{"aggro", 0}, {"holdMax", 6}, {"energyHi", 0.95}, {"energyLo", 0.05}, {"stutter", false}, {"split", 2}, {"gridSec", 2}, {"fadeMs", 300}}},
        {QStringLiteral("Cinematic montage"),
         QStringLiteral("Trailer pacing — steady, confident, hard on the hits."),
         QStringLiteral("Four-beat holds, 150 ms fades, a handful of stutters at the peak."),
         {{"aggro", 0.5}, {"holdMax", 4}, {"energyHi", 0.75}, {"energyLo", 0.25}, {"stutterMax", 6}, {"fadeMs", 150}, {"gridSec", 0.5}}},
        {QStringLiteral("Club driver"),
         QStringLiteral("Tight, driving, cut on the groove."),
         QStringLiteral("Two-beat holds at most, 80 ms fades, half-beat cuts through loud runs."),
         {{"aggro", 1.5}, {"holdMax", 2}, {"energyHi", 0.45}, {"energyLo", 0.35}, {"aggrFloor", 0.4}, {"aggrThreshold", 0.5}, {"insMax", 150}, {"stutterMaxBpm", 220}, {"fadeMs", 80}, {"gridSec", 0.5}}},
        {QStringLiteral("Techno 185"),
         QStringLiteral("Relentless. Every beat, every quarter."),
         QStringLiteral("One-beat holds, hard cuts, quarter-beat machine-gunning once a run builds."),
         {{"aggro", 2}, {"holdMax", 1}, {"energyHi", 0.35}, {"energyLo", 0.3}, {"aggrFloor", 0.3}, {"aggrThreshold", 0.35}, {"aggrHalfRun", 2}, {"aggrQuarterRun", 4}, {"insMax", 400}, {"stutterMaxBpm", 300}, {"split", 4}, {"stutterRamp", 0.25}, {"stutterAccent", 0.7}, {"fadeMs", 0}, {"gridSec", 0.3}}},
        {QStringLiteral("Bull on cocaine"),
         QStringLiteral("Chaos. Flash frames, no breathing room."),
         QStringLiteral("Five-piece splits, no crossfades, up to 2000 inserted micro-cuts."),
         {{"aggro", 2}, {"holdMax", 1}, {"energyHi", 0.2}, {"energyLo", 0.05}, {"aggrFloor", 0.12}, {"aggrThreshold", 0.1}, {"aggrHalfRun", 2}, {"aggrQuarterRun", 4}, {"split", 5}, {"stutterMaxBpm", 300}, {"stutterMax", 99}, {"stutterGap", 0}, {"stutterMinHold", 0.2}, {"stutterRamp", 0.05}, {"stutterAccent", 0.5}, {"insMax", 2000}, {"fadeMs", 0}, {"gridSec", 0.2}, {"darkRetries", 8}, {"reactiveMs", 300}}},
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
