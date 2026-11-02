#pragma once
#include <QString>
#include <QVariantMap>
#include <QList>

// Every generation knob, with the defaults they were tuned at. The UI never
// exposes these directly: preset cards and plain-language controls write them,
// and an Advanced disclosure edits them one by one.
struct Params {
    int holdMax = 3;
    double energyHi = 0.67;
    double energyLo = 0.33;
    double aggro = 1;
    int split = 3;
    bool stutter = true;
    int stutterMaxBpm = 140;
    int stutterMax = 12;
    double stutterGap = 3.5;
    double stutterMinHold = 0.9;
    double stutterRamp = 0.4;
    double stutterAccent = 0.85;
    int fadeMs = 120;
    int darkMean = 3;
    double darkBrightPct = 1;
    int darkRetries = 4;
    bool useLrf = true;
    bool precache = true;
    bool mood = true;
    bool fancy = true;
    bool shuffle = false;
    bool skipJunk = true;
    bool replicate = true;
    double aggrThreshold = 0.85;
    int aggrHalfRun = 3;
    int aggrQuarterRun = 6;
    double aggrFloor = 0;     // presets raise/lower this; 0 means "unset" in buildCuts
    int insMax = 48;          // presets override this
    bool reactive = true;
    int reactiveMs = 250;
    double reactiveCool = 1.5;
    double gridSec = 0.5;
    int seed = 0;
    QString skipWindows = QStringLiteral("2026-08-14±1");

    QVariantMap toMap() const;
    void applyMap(const QVariantMap &m);
    static Params fromMap(const QVariantMap &m) { Params p; p.applyMap(m); return p; }
};

// One row of the Advanced disclosure.
struct ParamField {
    QString key;
    QString label;      // plain language, no jargon
    QString hint;       // one sentence of what it changes
    enum Kind { Number, Check, Text } kind = Number;
    double min = 0, max = 1, step = 1;
    QString unit;
};

extern const QList<ParamField> &paramSchema();

// A named look. `overrides` only carries the fields that differ from defaults,
// as the built-in PRESETS table does.
struct Preset {
    QString name;
    QString blurb;      // one line of prose for the card
    QString detail;     // how it cuts, in human terms
    QVariantMap overrides;
};

extern const QList<Preset> &builtinPresets();

// ---- filters ----
// "2026-08-14±1" skips that day and ±1 around it; clips without a stamp never match.
struct SkipWindow { qint64 t; qint64 r; };
QList<SkipWindow> parseWindows(const QString &spec);
bool inSkipWindows(const QString &day, const QList<SkipWindow> &wins);

// Time-of-day window (hour floats); a >= b wraps past midnight.
struct TodRange { double a, b; bool wrap; };
TodRange todRange(double a, double b);

// Everything the pool filter needs besides the clip list itself.
struct Filters {
    bool junk = true;             // skip mostly-dark clips
    QString skipWindows;          // date windows
    bool dayOn = false;
    QString day;
    bool scoreOn = false;
    int scoreMin = 0;
    bool mvOn = false;
    int mvMin = 0;
    bool arOn = false;
    double ar = 0;
    bool todOn = false;
    QList<TodRange> tod;
};
