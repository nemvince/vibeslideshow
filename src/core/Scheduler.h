#pragma once
#include "core/Types.h"
#include "core/Params.h"
#include <QList>
#include <QPair>
#include <QRandomGenerator>
#include <algorithm>
#include <memory>

// Deterministic RNG for reproducible edits: mulberry32, the same stream the
// the same stream, so a fixed seed yields the same show.
class SeededRng {
public:
    explicit SeededRng(quint32 seed = 0) : m_s(seed) {}
    // 0..1 double
    double next();
private:
    quint32 m_s = 0;
};

// Turns an analysis + a pool into cut times, fades and clip picks. Every rule
// here is deterministic given the analysis, the pool and the seed.
class Scheduler {
public:
    Scheduler() = default;
    Scheduler(const Scheduler &) = delete;
    Scheduler &operator=(const Scheduler &) = delete;

    void setParams(const Params &p) { m_p = p; }
    const Params &params() const { return m_p; }
    void setAnalysis(const Analysis &a) { m_a = a; }
    const Analysis &analysis() const { return m_a; }
    void setPool(const QList<Clip> &pool) { m_pool = pool; }
    const QList<Clip> &pool() const { return m_pool; }
    // How bright a scene the edit should aim for, 0..100 from the pool filters.
    void setSceneBright(double pct) { m_sceneBright = std::clamp(pct, 0.0, 100.0); }
    double sceneBright() const { return m_sceneBright; }
    void reseed();          // restart the RNG from P.seed (0 = non-deterministic)
    double rnd();           // consumes the seeded stream, or random when seed is 0

    // Generated schedule for the whole song. `songDuration` in seconds.
    Cuts buildCuts(double songDuration) const;
    // No-beat fallback: a plain grid.
    QList<double> fallbackGrid(double songDuration) const;

    // Chronological walk order of the pool (day, then time of day, then id),
    // optionally shuffled on the seeded stream.
    QList<Clip> orderedPool(bool shuffleOn);

    // Deterministic in-point inside a clip so a segment of `len` seconds fits.
    double dur2seek(const Clip &c, double len, double r);
    // In-point for `len` seconds of `c` that does not overlap any `blocked`
    // clip-time range, aimed at the scene brightness set by setSceneBright().
    // Clips may appear many times as long as each shot comes from a different
    // stretch; returns -1 when no such stretch is left. `lum`, when given,
    // receives the luminance of the chosen window (0..255).
    double seekFree(const Clip &c, double len, const QList<QPair<double, double>> &blocked,
                    double *lum = nullptr);
    // Random neighbouring clip for a chop/swap: never the same as this segment
    // or its immediate neighbours, and preferably a well-scored one.
    const Clip *pickNeighbour(const QList<Clip> &base, int replaceId, int prevId, int nextId);

    // Materialise the classic schedule into an editable timeline. `base` is the
    // filtered pool in walk order (the caller's poolFor() + orderedPool()); when
    // empty the scheduler's own pool is used. `cutsOut`, when given, receives the
    // schedule the segments came from, fades included.
    QList<TlSeg> materialize(double songDuration, const QList<Clip> &base, Cuts *cutsOut = nullptr);

    // The move shot `i` gets, starting at song time `t`: alternating push-in /
    // pull-out, sized by the section it lands in and by P.moveAmt, and still when
    // P.moveOn is off. Public because the edit re-applies it when that setting
    // changes, without re-cutting anything.
    TlMove moveFor(int i, double t) const;

private:
    Params m_p;
    Analysis m_a;
    QList<Clip> m_pool;
    double m_sceneBright = 60;   // 0..100
    std::unique_ptr<SeededRng> m_rng;

    // The slider turned into an average luminance (0..255). Real footage
    // rarely leaves 0..220, so the full travel covers dim to sunlit.
    double sceneTargetLum() const { return m_sceneBright * 2.2; }
    // How much movement the music wants at `t`: 0..1 from the section map.
    double wantedMotion(double t) const;
    int secIdxAt(double t) const;
    double fadeFor(double t) const;
    QList<int> dropBestIds() const;
};
