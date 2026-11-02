#include "core/Analyzer.h"

#include <QProcess>
#include <QByteArray>
#include <QHash>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

// Median of an already-sorted list; mirrors JS's gsort[floor(len/2)].
double medianOfSorted(const QList<double> &v) {
    if (v.isEmpty()) return 0;
    return v.at(v.size() / 2);
}

int argMaxStrength(const QList<double> &s) {
    int am = 0;
    for (int i = 1; i < s.size(); i++)
        if (s[i] > s[am]) am = i;
    return am;
}

// Round to 3 decimals, the precision the scheduler thresholds were tuned at.
inline double r3(double v) { return std::round(v * 1000.0) / 1000.0; }

} // namespace

bool Analyzer::decodeAudio(const QString &path, QList<float> &pcm, QString *error) {
    QProcess p;
    p.start(QStringLiteral("ffmpeg"),
            {QStringLiteral("-v"), QStringLiteral("error"),
             QStringLiteral("-i"), path,
             QStringLiteral("-ac"), QStringLiteral("1"),
             QStringLiteral("-ar"), QString::number(SampleRate),
             QStringLiteral("-f"), QStringLiteral("f32le"),
             QStringLiteral("pipe:1")});
    if (!p.waitForStarted(10000)) {
        if (error) *error = QStringLiteral("could not start ffmpeg");
        return false;
    }
    QByteArray out;
    // Read while running so a long song cannot deadlock on pipe buffers.
    p.setReadChannel(QProcess::StandardOutput);
    while (p.state() != QProcess::NotRunning) {
        p.waitForReadyRead(200);
        out.append(p.readAllStandardOutput());
    }
    out.append(p.readAllStandardOutput());
    const QByteArray err = p.readAllStandardError();
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (error) *error = err.trimmed().isEmpty() ? QStringLiteral("could not decode audio") : QString::fromLocal8Bit(err.trimmed());
        return false;
    }
    const int n = out.size() / 4;
    if (n <= 0) {
        if (error) *error = QStringLiteral("could not decode audio");
        return false;
    }
    pcm.resize(n);
    std::memcpy(pcm.data(), out.constData(), size_t(n) * sizeof(float));
    return true;
}

Analysis Analyzer::analyze(const QList<float> &pcm, int sr) {
    Analysis a;
    const int win = 1024, hop = 512;
    const int len = pcm.size();
    const int n = std::max(1, (len - win) / hop + 1);

    // RMS energy per hop-sized frame.
    QList<double> e(n);
    for (int i = 0; i < n; i++) {
        double sum = 0;
        const int off = i * hop;
        for (int j = 0; j < win; j++) {
            const double v = (off + j < len) ? double(pcm[off + j]) : 0.0;
            sum += v * v;
        }
        e[i] = std::sqrt(sum / win);
    }
    // Onset novelty: positive energy jumps only.
    QList<double> nov(n, 0.0);
    for (int i = 1; i < n; i++) nov[i] = std::max(0.0, e[i] - e[i - 1]);
    // 3-tap smoothing.
    QList<double> sm(n, 0.0);
    for (int i = 0; i < n; i++) {
        double s = 0;
        int c = 0;
        for (int k = i - 1; k <= i + 1; k++)
            if (k >= 0 && k < n) { s += nov[k]; c++; }
        sm[i] = s / std::max(1, c);
    }

    // Peak picking: a local maximum that beats its neighbourhood mean by 1.5x,
    // no closer than 0.25 s apart.
    QList<double> beats, strengths;
    const int R = 43;
    double last = -1;
    for (int i = 1; i < n - 1; i++) {
        if (!(sm[i] >= sm[i - 1] && sm[i] >= sm[i + 1])) continue;
        double s = 0;
        int c = 0;
        for (int k = std::max(0, i - R); k <= std::min(n - 1, i + R); k++) { s += sm[k]; c++; }
        if (sm[i] <= (s / std::max(1, c)) * 1.5) continue;
        const double t = (i * double(hop)) / sr;
        if (last >= 0 && t - last < 0.25) continue;
        beats.append(t);
        strengths.append(sm[i]);
        last = t;
    }
    for (int i = 0; i < beats.size(); i++) beats[i] = std::round(beats[i] * 1000.0) / 1000.0;
    if (beats.isEmpty() || beats[0] > 0.25) {
        beats.prepend(0);
        strengths.prepend(0);
    }

    // Pulse snap: re-anchor onsets onto exact multiples of the median interval
    // around the strongest onset, so cuts land squarely on the pulse.
    if (beats.size() >= 4) {
        QList<double> gaps;
        for (int i = 1; i < beats.size(); i++) gaps.append(beats[i] - beats[i - 1]);
        QList<double> gsort = gaps;
        std::sort(gsort.begin(), gsort.end());
        const double med = medianOfSorted(gsort);
        if (med >= 0.15 && med <= 1.5) {
            const int am = argMaxStrength(strengths);
            const double A = beats[am];
            QList<double> sB, sS;
            sB.reserve(beats.size());
            sS.reserve(beats.size());
            for (int i = 0; i < beats.size(); i++) {
                const double kk = std::round((beats[i] - A) / med);
                const double target = A + kk * med;
                const bool snap = std::fabs(beats[i] - target) <= 0.14 * med && beats[i] != A;
                sB.append(snap ? std::round(target * 1000.0) / 1000.0 : beats[i]);
                sS.append(strengths[i]);
            }
            // Two onsets landing on the same slot: keep the stronger one.
            QList<double> oB, oS;
            for (int i = 0; i < sB.size(); i++) {
                if (!oB.isEmpty() && sB[i] == oB.last()) {
                    if (sS[i] > oS.last()) oS.last() = sS[i];
                    continue;
                }
                oB.append(sB[i]);
                oS.append(sS[i]);
            }
            beats = oB;
            strengths = oS;
        }
    }

    if (beats.size() >= 2) {
        QList<double> gaps;
        for (int i = 1; i < beats.size(); i++) gaps.append(beats[i] - beats[i - 1]);
        std::sort(gaps.begin(), gaps.end());
        const double med = medianOfSorted(gaps);
        if (med > 0) {
            a.bpm = std::round((60.0 / med) * 10.0) / 10.0;
            a.hasBpm = true;
        }
    }

    // Strengths normalised to 0..1 — the "energy" curve buildCuts thresholds on.
    double maxStrength = 1e-9;
    for (double s : strengths) maxStrength = std::max(maxStrength, s);
    QList<double> energy;
    energy.reserve(strengths.size());
    for (double s : strengths) energy.append(r3(s / maxStrength));

    // Per-beat brightness (zero-crossing rate): tells calm airy passages from
    // bright, busy ones.
    QList<double> bright;
    if (beats.size() >= 2) {
        QList<double> zs;
        double maxZ = 0;
        for (int bi = 0; bi < beats.size(); bi++) {
            const int lo = int(std::floor(beats[bi] * sr));
            const double endT = bi + 1 < beats.size() ? beats[bi + 1] : beats[bi] + 0.5;
            const int hi = std::min(len, int(std::floor(endT * sr)));
            int flips = 0;
            for (int k = lo + 1; k < hi; k++)
                if ((pcm[k] >= 0) != (pcm[k - 1] >= 0)) flips++;
            const double z = flips / double(std::max(1, hi - lo));
            zs.append(z);
            if (z > maxZ) maxZ = z;
        }
        for (double z : zs) bright.append(r3(z / (maxZ > 0 ? maxZ : 1)));
    }

    // Song sections from the energy curve, classified against the song's own
    // median so masters with little dynamics still sort out.
    if (beats.size() >= 8) {
        QList<double> pos;
        for (int i = 1; i < energy.size(); i++)
            if (energy[i] > 0) pos.append(energy[i]);
        std::sort(pos.begin(), pos.end());
        double medE = pos.isEmpty() ? 0.25 : pos.at(pos.size() / 2);
        if (medE == 0) medE = 0.25;
        const int W = 8;
        struct Row { double s, e, b, ra; int bi; };
        QList<Row> rows;
        for (int bi = 0; bi + 1 < beats.size(); bi += W) {
            double sum = 0, bs = 0;
            int c = 0;
            for (int k = bi; k < beats.size() && k < bi + W; k++) {
                sum += energy[k];
                bs += (k < bright.size()) ? bright[k] : 0.5;
                c++;
            }
            if (c) {
                const double ev = sum / c;
                rows.append({beats[bi], ev, bs / c, ev / medE, bi});
            }
        }
        for (int i = 0; i < rows.size(); i++) {
            const double bR = rows[std::max(0, i - 1)].ra;
            const double aR = rows[std::min(int(rows.size()) - 1, i + 1)].ra;
            const Row &r = rows[i];
            QString kind;
            if (r.ra - bR > 0.3 && aR > r.ra) kind = QStringLiteral("build");
            else if (r.ra >= 1.35 && bR <= r.ra - 0.3) kind = QStringLiteral("drop");
            else if (r.ra < 0.65) kind = QStringLiteral("chill");
            else if (r.ra >= 1.35) kind = QStringLiteral("loud");
            else kind = QStringLiteral("mid");
            a.sections.append({std::round(r.s * 100.0) / 100.0, r3(r.e), r3(r.b), kind, r.bi});
        }
    }

    if (!a.sections.isEmpty()) {
        double sum = 0;
        for (double v : e) sum += v;
        const double lvl = std::round(1000.0 * sum / std::max(1, n)) / 1000.0;
        int calm = 0;
        for (const auto &s : a.sections)
            if (s.e < 0.3) calm++;
        const double calmFrac = calm / double(a.sections.size());
        bool anyDrop = false;
        for (const auto &s : a.sections)
            if (s.kind == QLatin1String("drop")) anyDrop = true;
        const double bpm = a.hasBpm ? a.bpm : 0;
        if (lvl >= 0.25 && bpm >= 150) a.mood = QStringLiteral("aggressive");
        else if (lvl >= 0.15 && anyDrop) a.mood = QStringLiteral("energetic");
        else if (lvl < 0.08 || calmFrac >= 0.5) a.mood = QStringLiteral("calm");
        else a.mood = QStringLiteral("mid");
    }

    // Stutter signature: off-beat hits recurring at a consistent position inside
    // the beat interval (chopped-vocal doubles, half-beat stabs, ...).
    if (beats.size() >= 10) {
        QList<double> pos;
        for (double s : strengths)
            if (s > 0) pos.append(s);
        std::sort(pos.begin(), pos.end());
        double med = pos.isEmpty() ? 1 : pos.at(pos.size() / 2);
        if (med <= 0) med = 1;

        QHash<int, QList<double>> micro;
        int bi = 0;
        for (int f = 1; f < n - 1; f++) {
            if (!(sm[f] >= sm[f - 1] && sm[f] >= sm[f + 1])) continue;
            if (sm[f] < 0.35 * med) continue;
            const double t = (f * double(hop)) / sr;
            while (bi + 1 < beats.size() && beats[bi + 1] <= t) bi++;
            if (bi + 1 >= beats.size()) continue;
            const double off = (t - beats[bi]) / (beats[bi + 1] - beats[bi]);
            if (off < 0.04 || off > 0.96) continue;
            micro[bi].append(std::round(off * 20.0) / 20.0);
        }
        struct Sig { QString key; int count = 0; QList<int> at; QList<double> offsets; };
        QList<Sig> sigs;
        QHash<QString, int> index;
        for (int k = 0; k < beats.size(); k++) {
            auto it = micro.constFind(k);
            if (it == micro.constEnd() || it->isEmpty()) continue;
            QList<double> uniq = *it;
            std::sort(uniq.begin(), uniq.end());
            uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());
            if (uniq.isEmpty()) continue;
            QStringList parts;
            for (double v : uniq) parts.append(QString::number(v));
            const QString key = parts.join(QLatin1Char('|'));
            auto idx = index.constFind(key);
            if (idx == index.constEnd()) {
                Sig s;
                s.key = key;
                s.offsets = uniq;
                sigs.append(s);
                index.insert(key, sigs.size() - 1);
                idx = index.constFind(key);
            }
            Sig &s = sigs[idx.value()];
            s.count++;
            s.at.append(k);
        }
        // Most repeated signature wins; 5 beats minimum before it counts at all.
        int best = -1;
        for (int i = 0; i < sigs.size(); i++)
            if (sigs[i].count >= 5 && (best < 0 || sigs[i].count > sigs[best].count)) best = i;
        if (best >= 0) {
            a.patOffsets = sigs[best].offsets;
            a.patAt = sigs[best].at;
        }
    }

    a.beats = beats;
    a.energy = energy;
    a.bright = bright;
    return a;
}
