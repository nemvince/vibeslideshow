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
    bool useLrf = true;
    bool mood = true;
    bool shuffle = false;
    bool skipJunk = true;
    bool replicate = true;
    double aggrThreshold = 0.85;
    int aggrHalfRun = 3;
    int aggrQuarterRun = 6;
    double aggrFloor = 0;     // presets raise/lower this; 0 means "unset" in buildCuts
    int insMax = 48;          // presets override this
    double gridSec = 0.5;
    // Shortest shot, in seconds. Calm passages breathe with it even when the
    // energy curve says "cut on every beat"; loud sections scale it down.
    double minShotSec = 1.2;
    int seed = 0;

    // ---- the picture ----------------------------------------------------
    // These change what the film looks like, so a saved look carries them and a
    // preset switch leaves them alone.
    bool moveOn = true;      // move every shot (Ken Burns)
    double moveAmt = 1.0;    // 0..1, how far a shot is pushed or pulled
    int cutStep = 40;        // ms `[` and `]` move a single cut by
    int cutMax = 500;        // longest one cut may dissolve for, ms

    // ---- the editor -----------------------------------------------------
    // Belong to the app, not to a look: carried in params.json and in a project,
    // never in a saved look.
    int exportQuality = 1;   // 0 draft, 1 balanced, 2 high
    int exportW = 1920;
    int exportH = 1080;
    int exportFps = 30;
    bool restoreProject = true;   // reopen the last project on launch
    int undoDepth = 100;          // how many edit steps the history keeps

    QVariantMap toMap() const;
    // Everything a saved look owns: the generation knobs and the picture settings.
    QVariantMap lookMap() const;
    void applyMap(const QVariantMap &m);
    // A preset swap rebuilds the generation knobs from defaults; these carry over
    // what it must not disturb. A "Defaults" reset also carries the editor group,
    // but not the picture — that is part of the look.
    void keepPictureFrom(const Params &o);
    void keepEditorFrom(const Params &o);
    static Params fromMap(const QVariantMap &m) { Params p; p.applyMap(m); return p; }
};

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
    bool skipWindowsOn = false;   // apply the date windows below
    QString skipWindows;          // date windows
    bool dayOn = false;
    QString day;
    bool scoreOn = false;
    int scoreMin = 0;      // derived: the score at the chosen percentage
    int scorePct = 50;     // what the user picked, so the control can show it back
    bool mvOn = false;
    int mvMin = 0;
    int mvPct = 50;
    bool arOn = false;
    double ar = 0;
    bool todOn = false;
    QList<TodRange> tod;
};
