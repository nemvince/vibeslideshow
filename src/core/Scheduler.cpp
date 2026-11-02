#include "core/Scheduler.h"
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
inline int jsRound(double v) { return int(std::round(v)); }  // JS Math.round semantics for the ranges used
}

double SeededRng::next() {
    m_s = (m_s + 0x6D2B79F5u);
    quint32 z = m_s;
    z = quint32((quint64(z ^ (z >> 15)) * quint64(z | 1u)) & 0xFFFFFFFFu);
    z ^= z + quint32((quint64(z ^ (z >> 7)) * quint64(z | 61u)) & 0xFFFFFFFFu);
    return double((z ^ (z >> 14))) / 4294967296.0;
}

void Scheduler::reseed() {
    m_rng.reset(m_p.seed ? new SeededRng(quint32(m_p.seed)) : nullptr);
}

double Scheduler::rnd() {
    if (m_rng) return m_rng->next();
    return QRandomGenerator::global()->generateDouble();
}

int Scheduler::secIdxAt(double t) const {
    const auto &secs = m_a.sections;
    for (int s = 0; s < secs.size(); s++) {
        if (t >= secs[s].s && (s + 1 >= secs.size() || t < secs[s + 1].s)) return s;
    }
    return -1;
}

double Scheduler::fadeFor(double t) const {
    if (!m_p.mood) return m_p.fadeMs;
    const int si = secIdxAt(t);
    const QString kind = si >= 0 ? m_a.sections[si].kind : QStringLiteral("mid");
    if (kind == QLatin1String("loud") || kind == QLatin1String("drop")) return 0;
    if (kind == QLatin1String("build")) return std::round(m_p.fadeMs * 0.5);
    return m_p.fadeMs;
}

QList<int> Scheduler::dropBestIds() const {
    QList<Clip> pick;
    for (const Clip &c : m_pool) {
        if (m_p.skipJunk && c.dk >= 0.5) continue;
        if (c.score < 8) continue;
        pick.append(c);
    }
    std::stable_sort(pick.begin(), pick.end(), [](const Clip &a, const Clip &b) { return b.score < a.score; });
    QList<int> ids;
    for (int i = 0; i < pick.size() && i < 12; i++) ids.append(pick[i].id);
    return ids;
}

QList<double> Scheduler::fallbackGrid(double songDuration) const {
    QList<double> g;
    if (std::isfinite(songDuration) && songDuration > 0) {
        const double step = m_p.gridSec > 0 ? m_p.gridSec : 0.5;
        for (double t = 0; t < songDuration; t += step) g.append(std::round(t * 1000.0) / 1000.0);
    }
    if (g.isEmpty()) g.append(0);
    return g;
}

Cuts Scheduler::buildCuts(double songDuration) const {
    const auto &bts = m_a.beats;
    const auto &en = m_a.energy;
    const auto &secs = m_a.sections;

    QList<double> cuts, ce;
    QList<int> ci;
    QList<int> cutPick;

    const double hi = std::max(m_p.energyHi, m_p.energyLo + 0.05);
    const double lo = std::min(m_p.energyLo, m_p.energyHi - 0.05);

    // Drop statements: the drop opens on one long, best-scored clip.
    QSet<double> dropT;
    QSet<int> dropIdx;
    QList<int> dropBest;
    if (m_p.mood && !secs.isEmpty()) {
        for (const auto &x : secs) {
            if (x.kind == QLatin1String("drop")) {
                dropT.insert(x.s);
                dropIdx.insert(x.bi);
            }
        }
        if (!dropT.isEmpty()) dropBest = dropBestIds();
    }

    // Aggressivity scales holds across the board and eases the aggressive threshold.
    const double ag = std::max(0.0, std::min(2.0, m_p.aggro));
    const double floor_ = m_p.aggrFloor != 0 ? m_p.aggrFloor : 0.6;
    const double hiTh = std::max(floor_, std::min(1.0, m_p.aggrThreshold + (1 - ag) * 0.1));
    constexpr double MINCUT = 0.3;   // shorter slices read as flash strobes
    int dropRank = 0;

    int i = 0;
    const int nB = bts.size();
    while (i < nB && bts[i] < songDuration - 0.05) {
        // Chatter half-hits are not cut positions.
        if (!cuts.isEmpty() && bts[i] - cuts.last() < MINCUT) { i++; continue; }
        const double e = (en.size() == bts.size()) ? en[i] : 0.5;
        ce.append(e);
        cuts.append(bts[i]);
        ci.append(i);

        const double f = e >= hi ? 0 : (e <= lo ? 1 : (hi - e) / (hi - lo));
        int hold = jsRound(1 + f * (m_p.holdMax - 1));
        if (ag < 1) hold = jsRound(hold * (1 + (1 - ag) * 0.8));                 // toward long holds
        else if (ag > 1) hold = std::max(1, jsRound(hold / (1 + (ag - 1) * 1.5))); // toward every beat

        if (m_p.mood) {
            // Intensity-coupled holds: chill lingers, loud speeds up, builds
            // accelerate into the drop, the drop opener holds long.
            const int siH = secIdxAt(bts[i]);
            if (siH >= 0) {
                const Section &secH = secs[siH];
                const double endH = (siH + 1 < secs.size()) ? secs[siH + 1].s : songDuration;
                if (secH.kind == QLatin1String("chill"))
                    hold = std::min(m_p.holdMax, jsRound(hold * 1.35));
                else if (secH.kind == QLatin1String("loud"))
                    hold = std::max(1, jsRound(hold * 0.6));
                else if (secH.kind == QLatin1String("build")) {
                    const double pos = std::max(0.0, std::min(1.0, (bts[i] - secH.s) / std::max(0.05, endH - secH.s)));
                    hold = std::max(1, jsRound(hold * (1.7 - 1.5 * pos)));
                } else if (secH.kind == QLatin1String("drop")) {
                    const double nxt = (i + 1 < nB) ? bts[i + 1] - bts[i] : 0.5;
                    hold = std::max(hold, std::min(m_p.holdMax * 2, jsRound((endH - bts[i]) / std::max(0.05, nxt))));
                }
            }
        }

        const bool dropHere = dropT.contains(bts[i]) && !dropBest.isEmpty();
        cutPick.append(dropHere ? dropBest[dropRank++ % dropBest.size()] : -1);

        // Land the walk exactly on the next drop's opening beat.
        int i2 = (i + hold >= nB) ? nB : i + hold;
        if (!dropIdx.isEmpty()) {
            int dbest = std::numeric_limits<int>::max();
            for (int b : dropIdx)
                if (b > i && b < i2 && b < dbest) dbest = b;
            if (dbest != std::numeric_limits<int>::max()) i2 = dbest;
        }
        i = (i2 >= nB) ? nB : i2;
    }
    if (cuts.isEmpty()) {
        cuts.append(0);
        cutPick.append(-1);
    }

    // Pattern offsets: which beats carry the song's own off-beat hits.
    QSet<int> at;
    for (int b : m_a.patAt) at.insert(b);
    QList<double> offs;
    if (m_p.replicate) {
        QSet<double> seen;
        for (double o : m_a.patOffsets) {
            if (o <= 0.04 || o >= 0.96) continue;
            if (seen.contains(o)) continue;
            seen.insert(o);
            offs.append(o);
        }
        std::sort(offs.begin(), offs.end());
    }

    QList<double> out;
    QList<int> outPick;   // parallel to `out`; -1 = walk the pool
    QSet<double> insSet;
    double lastStutter = -1e9;
    int count = 0, insTotal = 0;
    const int insMax = m_p.insMax > 0 ? m_p.insMax : 48;

    for (int k = 0; k < cuts.size(); k++) {
        const double t0 = cuts[k];
        const double segEnd = (k + 1 < cuts.size()) ? cuts[k + 1] : songDuration;
        out.append(t0);
        outPick.append(cutPick.value(k, -1));
        if (k + 1 >= cuts.size() && segEnd <= t0) continue;

        QList<double> inserted;

        // 1. The song's own off-beat hits inside this segment.
        if (!offs.isEmpty()) {
            const int jMax = (k + 1 < cuts.size()) ? ci[k + 1] : nB - 1;
            for (int j = ci[k]; j <= jMax; j++) {
                if (!at.contains(j)) continue;
                const double jEnd = (j + 1 < nB) ? bts[j + 1] : songDuration;
                if (jEnd <= bts[j] || jEnd >= segEnd + 1e-9) continue;
                for (double off : offs) {
                    const double pt = bts[j] + off * (jEnd - bts[j]);
                    if (pt > t0 + 0.08 && pt < segEnd - 0.08 && !inserted.contains(pt) && insTotal < insMax) {
                        inserted.append(pt);
                        insSet.insert(pt);
                        insTotal++;
                    }
                }
            }
        }

        // 2. Aggressive runs: split each strong beat into half/quarter cuts. The
        // run length scans the global beat list, so density survives per-beat segments.
        if (k + 1 < cuts.size() && insTotal < insMax && !dropT.contains(t0)) {
            const int b0 = ci[k], b1 = ci[k + 1];
            for (int jg = b0; jg < b1 && insTotal < insMax; jg++) {
                const double ejg = (en.size() == bts.size()) ? en[jg] : 0.5;
                if (!(ejg >= hiTh)) continue;
                int runLen = 1;
                for (int back = jg - 1; back >= 0; back--) {
                    const double eb = (en.size() == bts.size()) ? en[back] : 0.5;
                    if (!(eb >= hiTh)) break;
                    runLen++;
                }
                for (int fwd = jg + 1; fwd < nB; fwd++) {
                    const double ef = (en.size() == bts.size()) ? en[fwd] : 0.5;
                    if (!(ef >= hiTh)) break;
                    runLen++;
                }
                const double jgEnd = (jg + 1 < nB) ? bts[jg + 1] : songDuration;
                const double intv = jgEnd - bts[jg];
                QList<double> of;
                if (runLen >= m_p.aggrQuarterRun) of = {0.25, 0.5, 0.75};
                else if (runLen >= m_p.aggrHalfRun) of = {0.5};
                for (double o : of) {
                    const double pt2 = bts[jg] + o * intv;
                    if (pt2 > t0 + 0.08 && pt2 < segEnd - 0.08 && !inserted.contains(pt2)) {
                        inserted.append(pt2);
                        insSet.insert(pt2);
                        insTotal++;
                    }
                }
            }
        }

        // 3. Otherwise a taste split on slow→loud ramps and strong accents.
        if (inserted.isEmpty() && k + 1 < cuts.size() && !dropT.contains(t0)) {
            const double gap = segEnd - t0;
            const double bpm = m_a.hasBpm ? m_a.bpm : 0;
            const int splitN = std::max(2, m_p.split);
            const bool ramp = (ce.size() > k + 1) && (ce[k + 1] - ce[k] >= m_p.stutterRamp || ce[k + 1] >= m_p.stutterAccent);
            if (m_p.stutter && bpm < m_p.stutterMaxBpm && gap >= m_p.stutterMinHold && count < m_p.stutterMax &&
                segEnd - lastStutter > m_p.stutterGap && ramp) {
                for (int q = 1; q < splitN; q++) {
                    const double pst = t0 + gap * q / splitN;
                    inserted.append(pst);
                    insSet.insert(pst);
                }
                lastStutter = segEnd;
                count++;
            }
        }

        std::sort(inserted.begin(), inserted.end());
        // Flash guard: no slice shorter than ~9 frames, whatever inserted it.
        constexpr double MINSLICE = 0.3;
        QList<double> dedup;
        for (double t : inserted) {
            const double lastIns = dedup.isEmpty() ? t0 : dedup.last();
            if (t - lastIns >= MINSLICE && segEnd - t >= MINSLICE) dedup.append(t);
        }
        for (int i = 0; i < dedup.size(); i++) outPick.append(-1);
        out += dedup;
    }

    // Per-cut crossfade: drops/loud are hard, chill dissolves long, inserted
    // micro-cuts are hard outside chill sections.
    Cuts result;
    result.times = out;
    result.picks = outPick;
    for (double t : out) {
        const int si = secIdxAt(t);
        if (m_p.mood && si >= 0 && secs[si].kind == QLatin1String("chill")) {
            result.fades.append(std::min(400.0, std::max(150.0, std::round(m_p.fadeMs * 2.5))));
        } else if (!m_p.mood) {
            result.fades.append(m_p.fadeMs);
        } else if (insSet.contains(t)) {
            result.fades.append(0);
        } else {
            result.fades.append(fadeFor(t));
        }
    }
    return result;
}

QList<Clip> Scheduler::orderedPool(bool shuffleOn) {
    QList<Clip> base = m_pool;
    std::stable_sort(base.begin(), base.end(), [](const Clip &a, const Clip &b) {
        const QString da = a.day.isEmpty() ? QStringLiteral("9999-12-31") : a.day;
        const QString db = b.day.isEmpty() ? QStringLiteral("9999-12-31") : b.day;
        if (da != db) return da < db;
        const double ta = a.hasTod ? a.tod : 1e9;
        const double tb = b.hasTod ? b.tod : 1e9;
        if (ta != tb) return ta < tb;
        return a.id < b.id;
    });
    if (shuffleOn) {
        // Fisher-Yates on the seeded stream: a fixed seed reproduces the shuffle.
        for (int i = base.size() - 1; i > 0; i--) {
            const int j = int(std::floor(rnd() * (i + 1)));
            std::swap(base[i], base[j]);
        }
    }
    return base;
}

double Scheduler::dur2seek(const Clip &c, double len, double r) {
    const double d = (m_p.useLrf && c.lrfDur > 0) ? c.lrfDur : c.duration;
    return r * std::max(0.0, d - len - 0.1);
}

const Clip *Scheduler::pickNeighbour(const QList<Clip> &base, int replaceId, int prevId, int nextId) {
    if (base.isEmpty()) return nullptr;
    for (int tr = 0; tr < 10; tr++) {
        const Clip &v = base[int(std::floor(rnd() * base.size())) % base.size()];
        if (v.score >= 8 && v.id != replaceId && v.id != prevId && v.id != nextId) return &v;
    }
    for (const Clip &v : base)
        if (v.id != replaceId && v.score >= 8) return &v;
    return &base.first();
}

QList<TlSeg> Scheduler::materialize(double songDuration, const QList<Clip> &baseIn, Cuts *cutsOut) {
    const Cuts cc = (m_a.valid()) ? buildCuts(songDuration) : Cuts{{fallbackGrid(songDuration)}, {}, {}};
    if (cutsOut) *cutsOut = cc;
    if (cc.times.size() < 2) return {};

    QList<Clip> base = baseIn.isEmpty() ? m_pool : baseIn;
    if (base.isEmpty()) return {};

    constexpr double TL_MIN = 0.05;
    QList<TlSeg> segs;
    for (int i = 0; i < cc.times.size(); i++) {
        const double end = (i + 1 < cc.times.size()) ? cc.times[i + 1] : songDuration;
        segs.append({-1, 0, std::max(TL_MIN, end - cc.times[i])});
    }
    int pos = 0;
    for (int i = 0; i < segs.size(); i++) {
        // A pick means the cut itself names the clip: drop openers land on the
        // best-scored shot on purpose, and the chronological walk is not consumed.
        const int pick = (i < cc.picks.size()) ? cc.picks[i] : -1;
        if (pick >= 0) {
            for (const Clip &p : base) {
                if (p.id != pick) continue;
                segs[i].id = p.id;
                segs[i].s = dur2seek(p, segs[i].len, rnd());
                break;
            }
            if (segs[i].id >= 0) continue;
        }
        const Clip &c = base[pos % base.size()];
        // Near-black clips score ~0: roll on deterministically.
        if (c.score < 8 && pos <= base.size() * 2) { pos++; i--; continue; }
        segs[i].id = c.id;
        segs[i].s = dur2seek(c, segs[i].len, rnd());
        pos++;
    }
    return segs;
}

