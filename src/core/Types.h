#pragma once
#include <QString>
#include <QList>

// One indexed video file. DJI proxies (.LRF) are paired by stem, capture
// stamps come from the filename.
struct Clip {
    int id = 0;
    QString path;
    QString ext;
    double duration = 0;
    QString day;          // YYYY-MM-DD from a 14-digit stamp, empty when absent
    double tod = -1;      // hour-of-day as a float, -1 when absent
    bool hasTod = false;
    qint64 size = 0;
    int w = 0, h = 0;
    QString lrf;          // proxy twin path, empty when none
    double lrfDur = 0;
    qint64 lrfSize = 0;

    int score = 0;
    int mv = 0;
    double dk = 0;
    bool scored = false;

    double ar() const { return (w > 0 && h > 0) ? (w / double(h)) : 0.0; }
    // Playback source: proxy when it exists and is allowed.
    QString source(bool useLrf) const { return (useLrf && !lrf.isEmpty()) ? lrf : path; }
    double playDuration(bool useLrf) const { return (useLrf && !lrf.isEmpty()) ? lrfDur : duration; }
};

struct Section {
    double s = 0;
    double e = 0;
    double b = 0;
    QString kind;   // chill | mid | loud | build | drop
    int bi = 0;
};

// Output of the beat analyser.
struct Analysis {
    QList<double> beats;
    // Per-beat curves, parallel to `beats`: the internal shape the scheduler
    // thresholds against. The track-level features live further down.
    QList<double> beatEnergy;
    QList<double> beatBright;
    QList<Section> sections;
    QList<double> patOffsets;
    QList<int> patAt;
    double bpm = 0;
    bool hasBpm = false;
    QString mood = QStringLiteral("mixed");

    // ---- the track, as features ----------------------------------------
    // Measured from the decoded audio, not looked up. 0..1 unless noted.
    double energy = 0;        // loudness × onset density × brightness
    double danceability = 0;  // how strongly the pulse repeats, and how regular it is
    double valence = 0;       // dark .. bright: minor/major key, weighted by brightness
    double brightness = 0;    // spectral centre of mass, 300 Hz = 0, 3.5 kHz = 1
    double loudnessDb = -60;  // RMS of the whole track, dBFS
    int keyPc = -1;           // 0 = C … 11 = B, -1 when it could not be told
    bool major = true;
    QString keyName;          // "A minor", empty when unknown

    bool valid() const { return beats.size() > 1; }
    void clear() { *this = Analysis(); }
};

// A generated schedule: one entry per cut.
struct Cuts {
    QList<double> times;
    QList<double> fades;   // crossfade in ms, parallel to times
    QList<int> picks;      // clip id override, -1 = walk the pool
    int cutCount() const { return times.size(); }
};

// One thing the project imported: a folder (walked recursively) or a single file.
struct MediaSource {
    QString path;        // absolute
    bool isDir = false;
};

// One segment of an edited timeline: clip `id` from in-point `s` for `len` seconds.
struct TlSeg {
    int id = -1;
    double s = 0;
    double len = 0;
};
